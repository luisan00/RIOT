/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup         cpu_cc26x2_cc13x2_rf
 * @{
 *
 * @file
 * @brief           CC13x2 sub-GHz IEEE 802.15.4g radio: RF Core commands
 *
 * The command structures and sequences in this file are checked against
 * these documents from Texas Instruments:
 *
 * - [TRM]: CC13x2, CC26x2 SimpleLink Wireless MCU Technical Reference
 *   Manual, SWCU185G (June 2024), chapter 25 (Radio). Section 25.10 covers
 *   the proprietary mode and 25.10.5.2.1 the IEEE 802.15.4g packet format.
 * - [SDK]: SimpleLink CC13xx/CC26xx SDK 8.33.00.16. The RF driver is
 *   `source/ti/drivers/rf/RFCC26X2_multiMode.c`.
 * - [RFCFG]: the radio settings shipped with [SDK] in
 *   `source/ti/devices/radioconfig/.meta/config`, the same data SmartRF
 *   Studio and SysConfig use. The MR-FSK modes come from its 2-GFSK
 *   IEEE 802.15.4 and Wi-SUN settings, see @ref _modes.
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 * @}
 */

#include <errno.h>
#include <string.h>

#include "cc26x2_cc13x2_rf_internal.h"
#include "cc26x2_cc13x2_rfc.h"
#include "vendor/rf_patch_cpe_prop.h"
#include "rf_conf.h"

#include "net/ieee802154.h"

#include "byteorder.h"
#include "irq.h"

#define ENABLE_DEBUG 0
#include "debug.h"

/**
 * @name    PHY parameters shared by every MR-FSK mode
 *
 * All the IEEE 802.15.4 settings of [RFCFG] use the same sync word and
 * whitening, only the modulation and the receiver change between modes, see
 * @ref _modes.
 * @{
 */
/**
 * @brief   Number of sync word bits
 */
#define SYNC_WORD_BITS      (24U)

/**
 * @brief   Sync word, 0x0055904E
 *
 * The 16-bit SFD of an uncoded MR-FSK frame (phyMRFSKSFD = 0), preceded by
 * one byte of preamble pattern. The radio sends the 24 bits MSB first.
 */
#define SYNC_WORD           (0x00550000UL | IEEE802154_PHY_MR_FSK_2FSK_UNCODED_SFD_0)
/** @} */

/**
 * @brief   Board hook for the RF front end
 *
 * Called with `false` when the radio is initialized and whenever the driver
 * goes back to the standard PA, and with `true` when it selects the 20 dBm
 * PA. A board with an RF switch sets it for the sub-GHz band here.
 */
#ifndef CC26X2_CC13X2_RF_FRONTEND_SETUP
#  define CC26X2_CC13X2_RF_FRONTEND_SETUP(pa20)     (void)(pa20)
#endif

/**
 * @brief   Whether the 20 dBm PA can be used: the option is on and the board
 *          has a table for it in the sub-GHz band
 */
#if IS_ACTIVE(CONFIG_CC26X2_CC13X2_RF_PA20) && defined(CC26X2_CC13X2_PA20_TABLE_NUMOF)
#  define HAVE_PA20                     (1)
#else
#  define HAVE_PA20                     (0)
#endif

/**
 * @brief   Transmit buffer length.
 *
 * Format is:
 *
 * ```
 * +-----+------+
 * | PHR | PSDU |
 * +-----+------+
 * ```
 */
#define TX_BUF_LEN         ALIGN_TO_4(IEEE802154_PHY_MR_FSK_PHR_LEN + \
                                      IEEE802154G_FRAME_LEN_MAX)

/**
 * @brief   Receive buffer length.
 *
 * This is the length of an individual receive buffer.
 *
 * Format is:
 *
 * ```
 * +------------------+------+--------+
 * | rfc_data_entry_t | PSDU | Status |
 * +------------------+------+--------+
 * ```
 */
#define RX_BUF_LEN         ALIGN_TO_4(sizeof(rfc_data_entry_t) + \
                                      IEEE802154G_FRAME_LEN_MAX +  \
                                      CC26X2_CC13X2_RF_RX_STATUS_LEN)

static uint8_t _tx_buf[TX_BUF_LEN]  __attribute__ ((aligned(4))); /**< Transmit buffer */
static rfc_data_queue_t _rx_queue __attribute__ ((aligned(4)));  /**< Receive data queue */
/** Receive buffer configured as a circular buffer, e.g.: the last one points
 * to the first, each buffer contains @ref rfc_data_entry_t which is used
 * by the RF Core to mark the buffers that have available data when a packet
 * is received, each buffer represents a different packet. */
typedef union {
    rfc_data_entry_t entry;
    uint8_t raw[RX_BUF_LEN];
} rx_buf_t;

static rx_buf_t _rx_buf[CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF]
    __attribute__ ((aligned(4)));

/** CMD_PROP_RX_ADV was accepted by the RF Core and not stopped since */
static bool _rx_submitted;

/** Counters and RSSI that the radio CPU updates while RX runs */
static rfc_prop_output_t _rx_output;

static uint8_t _short_addr[IEEE802154_SHORT_ADDRESS_LEN];
static uint8_t _long_addr[IEEE802154_LONG_ADDRESS_LEN];
static uint16_t _pan_id;

enum {
    STATE_OFF,              /**< the RF Core is off */
    STATE_RFC_POWER_ON,     /**< waiting for the boot of the RF Core */
    STATE_RFC_SETUP,        /**< waiting for the setup command chain */
    STATE_FINISHED,         /**< the radio is on */
} _poweron_state;

/**
 * @name    Radio register overrides of CMD_PROP_RADIO_DIV_SETUP
 *
 * The lists are the `pRegOverride` lists of the [RFCFG] settings of the
 * CC1312R, in the same order, with the comments of [RFCFG]. The CC1352P
 * settings have the same lists, except that they move the "PA ramp off wait
 * time" entry to `pRegOverrideTxStd` (they use the `_PA` variant of the setup
 * command) and, for 200 kbit/s with modulation index 0.5, use another PA ramp
 * time, see #CC26X2_CC13X2_RF_PA_RAMP_200K_H05.
 *
 * Every list starts with the same three entries and ends with
 * #OVERRIDE_PACTL. _load_mode() builds the list given to the RF Core.
 * @{
 */
/* override_prop_common: DC/DC regulator: In Tx, use DCDCCTL5[3:0]=0x7
 * (DITHER_EN=0 and IPEAK=7). */
#define OVERRIDE_DCDCCTL5_REG           ((uint32_t)0x00F788D3)
/* override_prop_common_sub1g: Set RF_FSCA.ANADIV.DIV_SEL_BIAS = 1. Bits
 * [0:16, 24, 30] are don't care.. */
#define OVERRIDE_RF_FSCA_0              ((uint32_t)0x4001405D)
/* override_prop_common_sub1g: same as above, second word */
#define OVERRIDE_RF_FSCA_1              ((uint32_t)0x08141131)
/* Number of the entries above */
#define OVERRIDES_COMMON_NUMOF          (3U)

/* Tx: Configure PA ramp time, PACTL2.RC=0x3 (in ADI0, set PACTL2[4:3]=0x3) */
#define OVERRIDE_PA_RAMP_RC3 \
    RFC_ADI_2HALFREG_OVERRIDE(0, 16, 0x8, 0x8, 17, 0x1, 0x1)
/* Tx: Configure PA ramp time, PACTL2.RC=0x1 (in ADI0, set PACTL2[4:3]=0x1) */
#define OVERRIDE_PA_RAMP_RC1 \
    RFC_ADI_2HALFREG_OVERRIDE(0, 16, 0x8, 0x8, 17, 0x1, 0x0)
/* Tx: Set wait time before turning off ramp to 0x1A (default: 0x1F) */
#define OVERRIDE_RAMP_OFF_WAIT_TIME     RFC_HW_REG_OVERRIDE(0x6028, 0x001A)
/* Rx: Set RSSI offset to adjust reported RSSI by -1 dB (default: -2), trimmed
 * for external bias and differential configuration */
#define OVERRIDE_RSSI_OFFSET_1DB        ((uint32_t)0x000188A3)
/* Rx: Set RSSI offset to adjust reported RSSI by -4 dB (default: -2), trimmed
 * for external bias and differential configuration */
#define OVERRIDE_RSSI_OFFSET_4DB        ((uint32_t)0x000488A3)
/* Set LNA IB boost to 2 */
#define OVERRIDE_LNA_IB_BOOST           RFC_ADI_HALFREG_OVERRIDE(0, 5, 0xF, 0x2)

/**
 * @brief   PA ramp time override of the 200 kbit/s, modulation index 0.5 mode
 *
 * [RFCFG] has PACTL2.RC=0x3 for the CC1312R (`setting_tc146_154g.json`) and
 * PACTL2.RC=0x1 for the CC1352P (`setting_tc746_154g.json`). The rf_conf.h of
 * a CC1352P board defines this to #OVERRIDE_PA_RAMP_RC1.
 */
#ifndef CC26X2_CC13X2_RF_PA_RAMP_200K_H05
#  define CC26X2_CC13X2_RF_PA_RAMP_200K_H05  OVERRIDE_PA_RAMP_RC3
#endif

/* TX power override
 * Tx: Set PA trim to max to maximize its output power
 * (in ADI0, set PACTL0=0xF8)
 *
 * In [RFCFG] this entry is `override_patable_14dbm.json`. The PA tables
 * (`pa_tables/cc13x2_cc26x2_r3/pasettings.json`) attach it to the 14 dBm
 * setting only, the one that also needs VDDR high. So it is only part of the
 * list when the PA table of the board has that entry, i.e. with
 * CONFIG_CC26XX_CC13XX_FORCE_VDDR_HH. */
