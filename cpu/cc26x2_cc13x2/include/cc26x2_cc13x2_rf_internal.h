/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         cpu_cc26x2_cc13x2_rf
 * @{
 *
 * @file
 * @brief           CC26x2/CC13x2 IEEE 802.15.4g Sub-1-GHz driver internal
 *                  functions.
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stdbool.h>
#include <stddef.h>

#include "net/eui64.h"
#include "iolist.h"
#include "byteorder.h"
#include "macros/units.h"
#include "net/ieee802154.h"

#include "cc26xx_cc13xx_rfc_common_cmd.h"
#include "cc26xx_cc13xx_rfc_prop_cmd.h"

#include "cc26xx_cc13xx_rfc_mailbox.h"
#include "cc26xx_cc13xx_rfc_prop_mailbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Number of receive buffers
 */
#ifndef CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF
#  define CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF (4)
#endif

/* XXX: shouldn't this reside on net/ieee802154.h */
/**
 * @name    SUN PHY frequency band definitions
 *
 * These are the frequency band identifiers for the SUN PHY.
 *
 * @see IEEE Std 802.154.g-2012, Table 68f
 * @{
 */
#define IEEE802154_SUN_PHY_FREQ_169_MHZ     0   /**< 169.400-169.475 (Europe) */
#define IEEE802154_SUN_PHY_FREQ_450_MHZ     1   /**< 450-470 (US FCC Part 22/90) */
#define IEEE802154_SUN_PHY_FREQ_470_MHZ     2   /**< 470-510 (China) */
#define IEEE802154_SUN_PHY_FREQ_780_MHZ     3   /**< 779-787 (China) */
#define IEEE802154_SUN_PHY_FREQ_863_MHZ     4   /**< 863-870 (Europe) */
#define IEEE802154_SUN_PHY_FREQ_896_MHZ     5   /**< 896-901 (US FCC Part 90) */
#define IEEE802154_SUN_PHY_FREQ_901_MHZ     6   /**< 901-902 (US FCC Part 24) */
#define IEEE802154_SUN_PHY_FREQ_915_MHZ     7   /**< 902-928 (US) */
#define IEEE802154_SUN_PHY_FREQ_917_MHZ     8   /**< 917-923.5 (Korea) */
#define IEEE802154_SUN_PHY_FREQ_920_MHZ     9   /**< 920-928 (Japan) */
#define IEEE802154_SUN_PHY_FREQ_928_MHZ     10  /**< 928-960 (US, non-contiguous) */
#define IEEE802154_SUN_PHY_FREQ_950_MHZ     11  /**< 950-958 (Japan) */
#define IEEE802154_SUN_PHY_FREQ_1427_MHZ    12  /**< 1427-1518 (US and Canada, non-contiguous) */
#define IEEE802154_SUN_PHY_FREQ_2450_MHZ    13  /**< 2400-2483.5 */
/** @} */

/**
 * @brief   SUN PHY frequency band used, one of the `IEEE802154_SUN_PHY_FREQ_*`
 *          identifiers
 *
 * The driver implements the 2-FSK modes of the MR-FSK PHY listed in
 * cc26x2_cc13x2_rf_set_fsk_mode(), with the radio settings that TI gives for
 * the 770 to 930 MHz RF front end of the CC1312R1, CC1352R1 and CC1352P1
 * LaunchPads. These bands have channel plans inside that range:
 *
 * | Identifier                        | Band          | 200 kHz plan      | 400 kHz plan     |
 * |:--------------------------------- |:------------- |:----------------- |:---------------- |
 * | `IEEE802154_SUN_PHY_FREQ_780_MHZ` | 779-787 MHz   | 779.200 MHz, 39   | 779.400 MHz, 19  |
 * | `IEEE802154_SUN_PHY_FREQ_863_MHZ` | 863-870 MHz   | 863.125 MHz, 34   | 863.225 MHz, 17  |
 * | `IEEE802154_SUN_PHY_FREQ_915_MHZ` | 902-928 MHz   | 902.200 MHz, 129  | 902.400 MHz, 64  |
 * | `IEEE802154_SUN_PHY_FREQ_917_MHZ` | 917-923.5 MHz | 917.100 MHz, 32   | 917.300 MHz, 16  |
 * | `IEEE802154_SUN_PHY_FREQ_920_MHZ` | 920-928 MHz   | 920.600 MHz, 38   | 920.900 MHz, 18  |
 *
 * Each plan gives the frequency of channel 0 and the number of channels. The
 * channel spacing is selected with #CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ.
 *
 * The other bands are not available. The 470 MHz and 950 MHz bands are
 * outside the range of the RF front end, and the 2450 MHz band uses the
 * 2.4 GHz RF path, which this driver does not handle.
 *
 * Pick the band that is allowed where the device is used: the driver does not
 * check any regulation, neither the band nor duty cycle or channel limits.
 *
 * The default is the 863 MHz band. With Kconfig the band is chosen in the
 * "Sub-GHz frequency band" menu of the CPU. Without it, define this macro, for
 * example
 * `CFLAGS += -DCONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ=IEEE802154_SUN_PHY_FREQ_915_MHZ`.
 */
#ifndef CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ
#  define CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ   IEEE802154_SUN_PHY_FREQ_863_MHZ
#endif

/**
 * @brief   Channel spacing in kHz, 200 or 400
 *
 * IEEE 802.15.4g ties the spacing to the operating mode of each band, and
 * Wi-SUN uses other pairs again (for example 150 kbit/s on a 200 kHz plan in
 * Europe and on a 400 kHz plan in North America). The radio HAL has no field
 * for it, so the spacing is chosen at build time, independent of the mode
 * set with config_phy.
 *
 * The default is 200 kHz. With Kconfig it is chosen in the "Sub-GHz channel
 * spacing" menu of the CPU.
 */
#ifndef CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ
#  define CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ   (200U)
#endif

/* TODO: teach the HAL about SUN PHY frequency bands? */
/**
 * @name    SUN PHY channel information
 *
 * The frequency of channel `n` is `CHAN_CENTER_FREQ0 + n * CHAN_SPACING`.
 *
 * Sources of the plans:
 *
 * - 863 MHz: TI's 15.4-Stack (`ti154stack_rf_config_common.js` in the
 *   SimpleLink SDK) for both plans, and RIOT's at86rf215 driver.
 * - 915 MHz: TI's 15.4-Stack for the 200 kHz plan and TI's Wi-SUN stack
 *   (`ti_wisunfan_rf_config_common.js`, `902_928_400`) for the 400 kHz plan.
 * - 920 MHz: TI's Wi-SUN stack, `920_928_200` and `920_928_400`.
 * - 780 MHz and 917 MHz: the SDK has no table. The plans come from two other
 *   implementations of the same PHY: the Contiki-NG driver for this MCU
 *   (200 kHz) and the regulatory table of wisun-br-linux (both spacings).
 *
 * `CC26X2_CC13X2_RF_CENTER_FREQ` is the center frequency of the band in MHz,
 * for the `centerFreq` field of CMD_PROP_RADIO_DIV_SETUP.
 *
 * `CC26X2_CC13X2_RF_LO_DIVIDER` is the `loDivider` field of that command. TI
 * derives it from the frequency: 5 from 863 MHz up and 6 from 719 MHz up
 * (`getLoDivider()` in `radioconfig/.meta/parameter_handler.js` of the SDK).
 * @{
 */
/** @brief   Lowest channel number */
#define CC26X2_CC13X2_RF_CHAN_MIN               (0)
/** @brief   Channel spacing in Hz */
#define CC26X2_CC13X2_RF_CHAN_SPACING           KHZ(CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ)

#if CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_780_MHZ
/** @brief   Center frequency of the band in MHz */
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (783U)
/** @brief   LO divider of the band */
#  define CC26X2_CC13X2_RF_LO_DIVIDER           (6U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
/** @brief   Highest channel number */
#    define CC26X2_CC13X2_RF_CHAN_MAX           (38)
/** @brief   Center frequency of channel 0 in Hz */
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(779200)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_MAX           (18)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(779400)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_863_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (868U)
#  define CC26X2_CC13X2_RF_LO_DIVIDER           (5U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_MAX           (33)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863125)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_MAX           (16)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863225)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_915_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (915U)
#  define CC26X2_CC13X2_RF_LO_DIVIDER           (5U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_MAX           (128)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(902200)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_MAX           (63)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(902400)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_917_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (920U)
#  define CC26X2_CC13X2_RF_LO_DIVIDER           (5U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_MAX           (31)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(917100)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_MAX           (15)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(917300)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_920_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (924U)
#  define CC26X2_CC13X2_RF_LO_DIVIDER           (5U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_MAX           (37)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(920600)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_MAX           (17)
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(920900)
#  endif
#else
#  error "CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ: frequency band not supported"
#endif

#ifndef CC26X2_CC13X2_RF_CHAN_MAX
#  error "CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ: must be 200 or 400"
#endif
/** @} */

/**
 * @brief   Length of the status length
 * The status section resides at the end of a received packet and
 * contains information such as the RSSI, sync word (SFD) that
 * was detected, etc
 *
 * @note    If this value is updated @ref rf_cmd_prop_rx_adv MUST
 *          be updated accordingly. For now it only includes the
 *          RSSI which is all we need.
 *
 *          The status doesn't include the LQI! The LQI of the last
 *          packet received MUST be read from an RF Core register,
 *          However, it requires some shenanigans to correctly read
 *          it for the packet we want, e.g. reading it at the ISR
 *          for `CPE_IRQ_RX_ENTRY_DONE` and saving it elsewhere.
 */
#define CC26X2_CC13X2_RF_RX_STATUS_LEN 1

/**
 * @brief   Align the size of a buffer to a 4-byte boundary.
 *
 * For example:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ {.c}
 * static uint8_t buf[ALIGN_TO_4(2047)];
 *
 * assert(sizeof(buf) == 2048);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#define ALIGN_TO_4(size) (((size) + 3) & ~3)

/**
 * @brief   Get center frequency of a channel
 *
 * @return Frequency, in Hz.
 */
static inline uint64_t cc26x2_cc13x2_rf_chan_freq(uint16_t chan)
{
    const uint64_t chan_center_freq0 = CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0;
    const uint64_t spacing = CC26X2_CC13X2_RF_CHAN_SPACING;

    return chan_center_freq0 + (chan * spacing);
}

/**
 * @brief   Convert a frequency in Hz, to a frequency that the CMD_FS
 *          command can understand.
 *
 * @param[in]   new_freq    New frequency, in Hz.
 * @param[out]  freq        Frequency, in MHz.
 * @param[out]  fract       Fractional part of the frequency.
 */
static inline void cc26x2_cc13x2_rf_freq_convert(uint64_t new_freq,
                                                 uint16_t *freq,
                                                 uint16_t *fract)
{
    /* convert to MHz, discarding any decimal */
    *freq = (uint16_t)(new_freq / MHZ(1));

    /* convert it back to Hz again and get difference from new_freq, to get
     * the fractional part. Doing the calculation this way we can get a
     * value (freq * (fract / 0x10000))) MHz that is close or equal
     * to new_freq.
     *
     * See Section 25.3.3.1.5 CMD_FS: Frequency Synthesizer Controls Command
     * on TRM (swcu185d) */
    const uint64_t diff = new_freq - ((*freq) * MHZ(1));
    *fract = (uint16_t)((diff * 0x10000) / 1000000);
}

/**
 * @brief   CMD_SYNC_START_RAT
 *
 * Synchronously start the RAT
 */
extern rfc_cmd_sync_start_rat_t rf_cmd_sync_start_rat;

/**
 * @brief   CMD_PROP_RADIO_DIV_SETUP
 *
 * Proprietary Mode Radio Setup Command for All Frequency Bands
 */
extern rfc_cmd_prop_radio_div_setup_t rf_cmd_prop_radio_div_setup;

/**
 * @brief   CMD_FS
 *
 * Frequency Synthesizer
 */
extern rfc_cmd_fs_t rf_cmd_fs;

/**
 * @brief   CMD_PROP_TX_ADV
 *
 * Proprietary Mode Advanced Transmit Command
 */
extern rfc_cmd_prop_tx_adv_t rf_cmd_prop_tx_adv;

/**
 * @brief   CMD_PROP_RX_ADV
 *
 * Proprietary Mode Advanced Receive Command
 */
extern rfc_cmd_prop_rx_adv_t rf_cmd_prop_rx_adv;

/**
 * @brief   CMD_PROP_CS
 *
 * Proprietary Mode Carrier Sense Command
 */
extern rfc_cmd_prop_cs_t rf_cmd_prop_cs;

/**
 * @brief   CMD_CLEAR_RX
 *
 * Clear the RX queue
 */
extern rfc_cmd_clear_rx_t rf_cmd_clear_rx;

/**
 * @brief   CMD_SET_TX_POWER
 *
 * Set TX Power
 */
extern rfc_cmd_set_tx_power_t rf_cmd_set_tx_power;

/**
 * @brief   Initialize internal variables
 */
void cc26x2_cc13x2_rf_internal_init(void);

/**
 * @brief   Request to turn on the radio
 */
void cc26x2_cc13x2_rf_request_on(void);

/**
 * @brief   Confirm that the RF Core is on
 *
 * @pre cc26x2_cc13x2_rf_request_on MUST have been called before.
 *
 * @return -EAGAIN if the RF Core is still starting.
 * @return 0 if the RF Core is on.
 */
int cc26x2_cc13x2_rf_confirm_on(void);

/**
 * @brief   Request a CCA
 *
 * @note The CCA is confirmed when the interrupt
 *       `CPE_IRQ_LAST_COMMAND_DONE` is raised AND
 *       rf_cmd_prop_cs.status is either #RFC_PROP_DONE_IDLE
 *       or `RFC_PROP_DONE_BUSY`.
 *       `CPE_IRQ_LAST_COMMAND_DONE` MUST be enabled.
 *
 * @return 0 on success.
 * @return negative errno value on error.
 */
int cc26x2_cc13x2_rf_request_cca(void);

/**
 * @brief   Start RX
 *
 * Does nothing if RX is already on.
 *
 * @return 0 on success.
 * @return -EBUSY if another radio operation command is running.
 * @return -EIO on any other error.
 */
int cc26x2_cc13x2_rf_rx_start(void);

/**
 * @brief   Stop RX and wait until the radio is idle
 *
 * A frame being received is dropped. Does nothing if RX is not on.
 */
void cc26x2_cc13x2_rf_rx_stop(void);

/**
 * @brief   Receive a packet from the packet queue.
 *
 * @pre An ISR should inform that a packet is ready in the
 * queue.
 *
 * @return NULL if no packet exists on the queue.
 * @return pointer to packet on success.
 */
rfc_data_entry_general_t *cc26x2_cc13x2_rf_recv(void);

/**
 * @brief   Write PDSU into transmit buffer
 *
 * @return 0 on success
 * @return negative errno value on error.
 */
int cc26x2_cc13x2_rf_write(const iolist_t *iolist);

/**
 * @brief   Request the RF Core to transmit our packet.
 *
 * @pre @ref cc26x2_cc13x2_rf_write MUST have been called.
 */
int cc26x2_cc13x2_rf_request_transmit(void);

/**
 * @brief   Get the PSDU length of the last written packet with
 *          @ref cc26x2_cc13x2_rf_write.
 *
 * @return PSDU length.
 */
size_t cc26x2_cc13x2_rf_tx_psdu_len(void);

/**
 * @brief   Set the CCA threshold for the radio.
 *
 * @param[in]   threshold   Threshold for CCA in dBm.
 */
void cc26x2_cc13x2_rf_set_cca_threshold(int8_t threshold);

/**
 * @brief   Change channel
 *
 * @param[in]   channel Channel to set.
 *
 * @return 0 on success.
 * @return negative errno value on error.
 */
int cc26x2_cc13x2_rf_change_chan(uint16_t channel);

/**
 * @brief   Set transmit power
 *
 * @param[in]   pow   Transmit power in dBm.
 *
 * @return 0 on success.
 * @return negative errno value on error.
 */
int cc26x2_cc13x2_rf_set_tx_pwr(int16_t pow);

/**
 * @brief   Get HW address of the device.
 *
 * @return  EUI-64 address.
 */
eui64_t cc26x2_cc13x2_rf_get_hwaddr(void);

/**
 * @brief   Set PAN ID.
 *
 * @param[in]   pan_id The PAN ID.
 */
void cc26x2_cc13x2_rf_set_pan_id(uint16_t pan_id);

/**
 * @brief   Set short IEEE 802.154.g address.
 *
 * @param[in]   short_addr The short address.
 */
void cc26x2_cc13x2_rf_set_short_addr(network_uint16_t short_addr);

/**
 * @brief   Set long IEEE 802.15.4g MAC address.
 *
 * @param[in]   long_addr The long address.
 */
void cc26x2_cc13x2_rf_set_long_addr(eui64_t long_addr);

/**
 * @brief   Filter an L2 payload.
 *
 * @note @p mhr length should be enough to access the destination PAN ID and
 *       address.
 *
 * @brief[in]   mhr   MAC header.
 *
 * @return true if it passed the filter, false otherwise.
 */
bool cc26x2_cc13x2_rf_l2_filter(uint8_t *mhr);

/**
 * @brief   Select the MR-FSK mode
 *
 * The modes are the 2-FSK settings that TI provides for these MCUs:
 *
 * | Symbol rate  | Modulation index | `mod_idx` | Deviation | Preamble |
 * |:------------ |:---------------- |:--------- |:--------- |:-------- |
 * | 50 ksym/s    | 0.5              | 32        | 12.5 kHz  | 4 bytes  |
 * | 50 ksym/s    | 1.0              | 64        | 25 kHz    | 7 bytes  |
 * | 100 ksym/s   | 0.5              | 32        | 25 kHz    | 7 bytes  |
 * | 100 ksym/s   | 1.0              | 64        | 50 kHz    | 4 bytes  |
 * | 150 ksym/s   | 0.5              | 32        | 37.5 kHz  | 4 bytes  |
 * | 200 ksym/s   | 0.5              | 32        | 50 kHz    | 7 bytes  |
 * | 200 ksym/s   | 1.0              | 64        | 100 kHz   | 4 bytes  |
 * | 300 ksym/s   | 0.5              | 32        | 75 kHz    | 4 bytes  |
 *
 * The power-on mode is 50 ksym/s with modulation index 1.0.
 *
 * @pre     The radio is on and idle.
 *
 * @param[in]   srate       Symbol rate.
 * @param[in]   mod_idx     Modulation index, in 1/64 (32 is 0.5, 64 is 1.0).
 *
 * @return 0 on success.
 * @return -EINVAL if there is no mode for these parameters.
 * @return -EIO if the RF Core rejected the setup.
 */
int cc26x2_cc13x2_rf_set_fsk_mode(ieee802154_mr_fsk_srate_t srate, uint8_t mod_idx);

/**
 * @brief   Copy PSDU bytes between a frame buffer of the RF Core and the
 *          upper layer, reversing the bit order of every byte.
 *
 * In its IEEE 802.15.4g mode the RF Core sends and receives every byte MSB
 * first. IEEE 802.15.4g sends the PHR MSB first but the PSDU LSB first, so
 * the PSDU has to be bit-reversed on its way in and out (SWCU185G, section
 * 25.10.5.2.1, note).
 *
 * @param[out]  dst     Destination buffer.
 * @param[in]   src     Source buffer.
 * @param[in]   len     Number of bytes to copy.
 */
void cc26x2_cc13x2_rf_copy_psdu(uint8_t *dst, const uint8_t *src, size_t len);

#ifdef __cplusplus
} /* end extern "C" */
#endif

/** @} */
