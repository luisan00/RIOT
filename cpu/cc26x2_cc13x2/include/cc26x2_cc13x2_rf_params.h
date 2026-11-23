/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         cpu_cc26x2_cc13x2_rf
 * @{
 *
 * @file
 * @brief           CC26x2/CC13x2 IEEE 802.15.4 netdev driver
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Value of a PA table entry (`txPower` of the setup command)
 *
 * @param[in] bias          IB, PA bias setting (bits 0 to 5)
 * @param[in] gain          GC, PA gain setting (bits 6 and 7)
 * @param[in] boost         PA boost bit (bit 8)
 * @param[in] coefficient   temperature coefficient (bits 9 to 15)
 *
 * The RF Core compensates the TX power for temperature by itself: every
 * time a transmission starts it uses the bias
 * `IB + (T - 25) * coefficient / 256`, with the temperature `T` of the chip
 * in degrees Celsius (SWCU185G, 25.3.3.2.16). It takes the temperature from
 * the battery and temperature monitor of the MCU, which is on from boot and
 * must be left on (SWCU185G, 20.1). The values of a PA table come with the
 * coefficient TI gives for each power.
 */
#define CC26X2_CC13X2_RF_PA_ENTRY(bias, gain, boost, coefficient) \
    (((bias) << 0) | ((gain) << 6) | ((boost) << 8) | ((coefficient) << 9))

/**
 * @brief   RF PA table entry
 */
typedef struct {
    int16_t dbm;  /**< PA entry value in dBm */
    uint16_t val; /**< Coded value for the PA entry */
} cc26x2_cc13x2_rf_pa_t;

/**
 * @brief   Entry of the PA table of the 20 dBm PA of the CC1352P
 */
typedef struct {
    int16_t dbm;  /**< PA entry value in dBm */
    uint32_t val; /**< Coded value for the PA entry */
} cc26x2_cc13x2_rf_pa20_t;

#ifdef __cplusplus
} /* end extern "C" */
#endif

/** @} */
