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
 * @brief       Configuration of SAUL PWM channels
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#ifndef PWM_PARAMS_H
#define PWM_PARAMS_H

#include "board.h"
#include "saul/periph.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief    No individual LED that needs dimming
 */
#define SAUL_PWM_NO_DIMMER

/**
 * @brief    The on-board RGB LED
 */
static const saul_pwm_rgb_params_t saul_pwm_rgb_params[] =
{
    {
        .name  = "RGB",
        .channels = {
            { PWM_DEV(0), 0, SAUL_PWM_INVERTED },
            { PWM_DEV(0), 1, SAUL_PWM_INVERTED },
            { PWM_DEV(0), 2, SAUL_PWM_INVERTED }
        }
    }
};

#ifdef __cplusplus
}
#endif

#endif /* PWM_PARAMS_H */
/** @} */
