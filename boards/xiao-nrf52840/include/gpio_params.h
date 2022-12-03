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
 * @brief       Configuration of SAUL mapped GPIO pins
 *
 * @author      Luis A. Ruiz <@hotmail.com>
 */

#ifndef GPIO_PARAMS_H
#define GPIO_PARAMS_H

#include "board.h"
#include "saul/periph.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief    LED and button configuration for SAUL
 */
static const saul_gpio_params_t saul_gpio_params[] = {
    #ifndef MODULE_SAUL_PWM
    {
        .name = "Red LED",
        .pin = LED0_PIN,
        .mode = GPIO_OUT,
        .flags = (SAUL_GPIO_INVERTED | SAUL_GPIO_INIT_CLEAR),
    },
    {
        .name = "Green LED",
        .pin = LED1_PIN,
        .mode = GPIO_OUT,
        .flags = (SAUL_GPIO_INVERTED | SAUL_GPIO_INIT_CLEAR),
    },
    {
        .name = "Blue LED",
        .pin = LED2_PIN,
        .mode = GPIO_OUT,
        .flags = (SAUL_GPIO_INVERTED | SAUL_GPIO_INIT_CLEAR),
    },
    #endif
    {
        .name = "Reset",
        .pin = BTN0_PIN,
        .mode = GPIO_IN_PU,
        .flags = SAUL_GPIO_INVERTED,
    },
};

#ifdef __cplusplus
}
#endif

#endif /* GPIO_PARAMS_H */
/** @} */
