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
 * SUN FSK PHY between 770 and 930 MHz: 780 MHz, 863 MHz (the default),
 * 866 MHz, 867 MHz, 870 MHz, 915 MHz and its regional variants 915 MHz-a to
 * 915 MHz-e, 917 MHz, 919 MHz, 920 MHz, 920 MHz-a and 920 MHz-b. Channel
 * numbers and frequencies follow IEEE Std 802.15.4-2024. The 863 MHz band
 * also has a plan outside the standard, the one of TI's 15.4-Stack and of
 * RIOT's AT86RF215 driver, for compatibility with those radios: see
 * @ref CONFIG_CC26X2_CC13X2_RF_863_COMPAT.
 *
 * Some bands do not start at channel 0. The channel the stack starts on,
 * @ref CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL, has to be a channel of the
 * band: with the SubMAC the build fails if it is not.
 *
 * ## Features
 *
 * - Automatic transmission of ACK frames, address filtering, promiscuous mode
 * - CCA by energy detection
 * - TX power from the PA table of the board (`rf_conf.h`)
 * - Optionally the 20 dBm PA of the CC1352P, on the boards that have it
 *   matched for the sub-GHz band, see @ref CONFIG_CC26X2_CC13X2_RF_PA20
 *
 * Not implemented:
 *
 * - CSMA-CA and retransmissions in the radio (the SubMAC does both)
 * - LQI, which is reported as 0
 * - 4-FSK, FEC and mode switch
 * - The 2.4 GHz band of the CC1352
 *
 * The driver has been tested on the CC1352P1 LaunchPad only.
 *
 * @{
 *
 * @file
 * @brief           Interface of the CC13x2 sub-GHz IEEE 802.15.4g radio driver
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include "net/ieee802154/radio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   IEEE 802.15.4 radio timer configuration
 *
 * This radio relies on a dedicated hardware timer to maintain IFS,
 * the default timer may be overwritten in the board configuration.
 */
#ifndef CC26X2_CC13X2_RF_TIMER
#  define CC26X2_CC13X2_RF_TIMER TIMER_DEV(2)
#endif

/**
 * @brief   Initialize radio hardware (RF Core)
 */
void cc26x2_cc13x2_rf_init(void);

/**
 * @brief   Setup cc26x2_cc13x2 in order to be used with the IEEE 802.15.4 Radio HAL
 *
 * @note    This functions MUST be called before @ref cc26x2_cc13x2_rf_init.
 *
 * @param[in] hal  pointer to the HAL descriptor associated to the device.
 */
void cc26x2_cc13x2_rf_hal_setup(ieee802154_dev_t *hal);

#ifdef __cplusplus
} /* end extern "C" */
#endif

/** @} */
