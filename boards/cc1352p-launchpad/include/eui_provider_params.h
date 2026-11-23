/*
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     boards_cc1352p_launchpad
 * @{
 *
 * @file
 * @brief       EUI providers found on the board
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#include "cc26x2_cc13x2_rf.h"
#include "modules.h"

#ifdef __cplusplus
extern "C" {
#endif

#if IS_USED(MODULE_CC26X2_CC13X2_RF) || defined(DOXYGEN)
/**
 * @brief   The MCU holds an IEEE address, programmed at production
 */
static inline int _cc26x2_cc13x2_get_eui64(uint8_t index, eui64_t *addr)
{
    (void)index;

    cc26x2_cc13x2_rf_get_eui64(addr);
    return 0;
}

/**
 * @name    EUI sources on the board
 *          IEEE address of the MCU for the sub-GHz radio
 * @{
 */
#define EUI64_PROVIDER_FUNC   _cc26x2_cc13x2_get_eui64
#define EUI64_PROVIDER_TYPE   NETDEV_CC26XX_CC13XX
#define EUI64_PROVIDER_INDEX  0
/** @} */
#endif

#ifdef __cplusplus
}
#endif

/** @} */