#if IS_USED(CONFIG_CC26XX_CC13XX_FORCE_VDDR_HH)
#  define OVERRIDE_PACTL                RFC_ADI_REG_OVERRIDE(0, 12, 0xF8)
#  define OVERRIDE_PACTL_NUMOF          (1U)
#else
#  define OVERRIDE_PACTL_NUMOF          (0U)
#endif

/**
 * @name    Overrides that depend on the PA in use
 *
 * From the `pRegOverrideTxStd` and `pRegOverrideTx20` lists of the CC1352P
 * settings of [RFCFG], for example `setting_tc706_154g.json`. [SDK] attaches
 * the second list to the override list of the setup command when the 20 dBm
 * PA is used. They are only part of the list when the 20 dBm PA is available,
 * so that changing the PA in either direction sets every value again.
 *
 * Without the 20 dBm PA the first list is not needed, although TI has it in
 * every CC1352P setting: its value is the one the register has after the
 * setup command anyway. Read back on a CC1352P1 in a build without these
 * entries: 0x0401 at 0x400460A8. With them: 0x0001 once the 20 dBm PA is
 * selected and 0x0401 again with the standard PA.
 * @{
 */
/* Set TXRX pin to 0 in RX and high impedance in idle/TX. */
#define OVERRIDE_TXRX_PIN_STD           RFC_HW_REG_OVERRIDE(0x60A8, 0x0401)
/* Set TXRX pin to 0 in RX/TX and high impedance in idle. */
#define OVERRIDE_TXRX_PIN_PA20          RFC_HW_REG_OVERRIDE(0x60A8, 0x0001)
/* Tx: Configure PA ramping, set wait time before turning off (0x1F ticks of
 * 16/24 us = 20.3 us). */
#define OVERRIDE_RAMP_OFF_WAIT_TIME_PA20 RFC_HW_REG_OVERRIDE(0x6028, 0x001F)
/* The ANADIV radio parameter based on LO divider and front end settings, for
 * the 20 dBm PA: `calcAnaDiv()` in `radioconfig/.meta/override_handler.js`
 * of [SDK] gives 0x11C1 for the LO divider 5 and 0xF1C1 for the LO divider
 * 6, in the upper half of the entry. */
#if CC26X2_CC13X2_RF_LO_DIVIDER == 5
#  define OVERRIDE_ANADIV_PA20          ((uint32_t)0x11C10703)
#else
#  define OVERRIDE_ANADIV_PA20          ((uint32_t)0xF1C10703)
#endif
/* Largest number of these entries in the list: the four of the 20 dBm PA */
#if HAVE_PA20
#  define OVERRIDES_PA_NUMOF            (4U)
#else
#  define OVERRIDES_PA_NUMOF            (0U)
#endif
/** @} */

/* 50 kbit/s, modulation index 0.5 (Wi-SUN mode #1a), setting_tc120 */
static const uint32_t _overrides_50k_h05[] = {
    /* Synth: Set loop bandwidth after lock to 80 kHz (K2) */
    (uint32_t)0xA47E0583,
    (uint32_t)0x000005A3,
    /* Synth: Set loop bandwidth after lock to 80 kHz (K3, LSB) */
    (uint32_t)0xEAE00603,
    /* Synth: Set loop bandwidth after lock to 80 kHz (K3, MSB) */
    (uint32_t)0x00010623,
    /* Synth: Set FREF = 8 MHz */
    (uint32_t)0x000684A3,
    /* Synth: Set FREF dither = 9.6 MHz */
    (uint32_t)0x000584B3,
    OVERRIDE_PA_RAMP_RC3,
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    /* Rx: Set AGC reference level to 0x1E (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x0023),
    OVERRIDE_RSSI_OFFSET_4DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0x9 (in ADI0, set
     * IFAMPCTL3[7:4]=0x9) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0x9),
    /* Enable FB2PLL, adjust for BW */
    RFC_HW_REG_OVERRIDE(0x5320, 0x3A07),
    /* Enable first-order IIR filter based freq offset estimator */
    RFC_HW_REG_OVERRIDE(0x50FC, 0x0003),
    /* Increase sync threshold from 0x1F to 0x23 */
    RFC_HW_REG_OVERRIDE(0x5114, 0x2323),
    OVERRIDE_LNA_IB_BOOST,
    RFC_END_OVERRIDE,
};

/* 50 kbit/s, modulation index 1.0 (IEEE 802.15.4g operating mode #1),
 * setting_tc106_154g */
