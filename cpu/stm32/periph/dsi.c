/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     cpu_stm32
 * @{
 *
 * @file
 * @brief       Low-level DSI host driver implementation
 *
 * The initialization sequences follow RM0410 and were cross-checked against
 * ST's STM32CubeF7 HAL.
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include "busy_wait.h"
#include "cpu.h"
#include "board.h"
#include "macros/math.h"
#include "macros/utils.h"
#include "periph_conf.h"
#include "periph_cpu.h"

#define ENABLE_DEBUG        0
#include "debug.h"

#ifndef LCD_SCREEN_WIDTH
#  error "periph_dsi: the board has to define LCD_SCREEN_WIDTH"
#endif

#ifndef LCD_SCREEN_HEIGHT
#  error "periph_dsi: the board has to define LCD_SCREEN_HEIGHT"
#endif

/* polling loop bound in iterations, well over one frame */
#define DSI_TIMEOUT_LOOPS           (CLOCK_CORECLOCK / 100)

/* the PLL lock flag is valid after 400 us, ST's HAL waits 1 ms */
#define DSI_PLL_LOCK_DELAY_US       (1000)

/* MIPI DSI data types used by this driver */
#define DSI_DCS_SHORT_WRITE         (0x05)  /* DCS short write, no parameter */
#define DSI_DCS_SHORT_WRITE_PARAM   (0x15)  /* DCS short write, one parameter */
#define DSI_DCS_LONG_WRITE          (0x39)  /* DCS long write */
#define DSI_DCS_READ                (0x06)  /* DCS read, no parameter */
#define DSI_MAX_RETURN_PKT_SIZE     (0x37)  /* set maximum return packet size */

/* VMCR video mode type: burst mode */
#define DSI_VID_MODE_BURST          (DSI_VMCR_VMT1)

/* color coding: 24-bit RGB888 */
#define DSI_COLOR_RGB888            (5U)

static int _wait_flag(const volatile uint32_t *reg, uint32_t mask, uint32_t set)
{
    for (uint32_t i = 0; i < DSI_TIMEOUT_LOOPS; i++) {
        if ((*reg & mask) == (set ? mask : 0)) {
            return 0;
        }
    }
    return -ETIMEDOUT;
}

static uint32_t _lane_byte_clk_khz(void)
{
    /* F_PHY = (HSE / IDF) * NDIV / (1 << ODF), lane byte clock = F_PHY / 8 */
    return ((CONFIG_CLOCK_HSE / KHZ(1)) / dsi_config.pll_idf) * dsi_config.pll_ndiv
           / (1UL << dsi_config.pll_odf) / 8;
}

static void _config_video_mode(void)
{
    const uint32_t lbc = _lane_byte_clk_khz();
    const uint32_t pclk = dsi_config.pixel_clk_khz;

    /* select video mode on host and wrapper */
    DSI->MCR &= ~DSI_MCR_CMDM;
    DSI->WCFGR &= ~DSI_WCFGR_DSIM;

    /* burst mode: one packet per line, no chunks, largest null packet */
    DSI->VMCR = (DSI->VMCR & ~DSI_VMCR_VMT) | DSI_VID_MODE_BURST;
    DSI->VPCR = LCD_SCREEN_WIDTH;
    DSI->VCCR = 0;
    DSI->VNPCR = 0xFFF;

    /* virtual channel of the LTDC traffic */
    DSI->LVCIDR = dsi_config.channel;

    /* control signal polarities: all active high */
    DSI->LPCR &= ~(DSI_LPCR_DEP | DSI_LPCR_VSP | DSI_LPCR_HSP);

    /* 24-bit color on host and wrapper */
    DSI->LCOLCR = (DSI->LCOLCR & ~DSI_LCOLCR_COLC) | DSI_COLOR_RGB888;
    DSI->WCFGR = (DSI->WCFGR & ~DSI_WCFGR_COLMUX) | (DSI_COLOR_RGB888 << 1);

    /* horizontal timings, converted from pixel clock to lane byte clock
     * units; vertical timings are in lines */
    DSI->VHSACR = (ltdc_config.hsync * lbc) / pclk;
    DSI->VHBPCR = (ltdc_config.hbp * lbc) / pclk;
    DSI->VLCR = ((LCD_SCREEN_WIDTH + ltdc_config.hsync + ltdc_config.hbp +
                  ltdc_config.hfp) * lbc) / pclk;
    DSI->VVSACR = ltdc_config.vsync;
    DSI->VVBPCR = ltdc_config.vbp;
    DSI->VVFPCR = ltdc_config.vfp;
    DSI->VVACR = LCD_SCREEN_HEIGHT;

    /* LP commands only in the vertical blanking, up to 16 bytes per line */
    DSI->VMCR |= DSI_VMCR_LPCE | DSI_VMCR_LPHFPE | DSI_VMCR_LPHBPE |
                 DSI_VMCR_LPVAE | DSI_VMCR_LPVFPE | DSI_VMCR_LPVBPE |
                 DSI_VMCR_LPVSAE;
    DSI->LPMCR = (16U << DSI_LPMCR_LPSIZE_Pos);
}

int dsi_init(void)
{
    DEBUG("[dsi] init\n");

    assert(dsi_config.pll_idf >= 1);
    assert(dsi_config.lanes >= 1 && dsi_config.lanes <= 2);

    periph_clk_en(APB2, dsi_config.rcc_mask);

    /* start from the reset state of the host and its wrapper */
    RCC->APB2RSTR |= RCC_APB2RSTR_DSIRST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_DSIRST;

    /* enable the D-PHY voltage regulator */
    DSI->WRPCR |= DSI_WRPCR_REGEN;
    if (_wait_flag(&DSI->WISR, DSI_WISR_RRS, 1) < 0) {
        DEBUG("[dsi] init: regulator not ready\n");
        return -ETIMEDOUT;
    }

    /* configure and enable the DSI PLL */
    DSI->WRPCR &= ~(DSI_WRPCR_PLL_NDIV | DSI_WRPCR_PLL_IDF | DSI_WRPCR_PLL_ODF);
    DSI->WRPCR |= (dsi_config.pll_ndiv << DSI_WRPCR_PLL_NDIV_Pos) |
                  (dsi_config.pll_idf << DSI_WRPCR_PLL_IDF_Pos) |
                  (dsi_config.pll_odf << DSI_WRPCR_PLL_ODF_Pos);
    DSI->WRPCR |= DSI_WRPCR_PLLEN;
    busy_wait_us(DSI_PLL_LOCK_DELAY_US);
    if (_wait_flag(&DSI->WISR, DSI_WISR_PLLLS, 1) < 0) {
        DEBUG("[dsi] init: PLL not locked\n");
        return -ETIMEDOUT;
    }

    /* enable the host to configure the D-PHY */
    DSI->CR |= DSI_CR_EN;

    /* TX escape clock: lane byte clock / div, at most 20 MHz; the dividers 0
     * and 1 stop the clock */
    uint32_t esc_div = MAX(DIV_ROUND_UP(_lane_byte_clk_khz(), 20000U), 2U);
    DSI->CCR = (DSI->CCR & ~DSI_CCR_TXECKDIV) | (esc_div & DSI_CCR_TXECKDIV);

    /* D-PHY clock and digital section enable */
    DSI->PCTLR |= DSI_PCTLR_DEN;
    DSI->PCTLR |= DSI_PCTLR_CKE;

    /* number of data lanes, then wait for the PHY to report them stopped */
    DSI->PCONFR = (DSI->PCONFR & ~DSI_PCONFR_NL) | (dsi_config.lanes - 1);
    uint32_t stopped = DSI_PSR_PSS0 | DSI_PSR_PSSC;
    if (dsi_config.lanes == 2) {
        stopped |= DSI_PSR_PSS1;
    }
    if (_wait_flag(&DSI->PSR, stopped, 1) < 0) {
        DEBUG("[dsi] init: lanes not in stop state\n");
        return -ETIMEDOUT;
    }

    /* high-speed mode bit period in 0.25 ns units:
     * UIX4 = 4000000 * IDF * (1 << ODF) / (HSE_kHz * NDIV) */
    uint32_t uix4 = (4000000U * dsi_config.pll_idf * (1UL << dsi_config.pll_odf))
                    / ((CONFIG_CLOCK_HSE / KHZ(1)) * dsi_config.pll_ndiv);
    DSI->WPCR[0] = (DSI->WPCR[0] & ~DSI_WPCR0_UIX4) | uix4;

    /* no error interrupts */
    DSI->IER[0] = 0;
    DSI->IER[1] = 0;

    /* the host stays off until dsi_start() */
    DSI->CR &= ~DSI_CR_EN;

    /* clock lane: high-speed, no automatic control */
    DSI->CLCR = (DSI->CLCR & ~(DSI_CLCR_DPCC | DSI_CLCR_ACR)) | DSI_CLCR_DPCC;

    /* acknowledge with bus-turnaround after read and write requests */
    DSI->PCR |= DSI_PCR_BTAE;

    /* reads start only in gaps of max_read_time_us, see dsi_dcs_read() */
    const uint32_t lbc = _lane_byte_clk_khz();
    assert(dsi_config.max_read_time_us <= DSI_DLTCR_MRD_TIME * 1000UL / lbc);
    DSI->DLTCR = (DSI->DLTCR & ~DSI_DLTCR_MRD_TIME) | (dsi_config.max_read_time_us * lbc / 1000);

    _config_video_mode();

    return 0;
}

void dsi_start(void)
{
    DSI->CR |= DSI_CR_EN;
    DSI->WCR |= DSI_WCR_DSIEN;
}

void dsi_stop(void)
{
    DSI->CR &= ~DSI_CR_EN;
    DSI->WCR &= ~DSI_WCR_DSIEN;
}

static int _wait_cmd_fifo_empty(void)
{
    return _wait_flag(&DSI->GPSR, DSI_GPSR_CMDFE, 1);
}

static void _packet_header(uint8_t type, uint8_t data0, uint8_t data1)
{
    DSI->GHCR = type | (dsi_config.channel << 6) | (data0 << 8) | (data1 << 16);
}

int dsi_dcs_write(uint8_t cmd, const uint8_t *params, size_t len)
{
    assert(params || (len == 0));

    if (_wait_cmd_fifo_empty() < 0) {
        return -ETIMEDOUT;
    }

    if (len == 0) {
        _packet_header(DSI_DCS_SHORT_WRITE, cmd, 0);
    }
    else if (len == 1) {
        _packet_header(DSI_DCS_SHORT_WRITE_PARAM, cmd, params[0]);
    }
    else {
        /* long packet: the command goes in the first payload byte */
        uint32_t word = cmd;
        size_t count = (len < 3) ? len : 3;
        for (size_t i = 0; i < count; i++) {
            word |= (uint32_t)params[i] << (8 * (i + 1));
        }
        DSI->GPDR = word;

        size_t sent = count;
        while (sent < len) {
            count = ((len - sent) < 4) ? (len - sent) : 4;
            word = 0;
            for (size_t i = 0; i < count; i++) {
                word |= (uint32_t)params[sent + i] << (8 * i);
            }
            DSI->GPDR = word;
            sent += count;
        }

        _packet_header(DSI_DCS_LONG_WRITE, (len + 1) & 0xff, ((len + 1) >> 8) & 0xff);
    }

    return 0;
}

int dsi_dcs_read(uint8_t cmd, uint8_t *dest, size_t len)
{
    assert(dest && (len > 0));

    /* the panel answers with a single byte until it is told a larger
     * maximum return packet size */
    if (len > 1) {
        if (_wait_cmd_fifo_empty() < 0) {
            return -ETIMEDOUT;
        }
        _packet_header(DSI_MAX_RETURN_PKT_SIZE, len & 0xff, (len >> 8) & 0xff);
    }

    if (_wait_cmd_fifo_empty() < 0) {
        return -ETIMEDOUT;
    }
    _packet_header(DSI_DCS_READ, cmd, 0);

    size_t received = 0;
    for (uint32_t i = 0; i < DSI_TIMEOUT_LOOPS && received < len; i++) {
        if (!(DSI->GPSR & DSI_GPSR_PRDFE)) {
            uint32_t word = DSI->GPDR;
            size_t count = ((len - received) < 4) ? (len - received) : 4;
            for (size_t j = 0; j < count; j++) {
                dest[received++] = word >> (8 * j);
            }
        }
        /* packet size error: the panel did not answer the read */
        if (!(DSI->GPSR & DSI_GPSR_RCB) && (DSI->ISR[1] & DSI_ISR1_PSE)) {
            return -EIO;
        }
    }

    return (received == len) ? 0 : -ETIMEDOUT;
}
