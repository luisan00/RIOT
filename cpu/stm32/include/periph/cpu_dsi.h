/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         cpu_stm32
 * @{
 *
 * @file
 * @brief           DSI host CPU specific definitions for the STM32 family
 *
 * The DSI host drives a MIPI DSI display panel from the LTDC output. This
 * driver supports the video burst mode with 24-bit color: the LTDC streams
 * frames continuously through the DSI wrapper, and DCS commands for the
 * panel initialization are transmitted in low-power mode between video
 * packets. The LTDC timings are taken from @ref ltdc_conf_t.
 *
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   DSI host configuration
 *
 * The DSI PLL output (the high-speed bit clock) is derived from the HSE:
 * F_PHY = (HSE / pll_idf) * pll_ndiv / (1 << pll_odf), and the lane byte
 * clock is F_PHY / 8.
 */
typedef struct {
    uint32_t rcc_mask;         /**< bit in the APB2 clock enable register */
    uint8_t pll_ndiv;          /**< PLL loop division factor [10..125] */
    uint8_t pll_idf;           /**< PLL input division factor [1..7] */
    uint8_t pll_odf;           /**< PLL output division factor, log2 [0..3] */
    uint8_t lanes;             /**< number of data lanes [1..2] */
    uint8_t channel;           /**< virtual channel ID */
    uint16_t max_read_time_us; /**< longest DCS read of the panel in us */
    uint32_t pixel_clk_khz;    /**< LTDC pixel clock in kHz, used to convert the
                                    horizontal timings to lane byte clock units */
} dsi_conf_t;

/**
 * @brief   Initialize the DSI host
 *
 * Powers the D-PHY regulator, locks the DSI PLL, configures the lanes and
 * the video mode timings from the board's @ref ltdc_conf_t. The host is
 * left stopped; call @ref dsi_start once the LTDC is initialized.
 *
 * @retval  0           on success
 * @retval  -ETIMEDOUT  the regulator, the PLL or the lanes did not get ready
 */
int dsi_init(void);

/**
 * @brief   Start the DSI host and its wrapper
 *
 * After this call the LTDC frames are streamed to the panel and DCS
 * commands are sent in low-power mode.
 */
void dsi_start(void);

/**
 * @brief   Stop the DSI host and its wrapper
 */
void dsi_stop(void);

/**
 * @brief   Send a DCS command to the panel
 *
 * Short write packets are used for up to one parameter, long write packets
 * otherwise. With video running, it is queued for the vertical blanking
 *
 * @param[in] cmd       DCS command code
 * @param[in] params    command parameters, may be NULL when @p len is 0
 * @param[in] len       number of parameters
 *
 * @retval  0           on success
 * @retval  -ETIMEDOUT  the command FIFO never drained
 */
int dsi_dcs_write(uint8_t cmd, const uint8_t *params, size_t len);

/**
 * @brief   Read from the panel with a DCS read command
 *
 * With video running, the call can busy-wait up to about one frame for a
 * gap of dsi_conf_t::max_read_time_us; with 0 a video line may be lost
 *
 * @param[in] cmd       DCS command code
 * @param[out] dest     destination buffer
 * @param[in] len       number of bytes to read, at least 1 and at most the
 *                      length of the answer to @p cmd
 *
 * @retval  0           on success
 * @retval  -ETIMEDOUT  the command FIFO never drained or the answer did not
 *                      arrive in time
 * @retval  -EIO        the panel did not answer the read request
 */
int dsi_dcs_read(uint8_t cmd, uint8_t *dest, size_t len);

#ifdef __cplusplus
}
#endif

/** @} */
