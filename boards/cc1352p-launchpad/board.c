/*
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     boards_cc1352p_launchpad
 * @{
 *
 * @file
 * @brief       Board specific implementations for the TI CC1352P LaunchPad
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include "board.h"

#include "periph/gpio.h"

/**
 * @name    On-board SKY13317 RF switch control
 *
 * The CC1352P LaunchPad uses DIO28, DIO29 and DIO30 to select the active RF
 * path. The current driver only supports the standard-power sub-GHz path:
 *
 * DIO28 DIO29 DIO30 | RF path
 * ------ ------ ------+--------------------------
 *   0      0      1   | sub-GHz standard PA
 *
 * Checked against the SimpleLink CC13xx/CC26xx SDK 8.33.00.16:
 *
 * - `source/ti/boards/.meta/CC1352P1_LAUNCHXL.syscfg.json` describes the
 *   SKY13317-373LF SP3T antenna switch and its control signals: 2.4 GHz on
 *   DIO28, 20 dBm PA on DIO29 and sub-1 GHz on DIO30.
 * - `source/ti/devices/radioconfig/.meta/config/cc1352p/targets/target_launchxl-cc1352p.json`
 *   gives the levels for the sub-1 GHz standard PA, the same in TX and RX:
 *   DIO28 low, DIO29 low, DIO30 high. In idle TI leaves the three pins as
 *   tristate inputs; here they stay driven.
 *
 * @{
 */
#define BOARD_RF_SWITCH_24G     GPIO_PIN(0, 28)
#define BOARD_RF_SWITCH_PA      GPIO_PIN(0, 29)
#define BOARD_RF_SWITCH_SUBGHZ  GPIO_PIN(0, 30)
/** @} */

void board_rf_switch_enable_subghz(void)
{
    gpio_clear(BOARD_RF_SWITCH_24G);
    gpio_clear(BOARD_RF_SWITCH_PA);
    gpio_set(BOARD_RF_SWITCH_SUBGHZ);
}

void board_init(void)
{
    gpio_init(BOARD_RF_SWITCH_24G, GPIO_OUT);
    gpio_init(BOARD_RF_SWITCH_PA, GPIO_OUT);
    gpio_init(BOARD_RF_SWITCH_SUBGHZ, GPIO_OUT);

    board_rf_switch_enable_subghz();
}
