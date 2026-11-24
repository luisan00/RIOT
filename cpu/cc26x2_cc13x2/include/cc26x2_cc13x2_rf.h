/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @defgroup        cpu_cc26x2_cc13x2_rf CC13x2 sub-GHz IEEE 802.15.4g radio
 * @ingroup         cpu_cc26x2_cc13x2
 * @ingroup         drivers_netdev
 * @brief           Driver for the sub-GHz radio of the CC1312R and CC1352
 *                  MCUs, for the IEEE 802.15.4 radio HAL
 *
 * The driver implements the MR-FSK PHY of IEEE 802.15.4g (SUN FSK) on top of
 * the proprietary mode of the RF Core. Boards select it as `netdev_default`;
 * with GNRC it runs below the SubMAC (`netdev_ieee802154_submac`).
 *
 * ## Operating modes
 *
 * 2-FSK without FEC, with the radio settings TI publishes for these MCUs:
 *
 * | Symbol rate | Modulation index |
 * |:----------- |:---------------- |
 * | 50 ksym/s   | 0.5 and 1.0      |
 * | 100 ksym/s  | 0.5 and 1.0      |
 * | 150 ksym/s  | 0.5              |
 * | 200 ksym/s  | 0.5 and 1.0      |
 * | 300 ksym/s  | 0.5              |
 *
 * The mode is set with `config_phy` of the radio HAL. The SubMAC configures
 * the one given by @ref CONFIG_IEEE802154_MR_FSK_DEFAULT_SRATE and
 * @ref CONFIG_IEEE802154_MR_FSK_DEFAULT_MOD_IDX when the interface starts.
 *
 * ## Frequency bands
 *
 * The band and the channel spacing are build-time options, see
 * @ref CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ and
 * @ref CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ. The bands are those of the
 * SUN FSK PHY between 770 and 930 MHz: 780 MHz, 863 MHz, 866 MHz, 867 MHz,
 * 870 MHz, 915 MHz and its regional variants 915 MHz-a to 915 MHz-e, 917 MHz,
 * 919 MHz, 920 MHz, 920 MHz-a and 920 MHz-b. Channel numbers and frequencies
 * follow IEEE Std 802.15.4-2024.
 *
 * The default is none of them, for compatibility with RIOT's AT86RF215
 * driver: it is the 863 MHz band with the channel plan that driver and TI's
 * 15.4-Stack use, which is not the one of the standard, see
 * @ref CC26X2_CC13X2_RF_FREQ_863_COMPAT. With it and the defaults of the
 * stack (200 ksym/s, modulation index 1.0, channel 5) a node is on
 * 865.225 MHz, like an AT86RF215 node set to the MR-FSK PHY and 2-FSK.
 *
 * Some bands do not start at channel 0. The channel the stack starts on,
 * @ref CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL, has to be a channel of the
 * band: with the SubMAC the build fails if it is not.
 *
 * ## Features
 *
 * - Automatic transmission of ACK frames, address filtering, promiscuous mode
 * - Reception of the ACK of a transmitted frame: the RF Core goes on to
 *   receive after the frame and the result of the transmission tells whether
 *   the ACK came. The wait ends when no sync word was found in the time the
 *   ACK of an AT86RF215, with its long preamble, takes to get there, from
 *   3.9 to 5.7 ms depending on the symbol rate. ACK frames are not passed to
 *   the upper layer
 * - A frame that comes in instead of that ACK, from a node that was sending
 *   at the same time, is not lost: it is acknowledged and kept, the
 *   transmission is reported as not acknowledged, and the frame is indicated
 *   when the upper layer sets the radio to RX again
 * - CCA by energy detection (CCA mode 1), on request and before a
 *   transmission. While the receiver runs it is the RSSI of the receiver,
 *   read for the CCA duration, as the RF Core does it in its IEEE 802.15.4
 *   mode: RX is not stopped for it, so a frame whose preamble is the energy
 *   on the channel is still received. With the receiver off the RF Core
 *   senses the carrier with a command of its own and only transmits if the
 *   channel was clear for the CCA duration. IEEE Std 802.15.4-2024, 11.2.8,
 *   does not recommend CCA mode 1 for the SUN FSK PHY when the channel
 *   spacing is below twice the symbol rate, and the driver has no other
 *   mode in this band
 * - CSMA-CA before a transmission, with the backoff exponents and the number
 *   of retries set with `set_csma_params` of the radio HAL (0 retries: a
 *   single CCA, a negative number: none). The radio listens during the
 *   backoff, and a frame that comes in then is acknowledged and kept like
 *   one that comes instead of an ACK
 * - A frame that is being received is not interrupted by a CCA, by a
 *   transmission with CCA or by a request to go idle that is not forced:
 *   they report a busy channel or radio. The same holds after the frame
 *   until its ACK has been sent
 * - LQI, derived from the RSSI of the frame: 0 at -100 dBm or less, 255 at
 *   -20 dBm or more
 * - TX power from the PA table of the board (`rf_conf.h`), compensated for
 *   temperature by the RF Core, see CC26X2_CC13X2_RF_PA_ENTRY(). Below
 *   25 degrees Celsius this takes some dB from the lowest powers, and
 *   -15 dBm then comes out as -20 dBm
 * - The IEEE address of the device as long address of the network interface,
 *   see cc26x2_cc13x2_rf_get_eui64()
 * - Optionally the 20 dBm PA of the CC1352P, on the boards that have it
 *   matched for the sub-GHz band, see @ref CONFIG_CC26X2_CC13X2_RF_PA20
 *
 * Not implemented:
 *
 * - The retransmissions, which the SubMAC does
 * - 4-FSK, FEC and mode switch
 * - The 2.4 GHz band of the CC1352
 *
 * ## Tested hardware
 *
 * LAUNCHXL-CC1312R1 and LAUNCHXL-CC1352P1, between them and with an AT86RF215
 * running RIOT. The 20 dBm PA was tried on the LAUNCHXL-CC1352P1.
 *
 * The LAUNCHXL-CC1352R1 and the LAUNCHXL-CC1352P-2 have the same radio and
 * the driver builds for them, but nobody has tried it on those boards yet.
 *
 * @{
 *
 * @file
 * @brief           Interface of the CC13x2 IEEE 802.15.4 radio driver
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Francisco Acosta <fco.ja.ac@gmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include "net/eui64.h"
#include "net/ieee802154/radio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Hardware timer of the sub-GHz driver
 *
 * The sub-GHz driver needs a timer of its own, for the delay before an ACK
 * frame and for the interframe spacing. A board can choose another one in
 * its configuration.
 */
#ifndef CC26X2_CC13X2_RF_TIMER
#  define CC26X2_CC13X2_RF_TIMER TIMER_DEV(2)
#endif

/**
 * @brief   Initialize radio hardware (RF Core)
 */
void cc26x2_cc13x2_rf_init(void);

/**
 * @brief   Set the radio up as a device of the IEEE 802.15.4 radio HAL
 *
 * @note    Call this function before @ref cc26x2_cc13x2_rf_init.
 *
 * @param[in] hal  pointer to the HAL descriptor associated to the device.
 */
void cc26x2_cc13x2_rf_hal_setup(ieee802154_dev_t *hal);

/**
 * @brief   Get the IEEE address (EUI-64) of the device
 *
 * It is the custom address of the customer configuration (CCFG) if one is
 * programmed there, and otherwise the address that TI programs at
 * production.
 *
 * The LaunchPads offer it as EUI-64 provider of the radio (see
 * @ref net_eui_provider), so it is the long address of the network
 * interface.
 *
 * @param[out]  addr    the address
 */
void cc26x2_cc13x2_rf_get_eui64(eui64_t *addr);

#ifdef __cplusplus
} /* end extern "C" */
#endif

/** @} */
