/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         boards_cc1312_launchpad
 * @{
 *
 * @file
 * @brief           RF configuration
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include "kernel_defines.h"
#include "cc26x2_cc13x2_rf_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name   CC26x2/CC13x2 PA table configuration
 * @{
 */
/**
 * @brief   PA table: TX power in dBm and the PA setting that gives it
 */
static cc26x2_cc13x2_rf_pa_t cc26x2_cc13x2_rf_patable[] =
{
    /* VDDR high allows 14 dBm transmit power, VDDR high setting is only
     * enabled if the CCFG is flashed */
#if IS_USED(CONFIG_CC26XX_CC13XX_FORCE_VDDR_HH)
    { .dbm = 14, .val = CC26X2_CC13X2_RF_PA_ENTRY(63, 0, 1, 0) },
#endif
    /* 12.5 rounded to 13 */
    { .dbm = 13, .val = CC26X2_CC13X2_RF_PA_ENTRY(36, 0, 0, 89) },
    { .dbm = 12, .val = CC26X2_CC13X2_RF_PA_ENTRY(16, 0, 0, 82) },
    { .dbm = 11, .val = CC26X2_CC13X2_RF_PA_ENTRY(26, 2, 0, 51) },
    { .dbm = 10, .val = CC26X2_CC13X2_RF_PA_ENTRY(18, 2, 0, 31) },
    { .dbm = 9, .val = CC26X2_CC13X2_RF_PA_ENTRY(28, 3, 0, 31) },
    { .dbm = 8, .val = CC26X2_CC13X2_RF_PA_ENTRY(24, 3, 0, 22) },
    { .dbm = 7, .val = CC26X2_CC13X2_RF_PA_ENTRY(20, 3, 0, 19) },
    { .dbm = 6, .val = CC26X2_CC13X2_RF_PA_ENTRY(17, 3, 0, 16) },
    { .dbm = 5, .val = CC26X2_CC13X2_RF_PA_ENTRY(14, 3, 0, 14) },
    { .dbm = 4, .val = CC26X2_CC13X2_RF_PA_ENTRY(13, 3, 0, 11) },
    { .dbm = 3, .val = CC26X2_CC13X2_RF_PA_ENTRY(11, 3, 0, 10) },
    { .dbm = 2, .val = CC26X2_CC13X2_RF_PA_ENTRY(10, 3, 0, 9) },
    { .dbm = 1, .val = CC26X2_CC13X2_RF_PA_ENTRY(9, 3, 0, 9) },
    { .dbm = 0, .val = CC26X2_CC13X2_RF_PA_ENTRY(8, 3, 0, 8) },
    { .dbm = -5, .val = CC26X2_CC13X2_RF_PA_ENTRY(4, 3, 0, 5) },
    { .dbm = -10, .val = CC26X2_CC13X2_RF_PA_ENTRY(2, 3, 0, 5) },
    { .dbm = -15, .val = CC26X2_CC13X2_RF_PA_ENTRY(1, 3, 0, 3) },
    { .dbm = -20, .val = CC26X2_CC13X2_RF_PA_ENTRY(0, 3, 0, 2) },
};
/**
 * @brief   Number of entries of the PA table
 */
#define CC26X2_CC13X2_PA_TABLE_NUMOF ARRAY_SIZE(cc26x2_cc13x2_rf_patable)
/** @} */

#ifdef __cplusplus
} /* extern "C" */
#endif

/** @} */
