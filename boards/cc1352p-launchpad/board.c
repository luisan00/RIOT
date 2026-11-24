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
 * The sub-GHz radio driver uses the first one, in TX and RX. With the 20 dBm
 * PA it uses the second one in TX and the first one in RX; the RF Core then
 * moves the switch itself, see board_rf_switch_subghz(). The 2.4 GHz driver
 * uses the third one, see board_rf_switch_24ghz().
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
 * - `source/ti/drivers/.meta/rf/RF.Board.c.xdt`, the antenna switching
 *   callback TI generates for this switch: with the 20 dBm PA in the sub-GHz
 *   band it routes two signals of the RF Core to the pins, RFC_GPO3 to DIO29
 *   and RFC_GPO0 (LNA enable) to DIO30, and keeps DIO28 low. RFC_GPO3 is used
 *   instead of RFC_GPO1 (PA enable) because, in TI's words, the latter "is
 *   sometimes not de-asserted on CC1352 Rev A".
 *
 * @{
 */
#define BOARD_RF_SWITCH_24G     GPIO_PIN(0, 28)
#define BOARD_RF_SWITCH_PA      GPIO_PIN(0, 29)
#define BOARD_RF_SWITCH_SUBGHZ  GPIO_PIN(0, 30)
/** @} */

void board_rf_switch_subghz(bool pa20)
{
    gpio_clear(BOARD_RF_SWITCH_24G);

    if (pa20 && BOARD_CC1352P_LAUNCHPAD_PA20_SUBGHZ) {
        /* the RF Core drives both pins: the output is enabled by the IOC
         * port function, as for any other peripheral output */
        IOC->CFG[BOARD_RF_SWITCH_PA] = IOCFG_PORTID_RFC_GPO3;
        IOC->CFG[BOARD_RF_SWITCH_SUBGHZ] = IOCFG_PORTID_RFC_GPO0;
    }
    else {
        gpio_init(BOARD_RF_SWITCH_PA, GPIO_OUT);
        gpio_init(BOARD_RF_SWITCH_SUBGHZ, GPIO_OUT);
        gpio_clear(BOARD_RF_SWITCH_PA);
        gpio_set(BOARD_RF_SWITCH_SUBGHZ);
    }
}

void board_rf_switch_24ghz(void)
{
    gpio_init(BOARD_RF_SWITCH_PA, GPIO_OUT);
    gpio_init(BOARD_RF_SWITCH_SUBGHZ, GPIO_OUT);
    gpio_clear(BOARD_RF_SWITCH_PA);
    gpio_clear(BOARD_RF_SWITCH_SUBGHZ);
    gpio_set(BOARD_RF_SWITCH_24G);
}

void board_init(void)
{
    gpio_init(BOARD_RF_SWITCH_24G, GPIO_OUT);

    board_rf_switch_subghz(false);
}