static const uint32_t _overrides_50k_h1[] = {
    OVERRIDE_PA_RAMP_RC3,
    /* Rx: Set AGC reference level to 0x1A (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x001A),
    OVERRIDE_RSSI_OFFSET_1DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0xD (in ADI0, set
     * IFAMPCTL3[7:4]=0xD) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0xD),
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    RFC_END_OVERRIDE,
};

/* 100 kbit/s, modulation index 0.5, setting_tc152_154g */
static const uint32_t _overrides_100k_h05[] = {
    /* Synth: Set loop bandwidth after lock to 40 kHz (K2) */
    (uint32_t)0x29200583,
    (uint32_t)0x000005A3,
    /* Synth: Set loop bandwidth after lock to 40 kHz (K3, LSB) */
    (uint32_t)0xF5700603,
    /* Synth: Set loop bandwidth after lock to 40 kHz (K3, MSB) */
    (uint32_t)0x00000623,
    /* Synth: Set FREF = 4 MHz */
    (uint32_t)0x000C84A3,
    OVERRIDE_PA_RAMP_RC3,
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    /* Rx: Set AGC reference level to 0x1D (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x001D),
    OVERRIDE_RSSI_OFFSET_1DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0xA (in ADI0, set
     * IFAMPCTL3[7:4]=0xA) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0xA),
    /* Synth: Set dTxM value factor to -50 */
    (uint32_t)0x00CE87E3,
    /* Synth: Set dTxM value threshold to 37 */
    (uint32_t)0x00250803,
    /* Synth: Set calibration fine point code to 60 (default: 64) */
    RFC_HW_REG_OVERRIDE(0x4064, 0x003C),
    RFC_END_OVERRIDE,
};

/* 100 kbit/s, modulation index 1.0, setting_tc137 */
static const uint32_t _overrides_100k_h1[] = {
    /* Synth: Set loop bandwidth after lock to 20 kHz (K2) */
    (uint32_t)0x0A480583,
    (uint32_t)0x000005A3,
    /* Synth: Set loop bandwidth after lock to 20 kHz (K3, LSB) */
    (uint32_t)0x7AB80603,
    /* Synth: Set loop bandwidth after lock to 20 kHz (K3, MSB) */
    (uint32_t)0x00000623,
    /* Synth: Set FREF = 4 MHz */
    (uint32_t)0x000C84A3,
    /* Synth: Set FREF dither = 4.36 MHz */
    (uint32_t)0x000B84B3,
    OVERRIDE_PA_RAMP_RC3,
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    /* Rx: Set AGC reference level to 0x1A (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x001A),
    OVERRIDE_RSSI_OFFSET_1DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0xA (in ADI0, set
     * IFAMPCTL3[7:4]=0xA) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0xA),
    RFC_END_OVERRIDE,
};

/* 150 kbit/s, modulation index 0.5 (Wi-SUN mode #3), setting_tc142 */
static const uint32_t _overrides_150k_h05[] = {
    /* Synth: Set loop bandwidth after lock to 20 kHz (K2) */
    (uint32_t)0x0A480583,
    (uint32_t)0x000005A3,
    /* Synth: Set loop bandwidth after lock to 20 kHz (K3, LSB) */
    (uint32_t)0x7AB80603,
    /* Synth: Set loop bandwidth after lock to 20 kHz (K3, MSB) */
    (uint32_t)0x00000623,
    /* Synth: Set FREF = 4 MHz */
    (uint32_t)0x000C84A3,
    /* Synth: Set FREF dither to 4.36 MHz */
    (uint32_t)0x000B84B3,
    /* Synth: Set dTxM value factor to -50 */
    (uint32_t)0x00CE87E3,
    /* Synth: Set dTxM value threshold to 37 */
    (uint32_t)0x00250803,
    /* Synth: Set calibration fine point code to 60 (default: 64) */
    RFC_HW_REG_OVERRIDE(0x4064, 0x003C),
    OVERRIDE_PA_RAMP_RC3,
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    /* Rx: Set AGC reference level to 0x1A (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x001A),
    OVERRIDE_RSSI_OFFSET_4DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0xB (in ADI0, set
     * IFAMPCTL3[7:4]=0xB) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0xB),
    OVERRIDE_LNA_IB_BOOST,
    RFC_END_OVERRIDE,
};

/* 200 kbit/s, modulation index 0.5, setting_tc146_154g */
static const uint32_t _overrides_200k_h05[] = {
    CC26X2_CC13X2_RF_PA_RAMP_200K_H05,
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    /* Rx: Set AGC reference level to 0x16 (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x0016),
    OVERRIDE_RSSI_OFFSET_1DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0x8 (in ADI0, set
     * IFAMPCTL3[7:4]=0x8) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0x8),
    RFC_END_OVERRIDE,
};

/* 200 kbit/s, modulation index 1.0, setting_tc151 */
static const uint32_t _overrides_200k_h1[] = {
    /* Synth: Set loop bandwidth after lock to 20 kHz (K2) */
    (uint32_t)0x0A480583,
    (uint32_t)0x000005A3,
    /* Synth: Set loop bandwidth after lock to 20 kHz (K3, LSB) */
    (uint32_t)0x7AB80603,
    /* Synth: Set loop bandwidth after lock to 20 kHz (K3, MSB) */
    (uint32_t)0x00000623,
    /* Synth: Set FREF = 4 MHz */
    (uint32_t)0x000C84A3,
    /* Synth: Set FREF dither to 4.36 MHz */
    (uint32_t)0x000B84B3,
    OVERRIDE_PA_RAMP_RC3,
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    /* Rx: Set AGC reference level to 0x1B (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x001B),
    OVERRIDE_RSSI_OFFSET_1DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0xC (in ADI0, set
     * IFAMPCTL3[7:4]=0xC) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0xC),
    RFC_END_OVERRIDE,
};

/* 300 kbit/s, modulation index 0.5 (Wi-SUN mode #5), setting_tc154 */
static const uint32_t _overrides_300k_h05[] = {
    OVERRIDE_PA_RAMP_RC1,
    OVERRIDE_RAMP_OFF_WAIT_TIME,
    /* Rx: Set AGC reference level to 0x26 (default: 0x2E) */
    RFC_HW_REG_OVERRIDE(0x609C, 0x0026),
    OVERRIDE_RSSI_OFFSET_4DB,
    /* Rx: Set anti-aliasing filter bandwidth to 0xA (in ADI0, set
     * IFAMPCTL3[7:4]=0xA) */
    RFC_ADI_HALFREG_OVERRIDE(0, 61, 0xF, 0xA),
    OVERRIDE_LNA_IB_BOOST,
    RFC_END_OVERRIDE,
};
/** @} */

/**
 * @brief   Largest number of mode specific overrides, without the end marker
 */
#define OVERRIDES_MODE_MAX              (15U)

/**
 * @brief   Override list given to the RF Core: common entries, entries of the
 *          current mode, #OVERRIDE_PACTL and the end marker
 */
static uint32_t _overrides[OVERRIDES_COMMON_NUMOF + OVERRIDES_MODE_MAX +
                           OVERRIDE_PACTL_NUMOF + OVERRIDES_PA_NUMOF + 1];

/** The 20 dBm PA is selected, with this setting from the table of the board */
static bool _pa20;
static uint32_t _pa20_val;

/**
 * @brief   MR-FSK mode: the fields of CMD_PROP_RADIO_DIV_SETUP that change
 *          with the symbol rate and the modulation index
 */
typedef struct {
    ieee802154_mr_fsk_srate_t srate;    /**< symbol rate */
    uint8_t mod_idx;                    /**< modulation index, in 1/64 */
    uint16_t deviation;                 /**< deviation, in steps of 250 Hz */
    uint32_t rate_word;                 /**< symbol rate word, prescaler 15 */
    uint8_t rx_bw;                      /**< receiver bandwidth setting */
    uint16_t int_freq;                  /**< intermediate frequency in MHz, signed
                                             4.12 fixed point, 0x8000: default */
    uint8_t preamble_len;               /**< preamble length in bytes */
    uint16_t turnaround_us;             /**< time from the end of a received
                                             frame to the first bit of a frame
                                             sent in reply without waiting */
    const uint32_t *overrides;          /**< mode specific overrides */
} fsk_mode_t;

/**
 * @brief   MR-FSK modes, from the 2-GFSK IEEE 802.15.4 and Wi-SUN settings of
 *          [RFCFG] that exist for both the CC1312R and the CC1352P
 *
 * - The deviation is `mod_idx * symbol rate / 2`. The symbol rate is
 *   `rate_word * 48 MHz / (15 * 2^21)` ([TRM] equation 15).
 * - The receiver bandwidth settings are listed in [TRM] Table 25-183.
 * - Each setting has its own intermediate frequency (`intFreq`), from 225 to
 *   600 kHz; only the IEEE 802.15.4 one of 50 kbit/s uses the default of the
 *   radio. TX uses the same intermediate frequency if supported, otherwise 0
 *   (driverlib rf_prop_cmd.h).
 * - Every mode uses #SYNC_WORD with 24 bits and the IEEE 802.15.4g whitener
 *   and CRC (`whitenMode` 7).
 *
 * `turnaround_us` is not from TI. It is what the RF Core and this driver need
 * between the end of a received frame and the start of the preamble of the
 * next transmission: the interrupt of the received frame, the end of the
 * receive command and the start of the transmit command. Measured on a
 * CC1352P1 with a second one as a sniffer, from the timestamps the RF Core
 * gives to the received frames; it grows with the symbol period and repeats
 * within 5 us. The driver subtracts it from the 1 ms the standard asks for
 * before an ACK, see cc26x2_cc13x2_rf_ack_delay_us().
 *
 * [RFCFG] has no 2-GFSK setting for 400 kbit/s, nor for 150 kbit/s and
 * 300 kbit/s with modulation index 1.0, and FEC needs an MCE patch
 * ([TRM] 25.10.5.2.1).
 */
static const fsk_mode_t _modes[] = {
    {   /* setting_tc120: 12.5 kHz deviation, 68 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_50K, .mod_idx = 32,
        .deviation = 50, .rate_word = 32768, .rx_bw = 80, .preamble_len = 4,
        .int_freq = 0x0399, .turnaround_us = 818,
        .overrides = _overrides_50k_h05,
    },
    {   /* setting_tc106_154g: 25 kHz deviation, 100 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_50K, .mod_idx = 64,
        .deviation = 100, .rate_word = 32768, .rx_bw = 82, .preamble_len = 7,
        .int_freq = 0x8000, .turnaround_us = 819,
        .overrides = _overrides_50k_h1,
    },
    {   /* setting_tc152_154g: 25 kHz deviation, 137 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_100K, .mod_idx = 32,
        .deviation = 100, .rate_word = 65536, .rx_bw = 84, .preamble_len = 7,
        .int_freq = 0x0599, .turnaround_us = 510,
        .overrides = _overrides_100k_h05,
    },
    {   /* setting_tc137: 50 kHz deviation, 196 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_100K, .mod_idx = 64,
        .deviation = 200, .rate_word = 65536, .rx_bw = 86, .preamble_len = 4,
        .int_freq = 0x08CC, .turnaround_us = 509,
        .overrides = _overrides_100k_h1,
    },
    {   /* setting_tc142: 37.5 kHz deviation, 273 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_150K, .mod_idx = 32,
        .deviation = 150, .rate_word = 98304, .rx_bw = 88, .preamble_len = 4,
        .int_freq = 0x08CC, .turnaround_us = 401,
        .overrides = _overrides_150k_h05,
    },
    {   /* setting_tc146_154g: 50 kHz deviation, 311 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_200K, .mod_idx = 32,
        .deviation = 200, .rate_word = 131072, .rx_bw = 89, .preamble_len = 7,
        .int_freq = 0x0999, .turnaround_us = 355,
        .overrides = _overrides_200k_h05,
    },
    {   /* setting_tc151: 100 kHz deviation, 273 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_200K, .mod_idx = 64,
        .deviation = 400, .rate_word = 131072, .rx_bw = 88, .preamble_len = 4,
        .int_freq = 0x0733, .turnaround_us = 356,
        .overrides = _overrides_200k_h1,
    },
    {   /* setting_tc154: 75 kHz deviation, 285 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_300K, .mod_idx = 32,
        .deviation = 300, .rate_word = 196608, .rx_bw = 92, .preamble_len = 4,
        .int_freq = 0x0666, .turnaround_us = 304,
        .overrides = _overrides_300k_h05,
    },
};

/**
 * @brief   Mode set up at power-on: 200 kbit/s, modulation index 1.0, the
 *          default of the stack (CONFIG_IEEE802154_MR_FSK_DEFAULT_SRATE and
 *          CONFIG_IEEE802154_MR_FSK_DEFAULT_MOD_IDX) and of RIOT's AT86RF215
 *          driver
 */
#define MODE_DEFAULT                    (6U)

/** Mode of the current setup command */
static const fsk_mode_t *_mode = &_modes[MODE_DEFAULT];

/**
 * CMD_SYNC_START_RAT
 *
 * This commands starts the Radio Timer, needed to run time-dependant radio
 * operations, and to chain commands. We only use the parameter `rat0` which
 * is the previous RAT offset when it was stopped using CMD_SYNC_STOP_RAT,
 * if no previous offset is present, 0 is fine.
 *
 * [TRM] 25.3.3.1.10 and 25.2.4:
 *
 * - The RAT must run before any command that uses the receiver or the
 *   transmitter, or that has a delayed start.
 * - The first start after a system boot should use CMD_START_RAT. This
 *   command with a fixed `rat0` such as 0 is the documented alternative; the
 *   RAT then starts from an arbitrary value.
 * - The start trigger must be TRIG_NOW, because the RAT is not running yet.
 * - AON_RTC:CTL.RTC_UPD_EN must be 1 and the system must run from XOSC_HF,
 *   see cc26x2_cc13x2_rfc_finish_on().
 *
 * `rat0` is 0 the first time and afterwards the value CMD_SYNC_STOP_RAT
 * returned when the radio was switched off, see cc26x2_cc13x2_rf_off().
 */
rfc_cmd_sync_start_rat_t rf_cmd_sync_start_rat =
{
    .command_no = RFC_CMD_SYNC_START_RAT,
    .status = RFC_IDLE,
    .next_op = NULL, /* set by us */
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0,
    },
    .condition = {
        .rule = RFC_COND_NEVER,
        .skip_no = 0,
    },
    .__dummy0 = 0,
    .rat0 = 0, /* set by us */
};

/**
 * CMD_PROP_RADIO_DIV_SETUP
 *
 * This is the "setup" command for the radio, sets the necessary parameters for
 * TX/RX and some configuration of the modulation, whitening, packet format,
 * symbol rate, preamble configuration, receiver bandwidth, intermediate
 * frequency, center frequency.
 *
 * The fields are described in [TRM] Table 25-175 and section 25.10.5.2:
 *
 * - The deviation, the symbol rate, the receiver bandwidth, the intermediate
 *   frequency, the preamble length and the override list depend on the
 *   MR-FSK mode, see @ref _modes.
 * - `msb_first = 1` and `whiten_mode` 4 to 7 select the IEEE 802.15.4g mode
 *   ([TRM] 25.10.5.2.1). In mode 7 the radio CPU reads the PHR of each frame:
 *   bit 12 selects the 16-bit CRC instead of the 32-bit one and bit 11
 *   enables the whitening.
 * - `center_freq` and `lo_divider` depend on the frequency band, see
 *   #CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ.
 * - `front_end_mode = 0` and `bias_mode = 1`: differential RF port and
 *   external bias, as on both LaunchPads ([RFCFG] target files).
 *
 * Every field has the value of [RFCFG] for the selected mode, with these
 * exceptions:
 *
 * - `tx_power`: [RFCFG] has 0x9F3F for the CC1312R and, for the CC1352P,
 *   0xFFFF with the `_PA` variant of the command. Here it is the first entry
 *   of the PA table of the board, and cc26x2_cc13x2_rf_set_tx_pwr() changes it
 *   afterwards.
 * - `center_freq` and `lo_divider`: [RFCFG] is written for 915 MHz (and for
 *   868 MHz in other settings of the same family), always with divider 5.
 *   Other bands only change these two fields: the override lists of [RFCFG]
 *   are the same for every divider.
 */
rfc_cmd_prop_radio_div_setup_t rf_cmd_prop_radio_div_setup =
{
    .command_no = RFC_CMD_PROP_RADIO_DIV_SETUP,
    .status = RFC_IDLE,
    .next_op = NULL, /* set by us */
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .condition = {
        .rule = RFC_COND_NEVER,
        .skip_no = 0
    },
    .modulation = {
        .mod_type = RFC_MOD_TYPE_GFSK,
        .deviation = 0, /* set by _load_mode() */
        .deviation_step_sz = RFC_DEV_STEP_SZ_250_HZ
    },
    .symbol_rate = {
        .prescale = 15,
        .rate_word = 0, /* set by _load_mode() */
        .decim_mode = RFC_DECIM_MODE_AUTO
    },
    .rx_bw = 0, /* set by _load_mode() */
    .pream_conf = {
        .pream_bytes = 0, /* set by _load_mode() */
        .pream_mode = RFC_PREAM_MODE_0_FIRST,
    },
    .format_conf = {
        .sw_bits = SYNC_WORD_BITS,
        .bit_reversal = 0,
        .msb_first = 1,
        .fec_mode = 0,
        .whiten_mode = RFC_WHITEN_MODE_IEEE802154G_CRC
    },
    .config = {
        .front_end_mode = 0,
        .bias_mode = RFC_BIAS_MODE_EXTERNAL,
        .analog_cfg_mode = 0,
        .no_fs_powerup = 0
    },
    .tx_power = 0x04C0,
    .reg_override = _overrides,
    .center_freq = CC26X2_CC13X2_RF_CENTER_FREQ,
    .int_freq = 0x8000,
    .lo_divider = CC26X2_CC13X2_RF_LO_DIVIDER
};

/**
 * CMD_FS
 *
 * The Frequency Sinthesizer command, sets the channel frequency.
 *
 * [TRM] 25.3.3.1.5: the synthesizer is tuned to
 * (`frequency` + `fract_freq` / 65536) MHz. `txmode = 0` starts it in RX mode;
 * the RX and TX commands change the mode on their own. The command ends with
 * ERROR_PAR for an illegal frequency or divider and with ERROR_NO_SETUP if
 * the radio is not set up yet, so it must come after the setup command.
 *
 * CMD_FS is a radio operation command: while another one is running, such as
 * CMD_PROP_RX_ADV, it is rejected with SchedulingError ([TRM] Table 25-2).
 */
rfc_cmd_fs_t rf_cmd_fs =
{
    .command_no = RFC_CMD_FS,
    .status = RFC_IDLE,
    .next_op = NULL, /* set by us */
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .condition = {
        .rule = RFC_COND_NEVER,
        .skip_no = 0
    },
    .frequency = 0, /* set by us */
    .fract_freq = 0, /* set by us */
    .synth_conf = {
        .txmode = 0,
        .ref_freq = 0
    },
    .__dummy0 = 0,
    .__dummy1 = 0,
    .__dummy2 = 0,
    .__dummy3 = 0
};

/**
 * CMD_PROP_TX_ADV
 *
 * Advanced proprietary transmission. Despite the "proprietary" name, it's
 * compatible with IEEE 802.15.4g. Fields set at runtime are: pkt and pkt_len.
 *
 * The fields are described in [TRM] Table 25-171 and section 25.10.5.3.2:
 *
 * - [TRM] 25.10.5.2.1 requires `crc_inc_sw = 0` and `crc_inc_hdr = 0` in the
 *   IEEE 802.15.4g mode. `use_crc = 1` makes the radio append the FCS.
 * - `num_hdr_bits = 16`: the PHR. It is read from the first two bytes of the
 *   buffer in little-endian order and sent MSB first, see
 *   cc26x2_cc13x2_rf_write().
 * - `pkt_len` counts the PHR and the PSDU without the FCS.
 * - `pre_trigger` TRIG_NOW: the preamble of the setup command is sent once,
 *   followed by the sync word.
 * - `fs_off = 0` keeps the synthesizer running after the frame, so RX can be
 *   restarted on the same frequency without a new CMD_FS.
 *
 * End status ([TRM] Table 25-184): PROP_DONE_OK when the frame was sent,
 * PROP_ERROR_NO_FS without a previous CMD_FS, PROP_ERROR_TXUNF on a TX
 * underflow.
 */
rfc_cmd_prop_tx_adv_t rf_cmd_prop_tx_adv =
{
    .command_no = RFC_CMD_PROP_TX_ADV,
    .status = RFC_IDLE,
    .next_op = NULL, /* set by us */
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .condition = {
        .rule = RFC_COND_NEVER,
        .skip_no = 0
    },
    .pkt_conf = {
        .fs_off = 0,
        .use_crc = 1,
        .crc_inc_sw = 0,
        .crc_inc_hdr = 0
    },
    .num_hdr_bits = IEEE802154_PHY_MR_FSK_PHR_LEN * 8,
    .pkt_len = 0, /*< set by us */
    .start_conf = {
        .ext_tx_trig = 0,
        .input_mode = 0,
        .source = 0
    },
    .pre_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .pre_time = 0,
    .sync_word = SYNC_WORD,
    .pkt = 0 /* set by us */
};

/**
 * CMD_PROP_RX_ADV
 *
 * Advanced proprietary RX. Sets the radio into RX mode, it keeps putting
 * the RX queue. This command does not finish unless it's aborted/stopped.
 *
 * The fields are described in [TRM] Table 25-173 and section 25.10.5.4:
 *
 * - [TRM] 25.10.5.2.1 fixes these values for the IEEE 802.15.4g mode:
 *   `crc_inc_sw = 0`, `crc_inc_hdr = 0`, `num_hdr_bits = 16`, `len_pos = 0`,
 *   `num_len_bits = 11` and `len_offset = -4`. They are the values used here
 *   and in [RFCFG].
 * - `repeat_ok = 1` and `repeat_nok = 1`: go back to sync search after every
 *   frame, with or without CRC error. With `end_trigger` TRIG_NEVER the
 *   command only ends on CMD_STOP, CMD_ABORT or an error
 *   ([TRM] Table 25-186).
 * - `rx_conf` ([TRM] Table 25-179): frames with a CRC error are flushed and
 *   raise no RX_ENTRY_DONE. The PHR and the FCS are not stored and one RSSI
 *   byte is appended. An entry element is then, as in [TRM] Figure 25-11, a
 *   2-byte length, the PSDU without FCS and the RSSI byte.
 * - `addr_size = 0`: no address filtering in the RF Core, `addr` must be
 *   NULL. The filtering is done in cc26x2_cc13x2_rf_l2_filter().
 * - `sync_word1 = 0`: no alternative sync word.
 *
 * If no entry of the queue is free when a frame starts, the command ends
 * with PROP_ERROR_RXBUF ([TRM] Table 25-186), see
 * cc26x2_cc13x2_rf_rx_start().
 */
rfc_cmd_prop_rx_adv_t rf_cmd_prop_rx_adv =
{
    .command_no = RFC_CMD_PROP_RX_ADV,
    .status = RFC_IDLE,
    .next_op = NULL, /* set by us */
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .condition = {
        .rule = RFC_COND_NEVER,
        .skip_no = 0
    },
    .pkt_conf = {
        .fs_off = 0,
        .repeat_ok = 1,
        .repeat_nok = 1,
        .use_crc = 1,
        .crc_inc_sw = 0,
        .crc_inc_hdr = 0,
        .end_type = 0,
        .filter_op = 1
    },
    .rx_conf = {
        .auto_flush_ignored = 1,
        .auto_flush_crc_err = 1,
        .include_hdr = 0,
        .include_crc = 0,
        .append_rssi = 1,
        .append_timestamp = 0,
        .append_status = 0
    },
    .sync_word0 = SYNC_WORD,
    .sync_word1 = 0,
    .max_pkt_len = IEEE802154G_FRAME_LEN_MAX,
    .hdr_conf = {
        .num_hdr_bits = IEEE802154_PHY_MR_FSK_PHR_LEN * 8,
        .len_pos = 0,
        .num_len_bits = 11
    },
    .addr_conf = {
        .addr_type = 0,
        .addr_size = 0,
        .addr_pos = 0,
        .num_addr = 0
    },
    .len_offset = -4,
    .end_trigger = {
        .type = RFC_TRIG_NEVER,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .end_time = 0,
    .addr = 0, /* unused */
    .queue = 0, /* set by us */
    .output = 0 /* set by us */
};

/**
 * Carrier sense command, used to perform CCA
 *
 * The carrier sense fields are described in [TRM] Table 25-178 and section
 * 25.10.5.5:
 *
 * - Only the RSSI is used (`ena_rssi = 1`, `ena_corr = 0`). The channel is
 *   IDLE after `num_rssi_idle` consecutive measurements below `rssi_thr`,
 *   BUSY after `num_rssi_busy` consecutive measurements above it and INVALID
 *   otherwise.
 * - `busy_op = 1` and `idle_op = 1` end the command on the first BUSY or IDLE
 *   state, with PROP_DONE_BUSY or PROP_DONE_IDLE ([TRM] Table 25-188).
 * - There is no end trigger. `timeout_res` only matters for an end trigger,
 *   so the command runs for as long as the state is INVALID.
 * - `off_idle = 0` and `off_busy = 0` keep the synthesizer running when the
 *   command ends.
 *
 * As the RX and TX commands, it needs the radio set up and the synthesizer
 * programmed with CMD_FS.
 */
rfc_cmd_prop_cs_t rf_cmd_prop_cs =
{
    .command_no = RFC_CMD_PROP_CS,
    .status = RFC_IDLE,
    .next_op = NULL,
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .condition = {
        .rule = RFC_COND_NEVER,
        .skip_no = 0
    },
    .fs_conf = {
        /* we don't want to turn off the Frequency Synthesizer after a
         * carrier sense command */
        .off_idle = 0,
        .off_busy = 0,
    },
    .__dummy0 = 0,
    .conf = {
        .ena_rssi = 1, /* use RSSI as the measurement parameter */
        .ena_corr = 0,
        .operation = 0,
        /* stop either on Busy or Idle */
        .busy_op = 1,
        .idle_op = 1,
        .timeout_res = 0,
    },
    .rssi_thr = -80, /* SHOULD be set by us, through hal or netdev */
    .num_rssi_idle = 1, /* consecutive RSSI measurements to mark channel as
                           Idle */
    .num_rssi_busy = 1, /* consecutive RSSI measurements to mark channel as
                           Busy */
    .corr_period = 0,
    .corr_config = {
        .num_corr_inv = 0,
        .num_corr_busy = 0,
    },
    /* end trigger is not used, as Carrier Sense will stop
     * on an Idle/Busy channel. */
    .end_trigger = {
        .type = RFC_TRIG_NEVER,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .end_time = 0,
};

/** Command to clear the RX queue */
rfc_cmd_clear_rx_t rf_cmd_clear_rx =
{
    .command_no = RFC_CMD_CLEAR_RX,
    .__dummy0 = 0,
    .queue = NULL /* set by us */
};

/**
 * @brief   CMD_SYNC_STOP_RAT ([TRM] 25.3.3.1.9), last command before the RF
 *          Core is powered off
 *
 * Stops the radio timer at an RTC tick and returns in `rat0` the value the
 * RAT would have had when the RTC was zero. Given to CMD_SYNC_START_RAT at
 * the next power-up it keeps the time base of the RAT consistent
 * ([TRM] 25.2.4.3). It needs AON_RTC:CTL.RTC_UPD_EN, set by
 * cc26x2_cc13x2_rfc_finish_on().
 */
static rfc_cmd_sync_stop_rat_t _cmd_sync_stop_rat =
{
    .command_no = RFC_CMD_SYNC_STOP_RAT,
    .status = RFC_IDLE,
    .next_op = NULL,
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0,
    },
    .condition = {
        .rule = RFC_COND_NEVER,
        .skip_no = 0,
    },
    .__dummy0 = 0,
    .rat0 = 0,
};

/**
 * @brief   CMD_FS_POWERDOWN ([TRM] 25.3.3.1.4), chained with
 *          #_cmd_sync_stop_rat
 *
 * Stops the synthesizer and switches the analog modules off. [TRM]: it "must
 * always be run before the radio is powered down". [SDK] does it with direct
 * register writes instead (RFCSynthPowerDown() in driverlib rfc.c).
 */
static rfc_cmd_fs_powerdown_t _cmd_fs_powerdown =
{
    .command_no = RFC_CMD_FS_POWERDOWN,
    .status = RFC_IDLE,
    .next_op = &_cmd_sync_stop_rat,
    .start_time = 0,
    .start_trigger = {
        .type = RFC_TRIG_NOW,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0,
    },
    .condition = {
        .rule = RFC_COND_ALWAYS,
        .skip_no = 0,
    },
};

/** Set TX power */
rfc_cmd_set_tx_power_t rf_cmd_set_tx_power =
{
    .command_no = RFC_CMD_SET_TX_POWER,
    .tx_power = 0,
};

#if HAVE_PA20
/** Set TX power of the 20 dBm PA */
static rfc_cmd_set_tx20_power_t _cmd_set_tx20_power =
{
    .command_no = RFC_CMD_SET_TX20_POWER,
    .tx20_power = 0,
};
#endif

/**
 * @brief   Upper bound of the polls of a command status
 *
 * A setup command followed by CMD_FS takes well below a millisecond. The
 * bound, several tens of milliseconds at 48 MHz, only keeps a misbehaving
 * RF Core from blocking the caller forever.
 */
#define CMD_POLL_MAX                    (1000000UL)

/*
 * Waits until a radio operation command has a final status. [SDK] does the
 * same for CMD_SYNC_STOP_RAT in RF_corePowerDown(). The status field is
 * written before COMMAND_DONE and LAST_COMMAND_DONE are raised
 * ([TRM] 25.3.2.5), so it does not depend on a flag that an earlier aborted
 * command may still raise.
 */
static int _wait_cmd(const volatile uint16_t *status)
{
    for (unsigned long i = 0; i < CMD_POLL_MAX; i++) {
        if (cc26x2_cc13x2_rfc_cmd_has_ended(*status)) {
            return 0;
        }
    }

    return -ETIMEDOUT;
}

/*
 * Writes a mode into CMD_PROP_RADIO_DIV_SETUP and builds its override list.
 * The command must not be running.
 */
static void _load_mode(const fsk_mode_t *mode)
{
    unsigned n = 0;

    rf_cmd_prop_radio_div_setup.modulation.deviation = mode->deviation;
    rf_cmd_prop_radio_div_setup.symbol_rate.rate_word = mode->rate_word;
    rf_cmd_prop_radio_div_setup.rx_bw = mode->rx_bw;
    rf_cmd_prop_radio_div_setup.int_freq = mode->int_freq;
    rf_cmd_prop_radio_div_setup.pream_conf.pream_bytes = mode->preamble_len;

    _overrides[n++] = OVERRIDE_DCDCCTL5_REG;
    _overrides[n++] = OVERRIDE_RF_FSCA_0;
    _overrides[n++] = OVERRIDE_RF_FSCA_1;
    for (const uint32_t *o = mode->overrides; *o != RFC_END_OVERRIDE; o++) {
        assert(n < OVERRIDES_COMMON_NUMOF + OVERRIDES_MODE_MAX);
        _overrides[n++] = *o;
    }
#if IS_USED(CONFIG_CC26XX_CC13XX_FORCE_VDDR_HH)
    _overrides[n++] = OVERRIDE_PACTL;
#endif
    if (HAVE_PA20) {
        if (_pa20) {
            /* [TRM] 25.3.3.1.2: with the TX power field set to 0xFFFF the
             * 20 dBm PA power entry is mandatory. The entries below come
             * after the ones of the mode and replace its ramp-off wait. */
            _overrides[n++] = RFC_TX20_POWER_OVERRIDE(_pa20_val);
            _overrides[n++] = OVERRIDE_ANADIV_PA20;
            _overrides[n++] = OVERRIDE_RAMP_OFF_WAIT_TIME_PA20;
            _overrides[n++] = OVERRIDE_TXRX_PIN_PA20;
        }
        else {
            _overrides[n++] = OVERRIDE_TXRX_PIN_STD;
        }
    }
    _overrides[n] = RFC_END_OVERRIDE;

    _mode = mode;
}

/*
 * Prepares the RX data queue and the setup command chain. Nothing is sent to
 * the RF Core here.
 *
 * RX data queue ([TRM] 25.3.2.7, Tables 25-10 and 25-11):
 *
 * - Each entry is a "general" data entry (type 0) with a 2-byte element
 *   length (`lensz = 2`). `length` is the storage after the 8-byte entry
 *   header.
 * - `status` is written as Pending (0) before the entry is handed to the
 *   radio CPU, which moves it to Active, Busy and Finished. The driver sets
 *   it back to Pending after reading the frame.
 * - The entries form a ring. `last_entry = NULL` with a non-NULL `curr_entry`
 *   means that no further entry may be appended to the queue.
 *
 * Setup chain, started by cc26x2_cc13x2_rf_confirm_on():
 *
 *     CMD_SYNC_START_RAT -> CMD_PROP_RADIO_DIV_SETUP -> CMD_FS
 *
 * [SDK] builds the same three commands in another order in
 * RF_fsmSetupState(): setup, CMD_SYNC_START_RAT, CMD_FS. Both orders respect
 * what [TRM] asks for: the setup before CMD_FS, and the RAT running before
 * any RX or TX command.
 */
void cc26x2_cc13x2_rf_internal_init(void)
{
    _pa20 = false;
    CC26X2_CC13X2_RF_FRONTEND_SETUP(false);

    /* Initialize data entries as general, with a length field of 2 bytes
     * (uint16_t) and the correct RX buffer size */
    for (unsigned i = 0; i < CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF; i++) {
        /* verify that the compiler put our RX buffers on aligned memory */
        assert(((uintptr_t)&_rx_buf[i] & 0x3) == 0);

        /* the initial bytes of an RX buffer is the rfc_data_entry_t structure */
        rfc_data_entry_t *entry = &_rx_buf[i].entry;

        entry->status = RFC_DATA_ENTRY_PENDING;
        entry->config.type = RFC_DATA_ENTRY_TYPE_GEN;
        entry->config.lensz = sizeof(uint16_t);
        entry->length = RX_BUF_LEN - sizeof(rfc_data_entry_t);

        if ((i + 1) == CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF) {
            /* Point to the first entry if this is the last */
            entry->next_entry = &_rx_buf[0].entry;
        }
        else {
            entry->next_entry = &_rx_buf[i + 1].entry;
        }
    }

    /* Put a pointer to the receive buffers on the received queue, last entry is
     * null, this to make a circular buffer */
    _rx_queue.curr_entry = &_rx_buf[0].entry;
    _rx_queue.last_entry = NULL;

    rf_cmd_prop_rx_adv.queue = &_rx_queue;
    rf_cmd_prop_rx_adv.output = &_rx_output;
    rf_cmd_clear_rx.queue = &_rx_queue;

    uint64_t default_freq = cc26x2_cc13x2_rf_chan_freq(CC26X2_CC13X2_RF_CHAN_MIN);
    cc26x2_cc13x2_rf_freq_convert(default_freq,
                                  &rf_cmd_fs.frequency,
                                  &rf_cmd_fs.fract_freq);

    cc26x2_cc13x2_rf_pa_t *pa_val = &cc26x2_cc13x2_rf_patable[0];

    _load_mode(&_modes[MODE_DEFAULT]);
    rf_cmd_prop_radio_div_setup.condition.rule = RFC_COND_ALWAYS;
    rf_cmd_prop_radio_div_setup.condition.skip_no = 0;
    rf_cmd_prop_radio_div_setup.next_op = (rfc_op_t *)&rf_cmd_fs;
    rf_cmd_prop_radio_div_setup.tx_power = pa_val->val;

    rf_cmd_sync_start_rat.next_op = &rf_cmd_prop_radio_div_setup;
    rf_cmd_sync_start_rat.condition.rule = RFC_COND_ALWAYS;
    /* the RAT starts from 0 the first time, cc26x2_cc13x2_rf_off() stores
     * the offset to resume from */
    rf_cmd_sync_start_rat.rat0 = 0;

    memset(_short_addr, 0, IEEE802154_SHORT_ADDRESS_LEN);
    memset(_long_addr, 0, IEEE802154_LONG_ADDRESS_LEN);
    _pan_id = 0;
    DEBUG("[cc26x2_cc13x2_rf]: radio initialised\n");
}

void cc26x2_cc13x2_rf_request_on(void)
{
    cc26x2_cc13x2_rfc_request_on();
    _poweron_state = STATE_RFC_POWER_ON;
}

/*
 * Power-on state machine, polled by the HAL:
 *
 * - STATE_RFC_POWER_ON: wait for the CPE boot. Then switch to XOSC_HF, patch
 *   the CPE and submit the setup chain. CMDSTA Done only tells that the chain
 *   was accepted ([TRM] Table 25-2).
 * - STATE_RFC_SETUP: wait for LAST_COMMAND_DONE, raised after the last
 *   command of the chain ([TRM] 25.3.2.5).
 *
 * If a command of the chain ends with an error its result is ABORT, the rest
 * of the chain is not run and LAST_COMMAND_DONE is raised all the same
 * ([TRM] 25.3.2.5.2). So the status field of each command is checked before
 * the radio is reported as on.
 */
int cc26x2_cc13x2_rf_confirm_on(void)
{
    uint32_t cmdsta;

    switch (_poweron_state) {
        case STATE_RFC_POWER_ON:
            if (cc26x2_cc13x2_rfc_confirm_on() != 0) {
                return -EAGAIN;
            }
            cc26x2_cc13x2_rfc_finish_on(rf_patch_cpe_prop);

            /* a command must be submitted with its status set to IDLE
             * ([TRM] 25.3.2.2) */
            rf_cmd_sync_start_rat.status = RFC_IDLE;
            rf_cmd_prop_radio_div_setup.status = RFC_IDLE;
            rf_cmd_fs.status = RFC_IDLE;
            _rx_submitted = false;

            cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_sync_start_rat);
            if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
                DEBUG("[cc26x2_cc13x2_rf]: setup chain rejected, cmdsta=%08lx\n",
                      cmdsta);
                return -EIO;
            }
            _poweron_state = STATE_RFC_SETUP;
            return -EAGAIN;

        case STATE_RFC_SETUP:
            if (cc26x2_cc13x2_rfc_confirm_execute() != 0) {
                return -EAGAIN;
            }
            if ((rf_cmd_sync_start_rat.status != RFC_DONE_OK) ||
                (rf_cmd_prop_radio_div_setup.status != RFC_PROP_DONE_OK) ||
                (rf_cmd_fs.status != RFC_DONE_OK)) {
                DEBUG("[cc26x2_cc13x2_rf]: setup chain failed, status: RAT=%04x "
                      "setup=%04x FS=%04x\n", rf_cmd_sync_start_rat.status,
                      rf_cmd_prop_radio_div_setup.status, rf_cmd_fs.status);
                return -EIO;
            }
            _poweron_state = STATE_FINISHED;
            return 0;

        case STATE_FINISHED:
            break;

        default:
            /* off: cc26x2_cc13x2_rf_request_on() was not called */
            return -EIO;
    }

    return 0;
}

/*
 * Switches the radio off. Mode, channel and TX power stay in the command
 * structures, so the next cc26x2_cc13x2_rf_request_on() brings the radio
 * back as it was.
 *
 * 1. End the receive command, if it runs.
 * 2. CMD_FS_POWERDOWN, then CMD_SYNC_STOP_RAT, see their structures. The
 *    returned `rat0` is stored for the next CMD_SYNC_START_RAT.
 * 3. Power the RF Core off, see cc26x2_cc13x2_rfc_power_off().
 *
 * If the two commands fail the RF Core is powered off all the same, and the
 * RAT starts from the previous offset next time.
 */
void cc26x2_cc13x2_rf_off(void)
{
    if (_poweron_state == STATE_FINISHED) {
        uint32_t cmdsta;

        cc26x2_cc13x2_rf_rx_stop();

        _cmd_fs_powerdown.status = RFC_IDLE;
        _cmd_sync_stop_rat.status = RFC_IDLE;
        cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&_cmd_fs_powerdown);
        if (((cmdsta & 0xFF) == RFC_CMDSTA_DONE) &&
            (_wait_cmd(&_cmd_fs_powerdown.status) == 0) &&
            (_cmd_fs_powerdown.status == RFC_DONE_OK) &&
            (_wait_cmd(&_cmd_sync_stop_rat.status) == 0) &&
            (_cmd_sync_stop_rat.status == RFC_DONE_OK)) {
            rf_cmd_sync_start_rat.rat0 = _cmd_sync_stop_rat.rat0;
        }
        else {
            DEBUG("[cc26x2_cc13x2_rf]: power-down commands failed, cmdsta=%08lx "
                  "FS=%04x RAT=%04x\n", cmdsta, _cmd_fs_powerdown.status,
                  _cmd_sync_stop_rat.status);
        }
    }

    cc26x2_cc13x2_rfc_power_off();
    _rx_submitted = false;
    _poweron_state = STATE_OFF;
}

/*
 * Starts CMD_PROP_CS. The result is not available when this function
 * returns: the command ends later with LAST_COMMAND_DONE and
 * PROP_DONE_IDLE (0x3407) or PROP_DONE_BUSY (0x3408) in its status field
 * ([TRM] Table 25-188). The caller must have stopped RX before, as only one
 * radio operation command can run at a time ([TRM] 25.3.2.5).
 */
int cc26x2_cc13x2_rf_request_cca(void)
{
    uint32_t cmdsta;

    rf_cmd_prop_cs.status = RFC_IDLE;
    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_prop_cs);
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: CMD_PROP_CS failed with cmdsta=%08lx",
              cmdsta);
        return -EIO;
    }

    return 0;
}

/*
 * RX is on from the moment CMD_PROP_RX_ADV is accepted until its status
 * field gets a final value. The status alone is not enough: right after the
 * command is accepted it still reads IDLE, until the radio CPU starts the
 * command and writes PENDING or ACTIVE ([TRM] 25.3.2.2).
 */
static bool _rx_is_on(void)
{
    return _rx_submitted &&
           !cc26x2_cc13x2_rfc_cmd_has_ended(rf_cmd_prop_rx_adv.status);
}

/*
 * Starts CMD_PROP_RX_ADV:
 *
 * 1. If the previous RX ended with PROP_ERROR_RXBUF (0x3801, no free entry at
 *    the start of a frame), empty the queue with the immediate command
 *    CMD_CLEAR_RX. It sets every entry of the ring back to Pending and fails
 *    with QueueBusy if the first entry is Busy ([TRM] 25.3.4.4).
 * 2. Submit the RX command. CMDSTA SchedulingError (0x86) means that another
 *    radio operation command is still running; it is not affected by the
 *    rejected command ([TRM] Table 25-2). This function then returns -EBUSY
 *    and leaves the running command alone.
 *
 * The synthesizer must already be programmed: this command needs a previous
 * CMD_FS ([TRM] 25.10.5.4), from the setup chain or from
 * cc26x2_cc13x2_rf_change_chan().
 */
static int _rx_start(void)
{
    uint32_t cmdsta;

    if (_rx_is_on()) {
        return 0;
    }
    _rx_submitted = false;

    /* if netdev or IEEE 802.15.4g HAL don't clear up the buffers by calling
     * recv/indication_rx the buffer might fill up, this is the case for
     * netdev as when receiving packets too fast gnrc_netif might not handle
     * them as fast as we want, so in either case if these packets aren't handled
     * just drop them.
     *
     * TODO: what about re-generating the ISR event and return -EBUSY here to
     * avoid the loss of these packets? */
    if (rf_cmd_prop_rx_adv.status == RFC_PROP_ERROR_RXBUF) {
        DEBUG("[cc26x2_cc13x2_rf]: clearing RX queue\n");
        cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_clear_rx);
        if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
            DEBUG("[cc26x2_cc13x2_rf]: CMD_CLEAR_RX failed, cmdsta=%08lx\n",
                  cmdsta);
            return -EIO;
        }
    }

    /* RX is not running, so the command structure may be written */
    rf_cmd_prop_rx_adv.status = RFC_IDLE;
    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_prop_rx_adv);
    if ((cmdsta & 0xFF) == RFC_CMDSTA_SCHEDULINGERROR) {
        DEBUG("[cc26x2_cc13x2_rf]: RX rejected, another command is running\n");
        return -EBUSY;
    }
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: RX rejected, cmdsta=%08lx\n", cmdsta);
        return -EIO;
    }

    _rx_submitted = true;

    return 0;
}

