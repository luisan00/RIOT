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
 * @name    SUN PHY frequency band identifiers
 *
 * @see IEEE Std 802.15.4-2024, Table 10-73 and Table 10-74. Identifier 11
 *      names the 920 MHz band a second time there. The regions are those of
 *      Annex F of the standard, where it lists the band.
 * @{
 */
#define IEEE802154_SUN_PHY_FREQ_169_MHZ     0   /**< 169.400-169.475 */
#define IEEE802154_SUN_PHY_FREQ_450_MHZ     1   /**< 450-470 */
#define IEEE802154_SUN_PHY_FREQ_470_MHZ     2   /**< 470-510 (China) */
#define IEEE802154_SUN_PHY_FREQ_780_MHZ     3   /**< 779-787 */
#define IEEE802154_SUN_PHY_FREQ_863_MHZ     4   /**< 863-870 (Europe) */
#define IEEE802154_SUN_PHY_FREQ_896_MHZ     5   /**< 896-901 */
#define IEEE802154_SUN_PHY_FREQ_901_MHZ     6   /**< 901-902 */
#define IEEE802154_SUN_PHY_FREQ_915_MHZ     7   /**< 902-928 (North America) */
#define IEEE802154_SUN_PHY_FREQ_917_MHZ     8   /**< 917-923.5 */
#define IEEE802154_SUN_PHY_FREQ_920_MHZ     9   /**< 920-928 (Japan) */
#define IEEE802154_SUN_PHY_FREQ_928_MHZ     10  /**< 928-960, non-contiguous */
#define IEEE802154_SUN_PHY_FREQ_1427_MHZ    12  /**< 1427-1518, non-contiguous */
#define IEEE802154_SUN_PHY_FREQ_2450_MHZ    13  /**< 2400-2483.5 */
#define IEEE802154_SUN_PHY_FREQ_866_MHZ     14  /**< 865-868 (India) */
#define IEEE802154_SUN_PHY_FREQ_870_MHZ     15  /**< 870-876 (Europe) */
#define IEEE802154_SUN_PHY_FREQ_915_A_MHZ   16  /**< 902-928, alternate (North
                                                     America and Mexico) */
#define IEEE802154_SUN_PHY_FREQ_915_B_MHZ   17  /**< 902-907.5 and 915-928
                                                     (Brazil) */
#define IEEE802154_SUN_PHY_FREQ_915_C_MHZ   18  /**< 915-928 (Australia and New
                                                     Zealand) */
#define IEEE802154_SUN_PHY_FREQ_915_D_MHZ   19  /**< 915-921 (Europe) */
#define IEEE802154_SUN_PHY_FREQ_915_E_MHZ   20  /**< 915-918 (Philippines) */
#define IEEE802154_SUN_PHY_FREQ_919_MHZ     21  /**< 919-923 (Malaysia) */
#define IEEE802154_SUN_PHY_FREQ_920_A_MHZ   22  /**< 920.5-924.5 (China) */
#define IEEE802154_SUN_PHY_FREQ_920_B_MHZ   23  /**< 920-925 (Hong Kong,
                                                     Singapore, Thailand and
                                                     Vietnam) */
#define IEEE802154_SUN_PHY_FREQ_867_MHZ     24  /**< 866-869 (Singapore) */
/** @} */

/**
 * @brief   The 863 MHz band with the channel plan of TI's 15.4-Stack and of
 *          RIOT's AT86RF215 driver
 *
 * A value for #CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ that is not a band
 * identifier of the standard. The plan is there for compatibility with those
 * radios and it is not the one the standard gives to the SUN FSK PHY in the
 * 863 MHz band:
 *
 * | Channel spacing | `IEEE802154_SUN_PHY_FREQ_863_MHZ` | This plan         |
 * |:--------------- |:--------------------------------- |:----------------- |
 * | 100 kHz         | 863.1 MHz, 0-68                   | not available     |
 * | 200 kHz         | 863.1 MHz, 0-34                   | 863.125 MHz, 0-33 |
 * | 400 kHz         | not available                     | 863.225 MHz, 0-16 |
 *
 * The channels of the two plans are 25 kHz apart, so a node with one and a
 * node with the other do not hear each other. The plan comes from
 * `ti154stack_rf_config_common.js` of the SimpleLink SDK and from
 * `drivers/at86rf215/at86rf215_fsk.c`.
 */
#define CC26X2_CC13X2_RF_FREQ_863_COMPAT    104

