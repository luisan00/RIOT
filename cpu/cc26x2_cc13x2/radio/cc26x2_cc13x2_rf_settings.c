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
 * @brief           CC13x2 sub-GHz IEEE 802.15.4g radio: radio settings
 *
 * The settings of the RF Core for the MR-FSK PHY: the register overrides and
 * the parameters of each mode, the structures of the commands the driver
 * uses, and the functions that select a mode and a TX power.
 *
 * They are checked against these documents from Texas Instruments:
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

#include "cc26x2_cc13x2_rf_internal.h"
#include "cc26x2_cc13x2_rfc.h"
#include "rf_conf.h"

#include "net/ieee802154.h"

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
 * [0:16, 24, 30] are don't care. */
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
    uint16_t cs_start_us;               /**< time from the start of the carrier
                                             sense command to its first RSSI
                                             measurement */
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
 * `cs_start_us` is not from TI either. It is the time CMD_PROP_CS took when
 * it was set to end with its first RSSI measurement, on a clear channel,
 * from the submission of the command to its interrupt. Measured on a
 * CC1352P1, it repeats within 1 us. The carrier sense runs for the CCA
 * duration after it, see rf_cmd_prop_cs. As the measurement includes the
 * time to submit the command and to serve the interrupt, the channel is
 * observed a little longer than that: the whole command takes 207 to 211 us
 * more than this value, where 160 us were added.
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
        .cs_start_us = 258,
        .overrides = _overrides_50k_h05,
    },
    {   /* setting_tc106_154g: 25 kHz deviation, 100 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_50K, .mod_idx = 64,
        .deviation = 100, .rate_word = 32768, .rx_bw = 82, .preamble_len = 7,
        .int_freq = 0x8000, .turnaround_us = 819,
        .cs_start_us = 258,
        .overrides = _overrides_50k_h1,
    },
    {   /* setting_tc152_154g: 25 kHz deviation, 137 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_100K, .mod_idx = 32,
        .deviation = 100, .rate_word = 65536, .rx_bw = 84, .preamble_len = 7,
        .int_freq = 0x0599, .turnaround_us = 510,
        .cs_start_us = 175,
        .overrides = _overrides_100k_h05,
    },
    {   /* setting_tc137: 50 kHz deviation, 196 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_100K, .mod_idx = 64,
        .deviation = 200, .rate_word = 65536, .rx_bw = 86, .preamble_len = 4,
        .int_freq = 0x08CC, .turnaround_us = 509,
        .cs_start_us = 175,
        .overrides = _overrides_100k_h1,
    },
    {   /* setting_tc142: 37.5 kHz deviation, 273 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_150K, .mod_idx = 32,
        .deviation = 150, .rate_word = 98304, .rx_bw = 88, .preamble_len = 4,
        .int_freq = 0x08CC, .turnaround_us = 401,
        .cs_start_us = 133,
        .overrides = _overrides_150k_h05,
    },
    {   /* setting_tc146_154g: 50 kHz deviation, 311 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_200K, .mod_idx = 32,
        .deviation = 200, .rate_word = 131072, .rx_bw = 89, .preamble_len = 7,
        .int_freq = 0x0999, .turnaround_us = 355,
        .cs_start_us = 133,
        .overrides = _overrides_200k_h05,
    },
    {   /* setting_tc151: 100 kHz deviation, 273 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_200K, .mod_idx = 64,
        .deviation = 400, .rate_word = 131072, .rx_bw = 88, .preamble_len = 4,
        .int_freq = 0x0733, .turnaround_us = 356,
        .cs_start_us = 133,
        .overrides = _overrides_200k_h1,
    },
    {   /* setting_tc154: 75 kHz deviation, 285 kHz RX bandwidth */
        .srate = IEEE802154_MR_FSK_SRATE_300K, .mod_idx = 32,
        .deviation = 300, .rate_word = 196608, .rx_bw = 92, .preamble_len = 4,
        .int_freq = 0x0666, .turnaround_us = 304,
        .cs_start_us = 115,
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
 * Starts the radio timer (RAT), which the RF Core needs for everything that
 * is timed and to chain commands. The only parameter the driver sets is
 * `rat0`, the offset the RAT had when CMD_SYNC_STOP_RAT stopped it; 0 is
 * fine when there is none yet.
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
    .next_op = NULL, /* set when the commands are chained */
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
    .rat0 = 0, /* set at power-up */
};