int cc26x2_cc13x2_rf_rx_start(void)
{
    /* RX is started and stopped from threads and from interrupts: the check,
     * the submission and the update of _rx_submitted must not be split */
    unsigned key = irq_disable();
    int res = _rx_start();
    irq_restore(key);

    return res;
}

/*
 * Ends CMD_PROP_RX_ADV with CMD_ABORT and waits until the command has ended,
 * with PROP_DONE_ABORT (0x3405, [TRM] Table 25-186). A frame being received
 * at that moment is dropped: an aborted reception is removed from the RX
 * queue right away ([TRM] 25.10.4.1).
 *
 * After this function returns the radio CPU is idle, so a new radio operation
 * command can be submitted.
 */
void cc26x2_cc13x2_rf_rx_stop(void)
{
    unsigned key = irq_disable();

    if (_rx_submitted) {
        int res = cc26x2_cc13x2_rfc_abort_cmd(&rf_cmd_prop_rx_adv.status);
        if (res == 0) {
            _rx_submitted = false;
        }
        else {
            /* RX is still running and stays marked as such */
            DEBUG("[cc26x2_cc13x2_rf]: could not stop RX (%d)\n", res);
        }
    }

    irq_restore(key);
}

/*
 * [TRM] 25.10.5.2.1, note: "The IEEE 802.15.4g PHY header must be presented
 * MSB first to the RF Core. In IEEE 802.15.4g specification, the payload part
 * is LSb first, however the payload length info in physical layer header
 * (PHR) is MSb first. This means that the payload needs to be flipped in the
 * System CPU. This can be achieved with the System CPU assembly instruction
 * RBIT."
 *
 * Checked against an AT86RF215: without the reversal its frames arrive with
 * a correct CRC, which is computed over the bit stream, and every byte
 * bit-reversed.
 */