/**
 * @brief   SUN PHY frequency band used, one of the `IEEE802154_SUN_PHY_FREQ_*`
 *          identifiers
 *
 * The driver implements the 2-FSK modes of the MR-FSK PHY listed in
 * cc26x2_cc13x2_rf_set_fsk_mode(), with the radio settings that TI gives for
 * the 770 to 930 MHz RF front end of the CC1312R1, CC1352R1 and CC1352P1
 * LaunchPads. These bands have channel plans inside that range. Each plan is
 * given as the frequency of channel 0 and the channel numbers of the band,
 * for a channel spacing (IEEE Std 802.15.4-2024, Table 11-14):
 *
 * | Band  | 100 kHz           | 200 kHz                  | 400 kHz                  | 600 kHz           |
 * |:----- |:----------------- |:------------------------ |:------------------------ |:----------------- |
 * | 780   |                   | 779.2 MHz, 0-38          | 779.4 MHz, 0-18          |                   |
 * | 863   | 863.1 MHz, 0-68   | 863.1 MHz, 0-34          |                          |                   |
 * | 866   | 865.1 MHz, 0-28   | 865.1 MHz, 0-14          |                          |                   |
 * | 867   | 863.1 MHz, 30-58  | 863.1 MHz, 15-29         | 863.2 MHz, 8-14          |                   |
 * | 870   | 870.1 MHz, 0-58   | 870.2 MHz, 0-28          |                          |                   |
 * | 915   |                   | 902.2 MHz, 0-128         | 902.4 MHz, 0-63          |                   |
 * | 915-a |                   | 902.2 MHz, 0-128         | 902.4 MHz, 0-63          |                   |
 * | 915-b |                   | 902.2 MHz, 0-26, 65-128  | 902.4 MHz, 0-12, 32-63   |                   |
 * | 915-c |                   | 902.2 MHz, 65-128        | 902.4 MHz, 32-63         |                   |
 * | 915-d |                   | 902.2 MHz, 65-93         | 902.4 MHz, 32-46         |                   |
 * | 915-e |                   | 902.2 MHz, 65-78         | 902.4 MHz, 32-38         |                   |
 * | 917   |                   | 917.1 MHz, 0-31          | 917.3 MHz, 0-15          |                   |
 * | 919   |                   | 902.2 MHz, 85-103        | 902.4 MHz, 42-51         |                   |
 * | 920   |                   | 920.6 MHz, 0-37          | 920.9 MHz, 0-17          | 920.8 MHz, 0-11   |
 * | 920-a |                   | 902.2 MHz, 92-111        |                          |                   |
 * | 920-b |                   | 902.2 MHz, 90-113        | 902.4 MHz, 45-56         |                   |
 *
 * The 867 MHz band uses the channel numbers of the 863 MHz band, and the
 * 915 MHz-a to 915 MHz-e, 919 MHz, 920 MHz-a and 920 MHz-b bands those of the
 * 915 MHz band, so their first channel is not channel 0. The channel spacing
 * is selected with #CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ.
 *
 * For the 863 MHz band there is also a plan that is not in the standard, for
 * compatibility with other radios: #CC26X2_CC13X2_RF_FREQ_863_COMPAT.
 *
 * The other bands are not available. The 169 MHz, 450 MHz and 470 MHz bands
 * are outside the range of the RF front end, the modes of the 896 MHz,
 * 901 MHz and 928 MHz bands are not implemented, and the 2450 MHz band uses
 * the 2.4 GHz RF path, which this driver does not handle.
 *
 * Pick the band that is allowed where the device is used: the driver does not
 * check any regulation, neither the band nor duty cycle or channel limits.
 *
 * The default is the 863 MHz band with the compatibility plan,
 * #CC26X2_CC13X2_RF_FREQ_863_COMPAT, so that a node with the default
 * configuration is on the frequency of a node with RIOT's AT86RF215 driver:
 * with the default channel spacing of this plan (400 kHz), symbol rate
 * (200 ksym/s), modulation index (1.0) and channel (5) of the stack, it is
 * on 865.225 MHz like that driver in its default MR-FSK configuration. The
 * AT86RF215 has to be set to the MR-FSK PHY and to 2-FSK for it, as its own
 * defaults are O-QPSK and 4-FSK. For the plan of the standard select
 * `IEEE802154_SUN_PHY_FREQ_863_MHZ`.
 *
 * With Kconfig the band is chosen in the "Sub-GHz frequency band" menu of
 * the CPU. Without it, define this macro, for example
 * `CFLAGS += -DCONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ=IEEE802154_SUN_PHY_FREQ_915_MHZ`.
 */
#ifndef CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ
#  define CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ   CC26X2_CC13X2_RF_FREQ_863_COMPAT
#endif