/**
 * CMD_PROP_RADIO_DIV_SETUP
 *
 * The setup command of the radio. It configures what TX and RX have in
 * common: modulation, symbol rate, receiver bandwidth, preamble, sync word,
 * whitening and CRC, intermediate frequency and frequency band.
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
    .next_op = NULL, /* set when the commands are chained */
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
 * The frequency synthesizer command: it tunes the radio to a channel.
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
    .next_op = NULL, /* set when the commands are chained */
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
    .frequency = 0, /* set with the channel */
    .fract_freq = 0, /* set with the channel */
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
 * The advanced transmit command of the proprietary mode. Despite the name of
 * the mode, it sends IEEE 802.15.4g frames. The driver fills in `pkt` and
 * `pkt_len` for each frame.
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
    .next_op = NULL, /* set when the commands are chained */
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
    .pkt_len = 0, /* set for each frame */
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
    .pkt = 0 /* set for each frame */
};

/**
 * CMD_PROP_RX_ADV
 *
 * The advanced receive command of the proprietary mode. It keeps the radio
 * receiving and puts every frame into the RX queue, until it is stopped or
 * aborted.
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
 * with PROP_ERROR_RXBUF ([TRM] Table 25-186), see _rx_ring_build().
 */
rfc_cmd_prop_rx_adv_t rf_cmd_prop_rx_adv =
{
    .command_no = RFC_CMD_PROP_RX_ADV,
    .status = RFC_IDLE,
    .next_op = NULL, /* set when the commands are chained */
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
    .queue = 0, /* set at initialization */
    .output = 0 /* set at initialization */
};

/**
 * CMD_PROP_RX_ADV for the ACK of a transmitted frame
 *
 * Chained to CMD_PROP_TX_ADV when the frame requests an ACK, so the RF Core
 * goes from TX to RX by itself and ends the wait by itself. It is
 * rf_cmd_prop_rx_adv with another end ([TRM] 25.10.5.4 and Table 25-186):
 *
 * - `repeat_ok = 0`: the command ends with PROP_DONE_OK after the first frame
 *   with a good CRC, whatever it is. If it is not the expected ACK, that ACK
 *   is not coming.
 * - `end_trigger` TRIG_REL_START: if no sync word was found `end_time` after
 *   the start of the command it ends with PROP_DONE_RXTIMEOUT. `end_type = 0`
 *   lets a frame that is being received at that moment finish (the command
 *   then ends with PROP_DONE_ENDED), so the time only has to reach to the
 *   sync word of the ACK, see _ack_wait_us().
 * - `repeat_nok = 1`: after a frame with a CRC error the search goes on until
 *   the end trigger.
 */
rfc_cmd_prop_rx_adv_t rf_cmd_prop_rx_ack =
{
    .command_no = RFC_CMD_PROP_RX_ADV,
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
    .pkt_conf = {
        .fs_off = 0,
        .repeat_ok = 0,
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
        .type = RFC_TRIG_REL_START,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .end_time = 0, /* set by _load_mode() */
    .addr = 0, /* unused */
    .queue = 0, /* set at initialization */
    .output = 0 /* set at initialization */
};

/**
 * Carrier sense command, used to perform CCA
 *
 * The carrier sense fields are described in [TRM] Table 25-178 and section
 * 25.10.5.5:
 *
 * - Only the RSSI is used (`ena_rssi = 1`, `ena_corr = 0`): CCA mode 1 of
 *   the standard, energy above threshold. The channel is IDLE after
 *   `num_rssi_idle` consecutive measurements below `rssi_thr`, BUSY after
 *   `num_rssi_busy` consecutive measurements above it and INVALID otherwise.
 * - `busy_op = 1` ends the command with PROP_DONE_BUSY on the first BUSY
 *   state. `idle_op = 0` keeps it running while the channel is IDLE, until
 *   the end trigger: #CC26X2_CC13X2_RF_CCA_DURATION_US after the first RSSI
 *   measurement of the mode, set by _load_mode(). It then ends with
 *   PROP_DONE_IDLE ([TRM] Table 25-188).
 * - `timeout_res = 0`: if there was no measurement at all before the end
 *   trigger, the command ends with PROP_DONE_BUSYTIMEOUT and the channel
 *   counts as busy.
 * - `off_idle = 0` and `off_busy = 0` keep the synthesizer running when the
 *   command ends.
 *
 * A busy channel is the result TRUE of the command and a clear one FALSE
 * ([TRM] Table 25-188). For a transmission with CCA the transmit command is
 * chained to this one with the condition COND_STOP_ON_TRUE ([TRM] Table
 * 25-8), so that the RF Core only transmits if the channel is clear, see
 * cc26x2_cc13x2_rf_request_transmit().
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
        /* the synthesizer stays on after the carrier sense, for the
         * command that follows */
        .off_idle = 0,
        .off_busy = 0,
    },
    .__dummy0 = 0,
    .conf = {
        .ena_rssi = 1, /* use RSSI as the measurement parameter */
        .ena_corr = 0,
        .operation = 0,
        /* stop on Busy, observe an Idle channel until the end trigger */
        .busy_op = 1,
        .idle_op = 0,
        .timeout_res = 0,
    },
    .rssi_thr = -80, /* a starting value, the radio HAL sets its own */
    .num_rssi_idle = 1, /* consecutive RSSI measurements to mark channel as
                           Idle */
    .num_rssi_busy = 1, /* consecutive RSSI measurements to mark channel as
                           Busy */
    .corr_period = 0,
    .corr_config = {
        .num_corr_inv = 0,
        .num_corr_busy = 0,
    },
    /* the end time depends on the mode, see _load_mode() */
    .end_trigger = {
        .type = RFC_TRIG_REL_START,
        .ena_cmd = 0,
        .trigger_no = 0,
        .past_trig = 0
    },
    .end_time = 0,
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
rfc_cmd_sync_stop_rat_t rf_cmd_sync_stop_rat =
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
 *          #rf_cmd_sync_stop_rat
 *
 * Stops the synthesizer and switches the analog modules off. [TRM]: it "must
 * always be run before the radio is powered down". [SDK] does it with direct
 * register writes instead (RFCSynthPowerDown() in driverlib rfc.c).
 */