void cc26x2_cc13x2_rf_copy_psdu(uint8_t *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        /* RBIT reverses the 32 bits of a register */
        dst[i] = (uint8_t)(__RBIT(src[i]) >> 24);
    }
}

rfc_data_entry_general_t *cc26x2_cc13x2_rf_recv(void)
{
    return (rfc_data_entry_general_t *)cc26x2_cc13x2_rfc_queue_recv(&_rx_queue);
}

int cc26x2_cc13x2_rf_write(const iolist_t *iolist)
{
    const size_t max_psdu_len = TX_BUF_LEN - IEEE802154_PHY_MR_FSK_PHR_LEN;
    const size_t fcs_len = sizeof(uint16_t);
    size_t total_len;
    size_t psdu_len = 0;

    /* PHY header size reserved (two bytes), after the PHR the
     * PSDU starts */
    uint8_t *buf_pos = _tx_buf + IEEE802154_PHY_MR_FSK_PHR_LEN;
    for (const iolist_t *iol = iolist; iol; iol = iol->iol_next) {
        psdu_len += iol->iol_len;
        if (psdu_len > max_psdu_len) {
            return -EOVERFLOW;
        }

        cc26x2_cc13x2_rf_copy_psdu(buf_pos, iol->iol_base, iol->iol_len);
        buf_pos += iol->iol_len;
    }

    /* Length in MR-FSK PHR. Includes the PSDU + FCS */
    total_len = psdu_len + fcs_len;
    if (total_len > IEEE802154G_FRAME_LEN_MAX) {
        return -EOVERFLOW;
    }

    /* Prepare the MR-FSK PHR
     * - No mode switching.
     * - Total length: PSDU + FCS length
     * - FCS type (CRC-16)
     *
     * Total length = len (payload) + CRC length
     *
     * The Radio will flip the bits around, so _tx_buf[0] must have the
     * length LSBs (PHR[15:8] and _tx_buf[1] will have PHR[7:0]
     *
     * This is the example of [TRM] 25.10.5.2.1 with the CRC-16 bit added.
     * The radio reads the 16-bit header from the buffer in little-endian
     * order and sends it MSB first ([TRM] 25.10.5.3.2), so _tx_buf[1] goes
     * out before _tx_buf[0]. With `whiten_mode = 7` the radio CPU reads
     * these header bits, counted from the LSB of the 16-bit header:
     *
     * - bit 11, 0x08 in _tx_buf[1]: whitening enabled,
     * - bit 12, 0x10 in _tx_buf[1]: 16-bit CRC instead of the 32-bit one,
     * - bit 15: header-only frame, never set here.
     *
     * Only the PHR is given in this form. The PSDU bytes were bit-reversed
     * when they were copied into the buffer, see
     * cc26x2_cc13x2_rf_copy_psdu(). */
    _tx_buf[0] = (((total_len) >> 0) & 0xFF);
    _tx_buf[1] = (((total_len) >> 8) & 0xFF) + 0x08 + 0x10;

    /* The length of the buffer used to transmit is the PHR + PSDU,
     * it does not include the FCS size as the radio writes it on
     * it's own depending on the configured FCS, which is CRC-16 in
     * our case */
    rf_cmd_prop_tx_adv.pkt_len = IEEE802154_PHY_MR_FSK_PHR_LEN + psdu_len;
    rf_cmd_prop_tx_adv.pkt = _tx_buf;
    rf_cmd_prop_tx_adv.status = RFC_IDLE;

    return 0;
}

