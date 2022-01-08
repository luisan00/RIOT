/*
 * Copyright (C) 2021 Luis A. Ruiz <luisan00@hotmail.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License v2.1. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @defgroup    boards_sipeed-longan-nano
 * @ingroup     boards
 * @brief       Support for the SeeedStudio Sipeed Longan Nano board
 * @{
 *
 * @file
 * @brief       Board specific definitions for the SeeedStudio 
 *              SeeedStudio Sipeed Longan Nano board
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "macros/units.h"

/**
 * @name    Xtimer configuration
 * @{
 */
#define XTIMER_HZ                   MHZ(1)
#define XTIMER_WIDTH                (16)
/** @} */

/**
 * @brief   Initialize board specific hardware
 */
void board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
/** @} */