/**
 * @brief   Channel spacing in kHz: 100, 200, 400 or 600
 *
 * The standard ties the spacing to the operating mode of each band (IEEE Std
 * 802.15.4-2024, Table 11-14 with Tables 20-7, 20-8 and 20-10). The radio
 * HAL has no field for it, so the spacing is chosen at build time,
 * independent of the mode set with config_phy: pick the one of the mode that
 * is going to be used. These are the modes of the standard that the driver
 * implements, as symbol rate in ksym/s and modulation index:
 *
 * | Band                       | 100 kHz | 200 kHz                   | 400 kHz                   | 600 kHz          |
 * |:-------------------------- |:------- |:------------------------- |:------------------------- |:---------------- |
 * | 780                        |         | 50/1.0                    | 100/1.0                   |                  |
 * | 863, 866, 870              | 50/0.5  | 100/0.5, 150/0.5          |                           |                  |
 * | 867                        | 50/0.5  | 100/0.5, 150/0.5          | 200/0.5, 300/0.5          |                  |
 * | 915, 917                   |         | 50/1.0                    | 150/0.5, 200/0.5          |                  |
 * | 915-a to 915-e, 919, 920-b |         | 50/1.0, 100/0.5, 150/0.5  | 200/0.5, 300/0.5          |                  |
 * | 920                        |         | 50/1.0                    | 100/1.0, 150/0.5, 300/0.5 | 200/1.0, 300/0.5 |
 * | 920-a                      |         | 50/1.0, 100/0.5, 150/0.5  |                           |                  |
 *
 * The build fails if the band has no plan for the spacing.
 *
 * The default is 200 kHz, except for the compatibility plan
 * #CC26X2_CC13X2_RF_FREQ_863_COMPAT, where it is 400 kHz like in RIOT's
 * AT86RF215 driver. With Kconfig it is chosen in the "Sub-GHz channel
 * spacing" menu of the CPU.
 */
#ifndef CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ
#  if CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == CC26X2_CC13X2_RF_FREQ_863_COMPAT
#    define CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ (400U)
#  else
#    define CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ (200U)
#  endif
#endif

/**
 * @brief   Use the 20 dBm PA of the CC1352P for the TX powers above the
 *          table of the standard PA
 *
 * Only has effect on a board whose 20 dBm PA is matched for the sub-GHz band
 * and that provides a table for it in its `rf_conf.h`, like the
 * LAUNCHXL-CC1352P1. A TX power above the highest entry of the standard PA
 * table then selects the 20 dBm PA, up to 20 dBm.
 *
 * It is off by default: the driver does not check any regulation, and most
 * channels of most bands do not allow these powers.
 */
#ifdef DOXYGEN
#  define CONFIG_CC26X2_CC13X2_RF_PA20
#endif

/* TODO: teach the HAL about SUN PHY frequency bands? */
/**
 * @name    SUN PHY channel information
 *
 * The frequency of channel `n` is `CHAN_CENTER_FREQ0 + n * CHAN_SPACING`
 * (IEEE Std 802.15.4-2024, 11.1.3.9), and the channels of the band are
 * `CHAN_MIN` to `CHAN_MAX`, see cc26x2_cc13x2_rf_chan_is_valid(). The values
 * are those of Table 11-14 of the standard.
 *
 * `CC26X2_CC13X2_RF_CENTER_FREQ` is the center frequency of the band in MHz,
 * for the `centerFreq` field of CMD_PROP_RADIO_DIV_SETUP.
 *
 * `CC26X2_CC13X2_RF_LO_DIVIDER` is the `loDivider` field of that command. TI
 * derives it from the frequency: 5 from 863 MHz up and 6 from 719 MHz up
 * (`getLoDivider()` in `radioconfig/.meta/parameter_handler.js` of the SDK).
 * @{
 */
/** @brief   Channel spacing in Hz */
#define CC26X2_CC13X2_RF_CHAN_SPACING           KHZ(CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ)