/*
 * Starts CMD_PROP_TX_ADV with the frame prepared by cc26x2_cc13x2_rf_write().
 * CMDSTA Done means that the command was accepted. The frame has been sent
 * when LAST_COMMAND_DONE is raised and the status field is PROP_DONE_OK
 * (0x3400, [TRM] Table 25-184).
 *
 * SchedulingError means that another radio operation command is still
 * running. The caller must stop RX with cc26x2_cc13x2_rf_rx_stop() before.
 */
int cc26x2_cc13x2_rf_request_transmit(void)
{
    uint32_t cmdsta;

    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_prop_tx_adv);
    switch (cmdsta & 0xFF) {
        case RFC_CMDSTA_DONE:
            break;
        /* previous command, if any, has not been finished */
        case RFC_CMDSTA_SCHEDULINGERROR:
            return -EBUSY;
        default:
            DEBUG("[cc26x2_cc13x2_rf]: couldn't send TX command, cmdsta=%08lx\n",
                  cmdsta);
            return -EIO;
    }

    return 0;
}

/*
 * IEEE Std 802.15.4-2024, 6.6.3.3: the transmission of an Ack frame starts
 * AIFS after the last symbol of the frame it acknowledges, and AIFS is 1 ms
 * for the SUN PHYs.
 */
