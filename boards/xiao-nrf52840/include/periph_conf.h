/*
 * Copyright (C) 2022 Luis A. Ruiz
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     boards_xiao-nrf52840
 * @{
 * @file
 * @brief       Peripheral configuration for the Xiao nRF52840
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#ifndef PERIPH_CONF_H
#define PERIPH_CONF_H

#include "cfg_clock_32_1.h"
#include "cfg_rtt_default.h"
#include "cfg_timer_default.h"
#include "periph_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    UART configuration
 *
 * @todo    ** Not Tested **
 *@{
 */

static const uart_conf_t uart_config[] = {
    {
        .dev = NRF_UARTE0,
        .rx_pin = GPIO_PIN(0, 6),
        .tx_pin = GPIO_PIN(0, 7),
#ifdef MODULE_PERIPH_UART_HW_FC
        .rts_pin = GPIO_UNDEF,
        .cts_pin = GPIO_UNDEF,
#endif
        .irqn = UARTE0_UART0_IRQn,
    },
    {
        .dev = NRF_UARTE1,
        .rx_pin = GPIO_PIN(1, 12),
        .tx_pin = GPIO_PIN(1, 11),
#ifdef MODULE_PERIPH_UART_HW_FC
        .rts_pin = GPIO_UNDEF,
        .cts_pin = GPIO_UNDEF,
#endif
        .irqn = UARTE1_IRQn,
    },
};

#define UART_0_ISR isr_uart0
#define UART_1_ISR isr_uart1

#define UART_NUMOF ARRAY_SIZE(uart_config)
/** @} */

/**
 * @name    I2C configuration
 * @todo    ** Add ports **
 * @{
 */
static const i2c_conf_t i2c_config[] = {
    {
        .dev = NRF_TWIM0,
        .scl = GPIO_PIN(0, 5),
        .sda = GPIO_PIN(0, 4),
        .speed = I2C_SPEED_NORMAL,

    },
};
#define I2C_NUMOF ARRAY_SIZE(i2c_config)
/** @} */

/**
 * @name    SPI configuration
 * @todo    ** Add ports **
 * @{
 */
static const spi_conf_t spi_config[] = {
    {
        .dev = NRF_SPIM0,
        .sclk = GPIO_PIN(1, 13),
        .mosi = GPIO_PIN(1, 15),
        .miso = GPIO_PIN(1, 14),
    },
};
#define SPI_NUMOF ARRAY_SIZE(spi_config)
/** @} */

/**
 * @name   PWM configuration
 * @todo ** Not Tested **
 *      - Add a second channel !?
 * @{
 */
static const pwm_conf_t pwm_config[] = {
    {
        .dev = NRF_PWM0,
        .pin =
            {
                GPIO_PIN(0, 26),
                GPIO_PIN(0, 30),
                GPIO_PIN(0, 6),
            },
    },
};

#define PWM_NUMOF ARRAY_SIZE(pwm_config)
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_CONF_H */
/** @} */
