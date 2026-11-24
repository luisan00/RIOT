/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         boards_cc1352_launchpad
 * @{
 *
 * @file
 * @brief           Radio configuration of the board: PA tables
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include "kernel_defines.h"
#include "cc26x2_cc13x2_rf_params.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !IS_USED(MODULE_CC26X2_CC13X2_RF_24GHZ) || defined(DOXYGEN)
/**
 * @name   PA table for the sub-GHz band
 *
 * The standard PA gives up to 13 dBm in this band, and 14 dBm with the higher
 * VDDR voltage. The values are TI's for the RF design `CC1352EM-XD7793-XD24`,
 * 770 to 930 MHz (SimpleLink SDK 8.33.00.16,
 * `radioconfig/.meta/config/pa_tables/cc13x2_cc26x2_r3/pasettings.json`),
 * which also has -3 and -7 dBm.
 * @{
 */
/**
 * @brief   PA table: TX power in dBm and the PA setting that gives it
 */
static cc26x2_cc13x2_rf_pa_t cc26x2_cc13x2_rf_patable[] =
{
    /* 14 dBm needs the higher VDDR voltage, which the MCU only uses when
     * the CCFG is flashed with that option */
#if IS_USED(CONFIG_CC26XX_CC13XX_FORCE_VDDR_HH)
    { .dbm = 14, .val = CC26X2_CC13X2_RF_PA_ENTRY(63, 0, 1, 0) },
#endif
    /* 12.5 dBm in TI's table */
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
#endif

#if IS_USED(MODULE_CC26X2_CC13X2_RF_24GHZ) || defined(DOXYGEN)
/**
 * @name   PA table for the 2.4 GHz band
 *
 * The standard PA gives up to 5 dBm in this band. The values are TI's for
 * the RF design `CC1352EM-XD7793-XD24`, 2360 to 2510 MHz (SimpleLink SDK 8.33.00.16,
 * `radioconfig/.meta/config/pa_tables/cc13x2_cc26x2_r3/pasettings.json`).
 * @{
 */
/**
 * @brief   PA table of the 2.4 GHz band: TX power in dBm and its setting
 */
static const cc26x2_cc13x2_rf_pa_t cc26x2_cc13x2_rf_patable_24ghz[] =
{
    { .dbm = 5, .val = 0x7217 },
    { .dbm = 4, .val = 0x4E63 },
    { .dbm = 3, .val = 0x385D },
    { .dbm = 2, .val = 0x3259 },
    { .dbm = 1, .val = 0x2856 },
    { .dbm = 0, .val = 0x2853 },
    { .dbm = -3, .val = 0x1893 },
    { .dbm = -5, .val = 0x12D6 },
    { .dbm = -6, .val = 0x10D4 },
    { .dbm = -9, .val = 0x0AD0 },
    { .dbm = -10, .val = 0x0ACF },
    { .dbm = -12, .val = 0x0ACC },
    { .dbm = -15, .val = 0x06CA },
    { .dbm = -18, .val = 0x06C8 },
    { .dbm = -20, .val = 0x04C6 },
};
/**
 * @brief   Number of entries of the PA table of the 2.4 GHz band
 */
#define CC26X2_CC13X2_PA_TABLE_24GHZ_NUMOF ARRAY_SIZE(cc26x2_cc13x2_rf_patable_24ghz)
/** @} */
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

/** @} */