uint32_t cc26x2_cc13x2_rf_ack_delay_us(void)
{
    const uint32_t aifs_us = 1000;

    return (_mode->turnaround_us < aifs_us) ? (aifs_us - _mode->turnaround_us) : 0;
}

void cc26x2_cc13x2_rf_set_cca_threshold(int8_t threshold)
{
    rf_cmd_prop_cs.rssi_thr = threshold;
}

/*
 * Changes the MR-FSK mode: runs CMD_PROP_RADIO_DIV_SETUP again with the
 * fields of the new mode, chained with CMD_FS for the current channel, as the
 * setup command resets the synthesizer. The radio must be idle.
 *
 * `analog_cfg_mode` stays 0, which rewrites the analog configuration, as
 * required after a change of frequency band or front end
 * ([TRM] Table 25-175, driverlib rf_prop_cmd.h). The TX power of the setup
 * command is the last one set by cc26x2_cc13x2_rf_set_tx_pwr().
 */
/*
 * Runs CMD_PROP_RADIO_DIV_SETUP chained with CMD_FS, with what _load_mode()
 * and cc26x2_cc13x2_rf_set_tx_pwr() left in the commands. The radio must be
 * idle.
 */
static int _run_setup(void)
{
    uint32_t cmdsta;

    rf_cmd_prop_radio_div_setup.status = RFC_IDLE;
    rf_cmd_fs.status = RFC_IDLE;

    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_prop_radio_div_setup);
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: setup rejected, cmdsta=%08lx\n", cmdsta);
        return -EIO;
    }
    /* if the setup command fails, the chain ends there and CMD_FS keeps its
     * IDLE status ([TRM] 25.3.2.5.2) */
    if ((_wait_cmd(&rf_cmd_prop_radio_div_setup.status) != 0) ||
        (rf_cmd_prop_radio_div_setup.status != RFC_PROP_DONE_OK) ||
        (_wait_cmd(&rf_cmd_fs.status) != 0) ||
        (rf_cmd_fs.status != RFC_DONE_OK)) {
        DEBUG("[cc26x2_cc13x2_rf]: setup failed, status: setup=%04x FS=%04x\n",
              rf_cmd_prop_radio_div_setup.status, rf_cmd_fs.status);
        return -EIO;
    }

    return 0;
}

