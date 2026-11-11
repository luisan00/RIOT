/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     drivers_nt35510
 * @{
 *
 * @file
 * @brief       Default configuration for the NT35510 panel controller
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#include "board.h"
#include "nt35510.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    Default configuration parameters
 * @{
 */
#ifndef NT35510_PARAM_RST_PIN
/** Panel reset pin (low active) */
#  define NT35510_PARAM_RST_PIN       GPIO_UNDEF
#endif
#ifndef NT35510_PARAM_BRIGHTNESS
/** Initial brightness */
#  define NT35510_PARAM_BRIGHTNESS    (0x7f)
#endif
#ifndef NT35510_PARAM_LANDSCAPE
/** Landscape orientation, following the LTDC resolution */
#  define NT35510_PARAM_LANDSCAPE     (LCD_SCREEN_WIDTH >= LCD_SCREEN_HEIGHT)
#endif
#ifndef NT35510_PARAMS
/** Default params */
#  define NT35510_PARAMS              { .rst_pin = NT35510_PARAM_RST_PIN, \
                                        .brightness = NT35510_PARAM_BRIGHTNESS, \
                                        .landscape = NT35510_PARAM_LANDSCAPE }
#endif
/** @} */

/**
 * @brief   Configuration struct
 */
static const nt35510_params_t nt35510_params[] =
{
    NT35510_PARAMS,
};

#ifdef __cplusplus
}
#endif

/** @} */
