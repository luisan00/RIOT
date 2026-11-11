/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @defgroup    drivers_nt35510 NT35510 DSI display panel controller
 * @ingroup     drivers_display
 *
 * @brief       Driver for display panels based on the Novatek NT35510,
 *              connected through the STM32 DSI host
 *
 * The driver resets the panel, checks its ID, sends the initialization
 * sequence over DSI and switches the panel on. The frames themselves are
 * streamed continuously by the LTDC through the DSI host in video burst
 * mode, so the drawing interface is the LTDC @ref drivers_disp_dev driver.
 *
 * Supported:
 * - the 480x800 panel of ST's MB1166-A09 display board (STM32F769I-DISCO)
 * - 24-bit color and the brightness setting
 * - landscape orientation, verified on hardware; portrait is implemented
 *   but not tested yet
 *
 * Not supported yet:
 * - the DSI command mode
 * - the tearing effect output
 * - the sleep mode
 *
 * @note The initialization sequence holds the power settings of that panel,
 *       so other NT35510-based panels may need different values. Older
 *       MB1166 revisions carry an OTM8009A controller instead, which
 *       nt35510_init() rejects with -ENODEV after reading the panel ID.
 *
 * @{
 *
 * @file
 * @brief       Device driver interface for the NT35510 panel controller
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stdbool.h>
#include <stdint.h>

#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Device initialization parameters
 */
typedef struct {
    gpio_t rst_pin;         /**< reset pin (low active), GPIO_UNDEF when the
                                 panel reset is not connected */
    uint8_t brightness;     /**< initial brightness, 0 (lowest) to 255
                                 (highest) */
    bool landscape;         /**< landscape instead of the native portrait
                                 orientation, it has to match the LTDC
                                 resolution */
} nt35510_params_t;

/**
 * @brief   Device descriptor
 */
typedef struct {
    const nt35510_params_t *params;     /**< device initialization parameters */
} nt35510_t;

/**
 * @brief   Initialize the display: DSI host, LTDC and the panel itself
 *
 * @param[out] dev      device descriptor
 * @param[in]  params   device initialization parameters
 *
 * @retval  0           on success
 * @retval  -ENODEV     the panel on the DSI link is not an NT35510
 * @retval  -ETIMEDOUT  the DSI host did not get ready or a DSI transfer
 *                      timed out
 * @retval  -EIO        the panel did not answer on the DSI link
 */
int nt35510_init(nt35510_t *dev, const nt35510_params_t *params);

/**
 * @brief   Set the panel brightness
 *
 * @param[in] dev       device descriptor
 * @param[in] level     brightness, 0 (lowest) to 255 (highest)
 *
 * @retval  0           on success
 * @retval  -ETIMEDOUT  the DSI transfer timed out
 */
int nt35510_set_brightness(const nt35510_t *dev, uint8_t level);

#ifdef __cplusplus
}
#endif

/** @} */