int cc26x2_cc13x2_rf_set_fsk_mode(ieee802154_mr_fsk_srate_t srate, uint8_t mod_idx)
{
    const fsk_mode_t *mode = NULL;

    for (unsigned i = 0; i < ARRAY_SIZE(_modes); i++) {
        if ((_modes[i].srate == srate) && (_modes[i].mod_idx == mod_idx)) {
            mode = &_modes[i];
            break;
        }
    }
    if (mode == NULL) {
        DEBUG("[cc26x2_cc13x2_rf]: no MR-FSK mode for srate=%u mod_idx=%u\n",
              (unsigned)srate, (unsigned)mod_idx);
        return -EINVAL;
    }
    if (mode == _mode) {
        return 0;
    }

    _load_mode(mode);

    return _run_setup();
}

/*
 * Changes the channel with a single CMD_FS and waits for it to end.
 *
 * - CMD_FS is a radio operation command ([TRM] 25.3.3.1.5). If RX is running
 *   it is rejected with SchedulingError and this function returns -EIO, so
 *   the radio must be idle.
 * - The end of the command is taken from its status field, see _wait_cmd().
 *
 * The frequency is CHAN_CENTER_FREQ0 + channel * CHAN_SPACING. The setup
 * command keeps the `center_freq` it was given at power-on.
 */
int cc26x2_cc13x2_rf_change_chan(uint16_t channel)
{
    uint32_t cmdsta;

    if (!cc26x2_cc13x2_rf_chan_is_valid(channel)) {
        DEBUG("[cc26x2_cc13x2_rf]: invalid channel value=%u\n", channel);
        return -EINVAL;
    }

    uint64_t new_freq  = cc26x2_cc13x2_rf_chan_freq(channel);
    cc26x2_cc13x2_rf_freq_convert(new_freq,
                                  &rf_cmd_fs.frequency,
                                  &rf_cmd_fs.fract_freq);

    rf_cmd_fs.status = RFC_IDLE;
    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_fs);
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: couldn't change channel, cmdsta=%08lx\n",
              cmdsta);
        return -EIO;
    }
    if ((_wait_cmd(&rf_cmd_fs.status) != 0) || (rf_cmd_fs.status != RFC_DONE_OK)) {
        DEBUG("[cc26x2_cc13x2_rf]: CMD_FS failed, status=%04x\n",
              rf_cmd_fs.status);
        return -EIO;
    }

    return 0;
}

/*
 * Sets the TX power.
 *
 * Standard PA, immediate command CMD_SET_TX_POWER ([TRM] 25.3.3.2.16):
 *
 * - `tx_power` packs the PA settings: IB in bits 0-5, GC in bits 6-7, boost
 *   in bit 8 and the temperature coefficient in bits 9-15, the layout of
 *   RF_TxPowerTable_DEFAULT_PA_ENTRY() in [SDK] and of
 *   CC26X2_CC13X2_RF_PA_ENTRY() here. TI recommends the values from SmartRF
 *   Studio. The PA table of the cc1352p-launchpad matches the
 *   "LAUNCHXL-CC1352P1" table for 770-930 MHz of [RFCFG]
 *   (`pa_tables/cc13x2_cc26x2_r3/pasettings.json`).
 * - The new value is used from the next frame on.
 *
 * 20 dBm PA of the "P" devices, used for a power above the table of the
 * standard PA when the board has it for this band and
 * CONFIG_CC26X2_CC13X2_RF_PA20 is set:
 *
 * - The PA is selected by the setup command: `tx_power = 0xFFFF` and a 20 dBm
 *   PA power entry in its override list ([TRM] 25.3.3.1.2 and Table 25-22).
 *   So a change of PA, in either direction, runs the setup command again,
 *   which needs the radio to be idle, and tells the board to set its RF
 *   front end for that PA.
 * - While the 20 dBm PA is selected its power is changed with
 *   CMD_SET_TX20_POWER and CMD_SET_TX_POWER is not allowed, and the other way
 *   around ([TRM] 25.3.3.2.16 and 25.3.3.2.17).
 *
 * [SDK] changes the PA with CMD_CHANGE_PA and the override lists of its `_PA`
 * setup structures (RF_updatePaConfiguration()). That command is not in
 * [TRM], so it is not used here.
 */
int cc26x2_cc13x2_rf_set_tx_pwr(int16_t pow)
{
    uint32_t cmdsta;
    cc26x2_cc13x2_rf_pa_t *match = &cc26x2_cc13x2_rf_patable[0];

#if HAVE_PA20
    if (pow > cc26x2_cc13x2_rf_patable[0].dbm) {
        const cc26x2_cc13x2_rf_pa20_t *pa20 = &cc26x2_cc13x2_rf_pa20_table[0];

        for (unsigned i = 0; i < CC26X2_CC13X2_PA20_TABLE_NUMOF; i++) {
            if (cc26x2_cc13x2_rf_pa20_table[i].dbm >= pow) {
                pa20 = &cc26x2_cc13x2_rf_pa20_table[i];
            }
            else {
                break;
            }
        }
        DEBUG("[cc26x2_cc13x2_rf]: 20 dBm PA at %"PRIi16" dBm (for %"PRIi16" dBm)\n",
              pa20->dbm, pow);

        _pa20_val = pa20->val;
        if (!_pa20) {
            _pa20 = true;
            rf_cmd_prop_radio_div_setup.tx_power = RFC_TX_POWER_USE_PA20;
            _load_mode(_mode);
            CC26X2_CC13X2_RF_FRONTEND_SETUP(true);
            return _run_setup();
        }
        /* the setup command of a later mode change takes the value from the
         * override list */
        _load_mode(_mode);
        _cmd_set_tx20_power.tx20_power = pa20->val;
        cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&_cmd_set_tx20_power);
        if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
            DEBUG("[cc26x2_cc13x2_rf]: couldn't set the power of the 20 dBm PA\n");
            return -EIO;
        }
        return 0;
    }
#endif

    for (unsigned i = 0; i < CC26X2_CC13X2_PA_TABLE_NUMOF; i++) {
        if (cc26x2_cc13x2_rf_patable[i].dbm >= pow) {
            match = &cc26x2_cc13x2_rf_patable[i];
        }
        else {
            break;
        }
    }

    DEBUG("[cc26x2_cc13x2_rf]: found matching TX power %"PRIi16" dBm (for %"PRIi16" dBm)\n",
          match->dbm, pow);

    rf_cmd_set_tx_power.tx_power = match->val;
    /* a later setup command, see cc26x2_cc13x2_rf_set_fsk_mode(), would set
     * the power of its own field again */
    rf_cmd_prop_radio_div_setup.tx_power = match->val;

    if (HAVE_PA20 && _pa20) {
        /* back to the standard PA */
        _pa20 = false;
        _load_mode(_mode);
        CC26X2_CC13X2_RF_FRONTEND_SETUP(false);
        return _run_setup();
    }

    /* execute the command, it is immediate */
    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_set_tx_power);
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: couldn't set TX power to %"PRIi16" dBm",
              match->dbm);
        return -EIO;
    }

    return 0;
}

/*
 * Reads the IEEE address of the device.
 *
 * - FCFG1:MAC_15_4_0 holds "the first 32-bits of the 64-bit MAC 15.4
 *   address" and MAC_15_4_1 the last ones ([TRM] 11.4.1.44 and 11.4.1.45),
 *   programmed at production.
 * - CCFG:IEEE_MAC_0 and IEEE_MAC_1 hold bits 31:0 and 63:32 of a custom
 *   address, which is used when it is not all ones, the erased state
 *   (`hw_ccfg.h` of [SDK]).
 *
 * Both are numbers, so the first byte in memory is the last one of the
 * EUI-64. On a LAUNCHXL-CC1352P1 the FCFG words read 0x1CA5C2EF and
 * 0x00124B00 for the address 00:12:4B:00:1C:A5:C2:EF, which starts with
 * TI's OUI 00:12:4B.
 */
void cc26x2_cc13x2_rf_get_eui64(eui64_t *addr)
{
    uint64_t val = ((uint64_t)CCFG->IEEE_MAC_1 << 32) | CCFG->IEEE_MAC_0;

    if (val == UINT64_MAX) {
        val = ((uint64_t)FCFG->MAC_15_4_1 << 32) | FCFG->MAC_15_4_0;
    }

    addr->uint64 = byteorder_htonll(val);
}

void cc26x2_cc13x2_rf_set_short_addr(network_uint16_t short_addr)
{
    memcpy(_short_addr, short_addr.u8, IEEE802154_SHORT_ADDRESS_LEN);
}

void cc26x2_cc13x2_rf_set_long_addr(eui64_t long_addr)
{
    memcpy(_long_addr, long_addr.uint8, IEEE802154_LONG_ADDRESS_LEN);
}

void cc26x2_cc13x2_rf_set_pan_id(uint16_t pan_id)
{
    _pan_id = pan_id;
}

bool cc26x2_cc13x2_rf_l2_filter(uint8_t *mhr)
{
    uint8_t dst_addr[IEEE802154_LONG_ADDRESS_LEN];
    le_uint16_t dst_pan;
    uint8_t pan_bcast[] = IEEE802154_PANID_BCAST;

    int addr_len = ieee802154_get_dst(mhr, dst_addr, &dst_pan);
    if (addr_len < 0) {
        return false;
    }

    /* filter PAN ID */
    /* Will only work on little endian platform (all cc26xx/cc13xx ;-) ) */
    if ((memcmp(pan_bcast, dst_pan.u8, 2) != 0) &&
        (memcmp(&_pan_id, dst_pan.u8, 2) != 0)) {
        return false;
    }

    /* check destination address */
    if (((addr_len == IEEE802154_SHORT_ADDRESS_LEN) &&
          (memcmp(_short_addr, dst_addr, addr_len) == 0 ||
           memcmp(ieee802154_addr_bcast, dst_addr, addr_len) == 0)) ||
        ((addr_len == IEEE802154_LONG_ADDRESS_LEN) &&
          (memcmp(_long_addr, dst_addr, addr_len) == 0))) {
        return true;
    }

    return false;
}