#if CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_780_MHZ
/** @brief   Center frequency of the band in MHz */
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (783U)
/** @brief   LO divider of the band */
#  define CC26X2_CC13X2_RF_LO_DIVIDER           (6U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
/** @brief   Center frequency of channel 0 in Hz */
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(779200)
/** @brief   Lowest channel number of the band */
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
/** @brief   Highest channel number of the band */
#    define CC26X2_CC13X2_RF_CHAN_MAX           (38)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(779400)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (18)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_863_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (868U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 100
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (68)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (34)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == CC26X2_CC13X2_RF_FREQ_863_COMPAT
/* not the plan of the standard, see CC26X2_CC13X2_RF_FREQ_863_COMPAT */
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (868U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863125)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (33)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863225)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (16)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_866_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (867U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 100
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(865100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (28)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(865100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (14)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_867_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (868U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 100
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (30)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (58)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (15)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (29)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(863200)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (8)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (14)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_870_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (873U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 100
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(870100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (58)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(870200)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (28)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_917_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (920U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(917100)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (31)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(917300)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (15)
#  endif
#elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_920_MHZ
#  define CC26X2_CC13X2_RF_CENTER_FREQ          (924U)
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(920600)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (37)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(920900)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (17)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 600
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(920800)
#    define CC26X2_CC13X2_RF_CHAN_MIN           (0)
#    define CC26X2_CC13X2_RF_CHAN_MAX           (11)
#  endif
#else
/* The remaining bands use the channel numbers of the 915 MHz band */
#  if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(902200)
#  elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#    define CC26X2_CC13X2_RF_CHAN_CENTER_FREQ0  KHZ(902400)
#  endif
#  if (CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_915_MHZ) || \
      (CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_915_A_MHZ)
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (915U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (0)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (128)
#    elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#      define CC26X2_CC13X2_RF_CHAN_MIN         (0)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (63)
#    endif
#  elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_915_B_MHZ
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (915U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (0)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (128)
/** @brief   First channel of the part of the range that is not in the band */
#      define CC26X2_CC13X2_RF_CHAN_GAP_MIN     (27)
/** @brief   Last channel of the part of the range that is not in the band */
#      define CC26X2_CC13X2_RF_CHAN_GAP_MAX     (64)
#    elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#      define CC26X2_CC13X2_RF_CHAN_MIN         (0)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (63)
#      define CC26X2_CC13X2_RF_CHAN_GAP_MIN     (13)
#      define CC26X2_CC13X2_RF_CHAN_GAP_MAX     (31)
#    endif
#  elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_915_C_MHZ
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (922U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (65)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (128)
#    elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#      define CC26X2_CC13X2_RF_CHAN_MIN         (32)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (63)
#    endif
#  elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_915_D_MHZ
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (918U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (65)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (93)
#    elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#      define CC26X2_CC13X2_RF_CHAN_MIN         (32)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (46)
#    endif
#  elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_915_E_MHZ
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (917U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (65)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (78)
#    elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#      define CC26X2_CC13X2_RF_CHAN_MIN         (32)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (38)
#    endif
#  elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_919_MHZ
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (921U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (85)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (103)
#    elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#      define CC26X2_CC13X2_RF_CHAN_MIN         (42)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (51)
#    endif
#  elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_920_A_MHZ
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (923U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (92)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (111)
#    endif
#  elif CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ == IEEE802154_SUN_PHY_FREQ_920_B_MHZ
#    define CC26X2_CC13X2_RF_CENTER_FREQ        (923U)
#    if CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 200
#      define CC26X2_CC13X2_RF_CHAN_MIN         (90)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (113)
#    elif CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ == 400
#      define CC26X2_CC13X2_RF_CHAN_MIN         (45)
#      define CC26X2_CC13X2_RF_CHAN_MAX         (56)
#    endif
#  else
#    error "CONFIG_CC26X2_CC13X2_RF_SUN_PHY_FREQ: frequency band not supported"
#  endif
#endif

#ifndef CC26X2_CC13X2_RF_CHAN_MAX
#  error "CONFIG_CC26X2_CC13X2_RF_CHAN_SPACING_KHZ: the band has no plan for it"
#endif

#ifndef CC26X2_CC13X2_RF_LO_DIVIDER
/* every band from 863 MHz up */
#  define CC26X2_CC13X2_RF_LO_DIVIDER           (5U)
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
 *          RSSI which is all we need: the LQI is derived from it.
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
 * @brief   Check that a channel number is one of the selected band
 *
 * @param[in]   chan    channel number
 *
 * @return  true if the channel belongs to the band
 */
static inline bool cc26x2_cc13x2_rf_chan_is_valid(uint16_t chan)
{
    /* signed, as the lowest channel is 0 in most bands */
    const int num = chan;

    if ((num < CC26X2_CC13X2_RF_CHAN_MIN) || (num > CC26X2_CC13X2_RF_CHAN_MAX)) {
        return false;
    }
#ifdef CC26X2_CC13X2_RF_CHAN_GAP_MIN
    if ((num >= CC26X2_CC13X2_RF_CHAN_GAP_MIN) &&
        (num <= CC26X2_CC13X2_RF_CHAN_GAP_MAX)) {
        return false;
    }
#endif
    return true;
}

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
 * @brief   Switch the radio off
 *
 * Stops reception, powers the synthesizer down, stops the radio timer and
 * powers the RF Core off. cc26x2_cc13x2_rf_request_on() brings the radio
 * back with the same mode, channel and TX power.
 */
void cc26x2_cc13x2_rf_off(void);

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
 * @brief   Time to wait after a frame was received before the transmission
 *          of its ACK is started
 *
 * Chosen so that the ACK is on the air 1 ms after the end of the frame, the
 * AIFS of the SUN PHYs, in the current MR-FSK mode.
 *
 * @return  the delay in microseconds
 */
uint32_t cc26x2_cc13x2_rf_ack_delay_us(void);

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