rfc_cmd_fs_powerdown_t rf_cmd_fs_powerdown =
{
    .command_no = RFC_CMD_FS_POWERDOWN,
    .status = RFC_IDLE,
    .next_op = &rf_cmd_sync_stop_rat,
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

/* Symbol period of a mode in nanoseconds */
static uint32_t _symbol_ns(const fsk_mode_t *mode)
{
    switch (mode->srate) {
    case IEEE802154_MR_FSK_SRATE_50K:
        return 20000;
    case IEEE802154_MR_FSK_SRATE_100K:
        return 10000;
    case IEEE802154_MR_FSK_SRATE_150K:
        return 6667;
    case IEEE802154_MR_FSK_SRATE_200K:
        return 5000;
    case IEEE802154_MR_FSK_SRATE_300K:
        return 3334;
    default:
        return 2500;
    }
}

/*
 * Time the receiver searches for the sync word of an ACK after the frame
 * was sent, in microseconds.
 *
 * IEEE Std 802.15.4-2024, 6.6.3.4, only asks to wait for the Ack frame "the
 * expected time". This is the time RIOT's SubMAC and its at86rf215 driver
 * wait, a backoff period (aTurnaroundTime + phyCcaDuration) plus
 * aTurnaroundTime plus the frame, with two differences:
 *
 * - The synchronization header counts with the symbol period of the mode.
 *   Its length is the one of the at86rf215, the longest of the radios RIOT
 *   has for this PHY: eight times ieee802154_mr_fsk_plen() preamble octets,
 *   which is 64, the maximum of phyFskPreambleLength, from 150 ksym/s on.
 * - The PHR and the PSDU of the ACK are left out: the wait ends at the sync
 *   word, and a frame that has started is received to its end, see
 *   rf_cmd_prop_rx_ack.
 */
static uint32_t _ack_wait_us(const fsk_mode_t *mode)
{
    const uint32_t backoff_period_us = IEEE802154G_ATURNAROUNDTIME_US
                                     + CC26X2_CC13X2_RF_CCA_DURATION_US;
    const uint32_t shr_symbols = (ieee802154_mr_fsk_plen(mode->srate) * 8U
                                  + (SYNC_WORD_BITS / 8U)) * 8U;

    return backoff_period_us + IEEE802154G_ATURNAROUNDTIME_US
           + (shr_symbols * _symbol_ns(mode) + 999U) / 1000U;
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
    /* in RAT ticks of 250 ns */
    rf_cmd_prop_cs.end_time = (mode->cs_start_us + CC26X2_CC13X2_RF_CCA_DURATION_US) * 4;
    rf_cmd_prop_rx_ack.end_time = _ack_wait_us(mode) * 4;

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
 * Loads what the radio starts with: the default mode, the standard PA and
 * the first entry of the PA table of the board. Nothing is sent to the RF
 * Core here.
 */
void cc26x2_cc13x2_rf_settings_init(void)
{
    _pa20 = false;
    CC26X2_CC13X2_RF_FRONTEND_SETUP(false);

    _load_mode(&_modes[MODE_DEFAULT]);
    rf_cmd_prop_radio_div_setup.tx_power = cc26x2_cc13x2_rf_patable[0].val;
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

    return cc26x2_cc13x2_rf_run_setup();
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
            return cc26x2_cc13x2_rf_run_setup();
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
        return cc26x2_cc13x2_rf_run_setup();
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
