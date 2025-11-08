/*
 * SPDX-FileCopyrightText: 2017 OTA keys S.A.
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     boards_stm32f769i-disco
 * @{
 *
 * @file
 * @brief       Peripheral MCU configuration for the STM32F769I-DISCO board
 *
 * @author      Vincent Dupont <vincent@otakeys.com>
 *              Luis A. Ruiz <luisan00@hotmail.com>
 */

/* This board provides an LSE */
#ifndef CONFIG_BOARD_HAS_LSE
#define CONFIG_BOARD_HAS_LSE    1
#endif

/* This board provides an HSE */
#ifndef CONFIG_BOARD_HAS_HSE
#define CONFIG_BOARD_HAS_HSE    1
#endif

/* The HSE provides a 25MHz clock */
#ifndef CONFIG_CLOCK_HSE
#define CONFIG_CLOCK_HSE               MHZ(25)
#endif

#include "periph_cpu.h"
#include "clk_conf.h"
#include "cfg_rtt_default.h"
#include "cfg_timer_tim2.h"
#include "cfg_usb_otg_fs.h"
#include "mii.h"

#ifdef __cplusplus
extern "C" {
#endif

// adc

// dac

/**
 * @name    DMA streams configuration
 * @{
 */
static const dma_conf_t dma_config[] = {
    { .stream = 15 }, /* DMA2 Stream 7 - USART1_TX */
    { .stream = 7 },  /* DMA1 Stream 7 - UART5_TX */
    { .stream = 14 }, /* DMA2 Stream 6 - USART6_TX */
    { .stream = 8 },  /* DMA2 Stream 0 - ETH_TX    */
};

#define DMA_0_ISR  isr_dma2_stream7
#define DMA_1_ISR  isr_dma1_stream7
#define DMA_2_ISR  isr_dma2_stream6
#define DMA_3_ISR  isr_dma2_stream0

#define DMA_NUMOF           ARRAY_SIZE(dma_config)
/** @} */


/**
 * @name i2c configurarion
 * 
 * 
 * | i2c | scl | sda |
 * |-----|-----|-----|
 * |  1  | PB8 | PB9 |
 * @{
 */
static const i2c_conf_t i2c_config[] = {
    {
        .dev = I2C1,
        .speed = I2C_SPEED_NORMAL,
        .scl_pin = GPIO_PIN(PORT_B, 8),
        .sda_pin = GPIO_PIN(PORT_B, 9),
        .scl_af = GPIO_AF4,
        .sda_af = GPIO_AF4,
        .bus = APB1,
        .rcc_mask = RCC_APB1ENR_I2C1EN,
        .rcc_sw_mask = RCC_DCKCFGR2_I2C1SEL_1,
        .irqn = I2C1_ER_IRQn,
    },
};
#define I2C_0_ISR           isr_i2c1_er
#define I2C_NUMOF           ARRAY_SIZE(i2c_config)
/** @} */

/**
 * @name    UART configuration
 * @{
 */
static const uart_conf_t uart_config[] = {
    { .dev = USART1,
      .rcc_mask = RCC_APB2ENR_USART1EN,
      .rx_pin = GPIO_PIN(PORT_A, 10),
      .tx_pin = GPIO_PIN(PORT_A, 9),
      .rx_af = GPIO_AF7,
      .tx_af = GPIO_AF7,
      .bus = APB2,
      .irqn = USART1_IRQn,
#ifdef MODULE_PERIPH_DMA
      .dma = 0,
      .dma_chan = 4
#endif
    },
    { .dev = UART5,
      .rcc_mask = RCC_APB1ENR_UART5EN,
      .rx_pin = GPIO_PIN(PORT_D, 2),    /* Pn CN2 */
      .tx_pin = GPIO_PIN(PORT_C, 12),   /* pn CN2 */
      .rx_af = GPIO_AF8,
      .tx_af = GPIO_AF8,
      .bus = APB1,
      .irqn = UART5_IRQn,
#ifdef MODULE_PERIPH_DMA
      .dma = 1,
      .dma_chan = 4
#endif
    },
    { .dev = USART6,
      .rcc_mask = RCC_APB2ENR_USART6EN,
      .rx_pin = GPIO_PIN(PORT_C, 7), /* Arduino connector */
      .tx_pin = GPIO_PIN(PORT_C, 6), /* Arduino connector */
      .rx_af = GPIO_AF8,
      .tx_af = GPIO_AF8,
      .bus = APB2,
      .irqn = USART6_IRQn,
#ifdef MODULE_PERIPH_DMA
      .dma = 1,
      .dma_chan = 5
#endif
    }
};

#define UART_0_ISR          (isr_usart1)
#define UART_0_DMA_ISR      (isr_dma2_stream7)
#define UART_1_ISR          (isr_uart5)
#define UART_1_DMA_ISR      (isr_dma1_stream7)
#define UART_2_ISR          (isr_usart6)
#define UART_2_DMA_ISR      (isr_dma2_stream6)
#define UART_NUMOF          ARRAY_SIZE(uart_config)
/** @} */

/**
 * @name ETH configuration
 * @{
 */
static const eth_conf_t eth_config = {
    .mode = RMII,
    .speed = MII_BMCR_SPEED_100 | MII_BMCR_FULL_DPLX,
    .dma = 7,
    .dma_chan = 8,
    .phy_addr = 0x00,
    .pins = {
        GPIO_PIN(PORT_G, 13),   /* TXD0 */
        GPIO_PIN(PORT_G, 14),   /* TXD1 */
        GPIO_PIN(PORT_G, 11),   /* TX_EN */
        GPIO_PIN(PORT_C, 4),    /* RXD0 */
        GPIO_PIN(PORT_C, 5),    /* RXD1 */
        GPIO_PIN(PORT_A, 7),    /* CRS_DV */
        GPIO_PIN(PORT_C, 1),    /* MDC */
        GPIO_PIN(PORT_A, 2),    /* MDIO */
        GPIO_PIN(PORT_A, 1),    /* REF_CLK */
    }
};

/**
 * @name    FMC configuration
 * @{
 */
/**
 * @brief   FMC controller configuration
 */
static const fmc_conf_t fmc_config = {
    .bus = AHB3,
    .rcc_mask = RCC_AHB3ENR_FMCEN,
#if MODULE_PERIPH_FMC_SDRAM
    .ba0_pin = { .pin = GPIO_PIN(PORT_G, 4), .af = GPIO_AF12, },     /* FMC_BA0 signal */
    .ba1_pin = { .pin = GPIO_PIN(PORT_G, 5), .af = GPIO_AF12, },     /* FMC_BA1 signal */
    .sdclk_pin = { .pin = GPIO_PIN(PORT_G, 8), .af = GPIO_AF12, },   /* FMC_SDCLK signal */
    .sdnwe_pin = { .pin = GPIO_PIN(PORT_H, 5), .af = GPIO_AF12, },   /* FMC_SDNWE signal */
    .sdnras_pin = { .pin = GPIO_PIN(PORT_F, 11), .af = GPIO_AF12, }, /* FMC_SDNRAS signal */
    .sdncas_pin = { .pin = GPIO_PIN(PORT_G, 15), .af = GPIO_AF12, }, /* FMC_SDNCAS signal */
    .sdcke0_pin = { .pin = GPIO_PIN(PORT_H, 2), .af = GPIO_AF12, },  /* FMC_SDCKE0 signal */
    .sdne0_pin = { .pin = GPIO_PIN(PORT_H, 3), .af = GPIO_AF12, },   /* FMC_SDNE0 signal */
    .addr = {
        { .pin = GPIO_PIN(PORT_F, 0), .af = GPIO_AF12, },  /* FMC_A0 signal */
        { .pin = GPIO_PIN(PORT_F, 1), .af = GPIO_AF12, },  /* FMC_A1 signal */
        { .pin = GPIO_PIN(PORT_F, 2), .af = GPIO_AF12, },  /* FMC_A2 signal */
        { .pin = GPIO_PIN(PORT_F, 3), .af = GPIO_AF12, },  /* FMC_A3 signal */
        { .pin = GPIO_PIN(PORT_F, 4), .af = GPIO_AF12, },  /* FMC_A4 signal */
        { .pin = GPIO_PIN(PORT_F, 5), .af = GPIO_AF12, },  /* FMC_A5 signal */
        { .pin = GPIO_PIN(PORT_F, 12), .af = GPIO_AF12, }, /* FMC_A6 signal */
        { .pin = GPIO_PIN(PORT_F, 13), .af = GPIO_AF12, }, /* FMC_A7 signal */
        { .pin = GPIO_PIN(PORT_F, 14), .af = GPIO_AF12, }, /* FMC_A8 signal */
        { .pin = GPIO_PIN(PORT_F, 15), .af = GPIO_AF12, }, /* FMC_A9 signal */
        { .pin = GPIO_PIN(PORT_G, 0), .af = GPIO_AF12, },  /* FMC_A10 signal */
        { .pin = GPIO_PIN(PORT_G, 1), .af = GPIO_AF12, },  /* FMC_A11 signal */
    },
#endif
    .data = {
        { .pin = GPIO_PIN(PORT_D, 14), .af = GPIO_AF12, }, /* FMC_D0 signal */
        { .pin = GPIO_PIN(PORT_D, 15), .af = GPIO_AF12, }, /* FMC_D1 signal */
        { .pin = GPIO_PIN(PORT_D, 0), .af = GPIO_AF12, },  /* FMC_D2 signal */
        { .pin = GPIO_PIN(PORT_D, 1), .af = GPIO_AF12, },  /* FMC_D3 signal */
        { .pin = GPIO_PIN(PORT_E, 7), .af = GPIO_AF12, },  /* FMC_D4 signal */
        { .pin = GPIO_PIN(PORT_E, 8), .af = GPIO_AF12, },  /* FMC_D5 signal */
        { .pin = GPIO_PIN(PORT_E, 9), .af = GPIO_AF12, },  /* FMC_D6 signal */
        { .pin = GPIO_PIN(PORT_E, 10), .af = GPIO_AF12, }, /* FMC_D7 signal */
#if MODULE_PERIPH_FMC_32BIT
        { .pin = GPIO_PIN(PORT_E, 11), .af = GPIO_AF12, }, /* FMC_D8 signal */
        { .pin = GPIO_PIN(PORT_E, 12), .af = GPIO_AF12, }, /* FMC_D9 signal */
        { .pin = GPIO_PIN(PORT_E, 13), .af = GPIO_AF12, }, /* FMC_D10 signal */
        { .pin = GPIO_PIN(PORT_E, 14), .af = GPIO_AF12, }, /* FMC_D11 signal */
        { .pin = GPIO_PIN(PORT_E, 15), .af = GPIO_AF12, }, /* FMC_D12 signal */
        { .pin = GPIO_PIN(PORT_D, 8), .af = GPIO_AF12, },  /* FMC_D13 signal */
        { .pin = GPIO_PIN(PORT_D, 9), .af = GPIO_AF12, },  /* FMC_D14 signal */
        { .pin = GPIO_PIN(PORT_D, 10), .af = GPIO_AF12, }, /* FMC_D15 signal */
        { .pin = GPIO_PIN(PORT_H, 8), .af = GPIO_AF12, },  /* FMC_D16 signal */
        { .pin = GPIO_PIN(PORT_H, 9), .af = GPIO_AF12, },  /* FMC_D17 signal */
        { .pin = GPIO_PIN(PORT_H, 10), .af = GPIO_AF12, }, /* FMC_D18 signal */
        { .pin = GPIO_PIN(PORT_H, 11), .af = GPIO_AF12, }, /* FMC_D19 signal */
        { .pin = GPIO_PIN(PORT_H, 12), .af = GPIO_AF12, }, /* FMC_D20 signal */
        { .pin = GPIO_PIN(PORT_H, 13), .af = GPIO_AF12, }, /* FMC_D21 signal */
        { .pin = GPIO_PIN(PORT_H, 14), .af = GPIO_AF12, }, /* FMC_D22 signal */
        { .pin = GPIO_PIN(PORT_H, 15), .af = GPIO_AF12, }, /* FMC_D23 signal */
        { .pin = GPIO_PIN(PORT_I, 0), .af = GPIO_AF12, },  /* FMC_D24 signal */
        { .pin = GPIO_PIN(PORT_I, 1), .af = GPIO_AF12, },  /* FMC_D25 signal */
        { .pin = GPIO_PIN(PORT_I, 2), .af = GPIO_AF12, },  /* FMC_D26 signal */
        { .pin = GPIO_PIN(PORT_I, 3), .af = GPIO_AF12, },  /* FMC_D27 signal */
        { .pin = GPIO_PIN(PORT_I, 6), .af = GPIO_AF12, },  /* FMC_D28 signal */
        { .pin = GPIO_PIN(PORT_I, 7), .af = GPIO_AF12, },  /* FMC_D29 signal */
        { .pin = GPIO_PIN(PORT_I, 9), .af = GPIO_AF12, },  /* FMC_D30 signal */
        { .pin = GPIO_PIN(PORT_I, 10), .af = GPIO_AF12, }, /* FMC_D31 signal */
#endif
    },
    .nbl0_pin = { .pin = GPIO_PIN(PORT_E, 0), .af = GPIO_AF12, },   /* FMC_NBL0 signal (DQM0) */
    .nbl1_pin = { .pin = GPIO_PIN(PORT_E, 1), .af = GPIO_AF12, },   /* FMC_NBL1 signal (DQM1) */
    .nbl2_pin = { .pin = GPIO_PIN(PORT_I, 4), .af = GPIO_AF12, },   /* FMC_NBL2 signal (DQM2) */
    .nbl3_pin = { .pin = GPIO_PIN(PORT_I, 5), .af = GPIO_AF12, },   /* FMC_NBL3 signal (DQM3) */
};

/**
 * @brief   FMC Bank configuration
 *
 * The board has a SDRAM MT48LC4M32B2B5-6A with 128 MBit on-board.
 * It is organized in 4 banks of 1M x 32 bits each and connected to bank 5
 * at address 0xc0000000.
 */
static const fmc_bank_conf_t fmc_bank_config[] = {
    /* bank 5 is used for SDRAM */
    {
        .bank = FMC_BANK_5,
        .mem_type = FMC_SDRAM,
        .data_width = FMC_BUS_WIDTH_32BIT,
        .address = 0xc0000000,               /* Bank 6 is mapped to 0xc0000000 */
        .size = MiB(16),                      /* Size in MByte, 4M x 32 Bit */
        .sdram = {
            .clk_period = 2,                 /* SDCLK = 2 x HCLK */
            .row_bits = 12,                  /* A11..A0 used for row address */
            .col_bits = 8,                   /* A7..A0 used for column address */
            .cas_latency = 2,                /* CAS latency is 2 clock cycles */
            .read_delay = 0,                 /* No read delay after CAS */
            .burst_read = true,              /* Burst read mode enabled */
            .burst_write = false,            /* Burst write mode disabled */
            .burst_len = FMC_BURST_LENGTH_1, /* Burst length is 1 */
            .burst_interleaved = false,      /* Burst mode interleaved */
            .write_protect = false,          /* No write protection */
            .four_banks = true,              /* SDRAM has four internal banks */
            .timing = {                      /* SDRAM Timing parameters */
                .row_to_col_delay = 2,       /* Row to column delay (2 clock cycles) */
                .row_precharge = 2,          /* Row precharge delay (2 clock cycles) */
                .recovery_delay = 2,         /* Recovery delay is (2 clock cycles) */
                .row_cylce = 7,              /* Row cycle delay is (7 clock cycles) */
                .self_refresh = 4,           /* Self refresh time is (4 clock cycles) */
                .exit_self_refresh = 7,      /* Exit self-refresh delay (7 clock cycles) */
                .load_mode_register = 2,     /* Load Mode Register to Activate delay */
                .refresh_period = 64,        /* Refresh period in ms */
            },
        },
    },
};

/**
 * @brief   Number of configured FMC banks
 */
#define FMC_BANK_NUMOF  ARRAY_SIZE(fmc_bank_config)
/** @} */

// i2c

// i2s

// spi

/**
 * @name PWM configuration
 * @{
 *
 * | dev   | chan | pin     |
 * |-------|------|---------|
 * | TIM10 | 0    | CN13-D3 |
 * | TIM3  | 2    | CN13-D5 |
 * | TIM11 | 0    | CN13-D6 |
 * | TIM12 | 0    | CN9-D9  |
 * | TIM1  | 3    | CN9-D10 |
 * | TIM12 | 1    | CN9-D11 |
 */
static const pwm_conf_t pwm_config[] = {
    {
        .dev = TIM10,
        .rcc_mask = RCC_APB2ENR_TIM10EN,
        .chan = {
            { .pin = GPIO_PIN(PORT_F, 6), .cc_chan = 0 }, /* CN13 D3*/
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
        },
        .af = GPIO_AF3,
        .bus = APB2,
    },
    {
        .dev = TIM3,
        .rcc_mask = RCC_APB1ENR_TIM3EN,
        .chan = {
            { .pin = GPIO_PIN(PORT_C, 8), .cc_chan = 2 }, /* CN13 D5 */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
        },
        .af = GPIO_AF2,
        .bus = APB1,
    },
    {
        .dev = TIM11,
        .rcc_mask = RCC_APB2ENR_TIM11EN,
        .chan = {
            { .pin = GPIO_PIN(PORT_F, 7), .cc_chan = 0 }, /* D6 */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },          /* unused */
        },
        .af = GPIO_AF3,
        .bus = APB2,
    },
    {
        .dev = TIM12,
        .rcc_mask = RCC_APB1ENR_TIM12EN,
        .chan = {
            { .pin = GPIO_PIN(PORT_H, 6), .cc_chan = 0 },  /* D9 */
            { .pin = GPIO_PIN(PORT_B, 15), .cc_chan = 1 }, /* D11 */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },           /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },           /* unused */
        },
        .af = GPIO_AF9,
        .bus = APB1,
    },
    {
        .dev = TIM1,
        .rcc_mask = RCC_APB2ENR_TIM1EN,
        .chan = {
            { .pin = GPIO_PIN(PORT_A, 11), .cc_chan = 3 }, /* D10 */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },           /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },           /* unused */
            { .pin = GPIO_UNDEF, .cc_chan = 0 },           /* unused */
        },
        .af = GPIO_AF1,
        .bus = APB2,
    },
};
#define PWM_NUMOF   ARRAY_SIZE(pwm_config)
/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
