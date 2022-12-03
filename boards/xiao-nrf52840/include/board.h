/*
 * Copyright (C) 2022 Luis A. Ruiz
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     boards_xiao-nrf52840
 * @{
 *
 * @file
 * @brief       Board specific configuration for the Xiao nRF52840
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#ifndef BOARD_H
#define BOARD_H

#include "board_common.h"
#include "cpu.h"
#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name USB configuration
 * @{
 */
#define INTERNAL_PERIPHERAL_VID (0x2886)
#define INTERNAL_PERIPHERAL_PID (0x8044)
#define CONFIG_USB_MANUF_STR "Seeed"
#define CONFIG_USB_PRODUCT_STR "XIAO nRF52840"
/** @} */

/**
 * @name    LED pin configuration
 * @{
 */
#define LED0_PIN GPIO_PIN(0, 26) /*!> Red LED */
#define LED1_PIN GPIO_PIN(0, 30) /*!> Green LED*/
#define LED2_PIN GPIO_PIN(0, 6)  /*!> Blue LED */
#define LED3_PIN GPIO_PIN(0, 14) /*!> Read Bat LED */
/** @} */

/**
 * @name    LED access macros
 * @{
 */
#define LED0_ON gpio_clear(LED0_PIN)      /*!> On */
#define LED0_OFF gpio_set(LED0_PIN)       /*!> Off */
#define LED0_TOGGLE gpio_toggle(LED0_PIN) /*!> Toggle */

#define LED1_ON gpio_clear(LED1_PIN)      /*!> On */
#define LED1_OFF gpio_set(LED1_PIN)       /*!> Off */
#define LED1_TOGGLE gpio_toggle(LED1_PIN) /*!> Toggle */

#define LED2_ON gpio_clear(LED2_PIN)      /*!> On */
#define LED2_OFF gpio_set(LED2_PIN)       /*!> Off */
#define LED2_TOGGLE gpio_toggle(LED2_PIN) /*!> Toggle */

#define LED3_ON gpio_clear(LED3_PIN)      /*!> On */
#define LED3_OFF gpio_set(LED3_PIN)       /*!> Off */
#define LED3_TOGGLE gpio_toggle(LED3_PIN) /*!> Toggle */
/** @} */

/**
 * @name    Button pin configuration
 * @todo ** Not Tested **
 * @{
 */
#define BTN0_PIN GPIO_PIN(0, 18)
#define BTN0_MODE GPIO_IN_PU
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
/** @} */
