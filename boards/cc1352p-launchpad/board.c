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
 * The LAUNCHXL-CC1352P1 selects the RF path with DIO28, DIO29 and DIO30. It
 * has three paths. Two of them are for the 868/915 MHz band: the standard PA
 * and receive path, and the output of the 20 dBm PA, which on this LaunchPad
 * is matched for that band (on the CC1352P-2 and CC1352P-4 it is matched for
 * 2.4 GHz).
 *
 * DIO28 | DIO29 | DIO30 | RF path
 * ----- | ----- | ----- | -----------------------------------------
 *   0   |   0   |   1   | sub-GHz, standard PA (up to 14 dBm) and RX
 *   0   |   1   |   0   | sub-GHz, 20 dBm PA, TX only
 *   1   |   0   |   0   | 2.4 GHz
 *
 * The radio driver only uses the first one for now, in TX and RX.
 *
 * Checked against the SimpleLink CC13xx/CC26xx SDK 8.33.00.16:
 *
 * - `source/ti/boards/.meta/CC1352P1_LAUNCHXL.syscfg.json` describes the
 *   SKY13317-373LF SP3T antenna switch and its control signals: 2.4 GHz on
 *   DIO28, 20 dBm PA on DIO29 and sub-1 GHz on DIO30.
 * - The targets in `source/ti/devices/radioconfig/.meta/config/cc1352p/targets`
 *   give the levels and the matching of each LaunchPad:
 *   `target_launchxl-cc1352p.json` for the standard PA (DIO30 high in TX and
 *   RX), `target_launchxl-cc1352p_highPA.json` for the 20 dBm PA (DIO29 high
 *   in TX, DIO30 high in RX) and `target_launchxl-cc1352p_2_4_ghz.json` for
 *   2.4 GHz (DIO28 high). In idle TI leaves the three pins as tristate
 *   inputs; here they stay driven.
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
