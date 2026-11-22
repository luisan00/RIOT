/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         boards_cc1352p_launchpad
 * @{
 *
 * @file
 * @brief           Board specific definitions for TI CC1352P LaunchPad
 *
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stdbool.h>

#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    xtimer configuration
 * @{
 */
#define XTIMER_WIDTH        (16)
#define XTIMER_BACKOFF      (25)
#define XTIMER_ISR_BACKOFF  (20)
/** @} */

/**
 * @name    On-board button configuration
 * @{
 */
#define BTN0_PIN            GPIO_PIN(0, 15)
#define BTN0_MODE           GPIO_IN_PU

#define BTN1_PIN            GPIO_PIN(0, 14)
#define BTN1_MODE           GPIO_IN_PU
/** @} */

/**
 * @brief   On-board LED configuration and controlling
 * @{
 */
#define LED0_PIN            GPIO_PIN(0, 6)          /**< red   */
#define LED1_PIN            GPIO_PIN(0, 7)          /**< green */

#define LED0_ON             gpio_set(LED0_PIN)
#define LED0_OFF            gpio_clear(LED0_PIN)
#define LED0_TOGGLE         gpio_toggle(LED0_PIN)

#define LED1_ON             gpio_set(LED1_PIN)
#define LED1_OFF            gpio_clear(LED1_PIN)
#define LED1_TOGGLE         gpio_toggle(LED1_PIN)
/** @} */

/**
 * @brief   Whether the 20 dBm PA of this LaunchPad is for the sub-GHz band
 *
 * 1 on the LAUNCHXL-CC1352P1, 0 on the LAUNCHXL-CC1352P-2, whose 20 dBm PA
 * is for 2.4 GHz. Set by the build system from `BOARD_VERSION`.
 */
#ifndef BOARD_CC1352P_LAUNCHPAD_PA20_SUBGHZ
#  define BOARD_CC1352P_LAUNCHPAD_PA20_SUBGHZ   1
#endif

/**
 * @brief   Set the on-board RF switch for the sub-GHz band
 *
 * @param[in] pa20  false: standard PA, the switch stays on the sub-GHz path.
 *                  true: 20 dBm PA, the RF Core moves the switch between the
 *                  output of that PA in TX and the sub-GHz path in RX. Only
 *                  with #BOARD_CC1352P_LAUNCHPAD_PA20_SUBGHZ.
 */
void board_rf_switch_subghz(bool pa20);

/**
 * @brief   Board hook of the radio driver, called when the radio is set up
 *          and when it changes between the standard and the 20 dBm PA
 */
#define CC26X2_CC13X2_RF_FRONTEND_SETUP(pa20) board_rf_switch_subghz(pa20)

#ifdef __cplusplus
}
#endif

/** @} */
