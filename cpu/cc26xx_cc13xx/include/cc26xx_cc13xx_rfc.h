/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         cpu_cc26xx_cc13xx_definitions
 * @{
 *
 * @file
 * @brief           CC26xx/CC13xx MCU I/O register definitions
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include "cc26xx_cc13xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   RFC_DBELL registers
 */
typedef struct {
    reg32_t CMDR; /**< Doorbell Command Register */
    reg32_t CMDSTA; /**< Doorbell Command Status Register */
    reg32_t RFHWIFG; /**< Interrupt Flags From RF Hardware Modules */
    reg32_t RFHWIEN; /**< Interrupt Enable For RF Hardware Modules */
    reg32_t RFCPEIFG; /**< Interrupt Flags For Command and Packet Engine
                           Generated Interrupts */
    reg32_t RFCPEIEN; /**< Interrupt Enable For Command and Packet Engine
                           Generated Interrupts */
    reg32_t RFCPEISL; /**< Interrupt Vector Selection For Command and Packet
                          Engine Generated Interrupts */
    reg32_t RFACKIFG; /**< Doorbell Command Acknowledgement Interrupt Flag */
    reg32_t SYSGPOCTL; /**< RF Core General Purpose Output Control */
} rfc_dbell_regs_t;

/**
 * @brief   RFC_DBELL definitions
 * @{
 */

/**
 * @brief   RFCHWIFG/RFCHWIEN interrupt flags
 */
typedef enum {
    HW_IRQ_FSCA     = (1 << 1),
    HW_IRQ_MDMDONE  = (1 << 2),
    HW_IRQ_MDMIN    = (1 << 3),
    HW_IRQ_MDMOUT   = (1 << 4),
    HW_IRQ_MDMSOFT  = (1 << 5),
    HW_IRQ_TRCTK    = (1 << 6),
    HW_IRQ_RFEDONE  = (1 << 8),
    HW_IRQ_RFESOFT0 = (1 << 9),
    HW_IRQ_RFESOFT1 = (1 << 10),
    HW_IRQ_RFESOFT2 = (1 << 11),
    HW_IRQ_RATCH0   = (1 << 12),
    HW_IRQ_RATCH1   = (1 << 13),
    HW_IRQ_RATCH2   = (1 << 14),
    HW_IRQ_RATCH3   = (1 << 15),
    HW_IRQ_RATCH4   = (1 << 16),
    HW_IRQ_RATCH5   = (1 << 17),
    HW_IRQ_RATCH6   = (1 << 18),
    HW_IRQ_RATCH7   = (1 << 19)
} rf_hw_irq_t;

/**
 * @name    RFCPEIFG, RFCPEIEN and RFCPEISL interrupt flags
 *
 * Descriptions from SWCU185G, Table 25-207 "RFCPEIFG Register Field
 * Descriptions".
 * @{
 */
/** @brief A radio operation command has finished */
#define CPE_IRQ_COMMAND_DONE           (1UL << 0)
/** @brief The last radio operation command in a chain of
 *   commands has finished */
#define CPE_IRQ_LAST_COMMAND_DONE      (1UL << 1)
/** @brief IEEE 802.15.4 mode only: a foreground radio operation
 *   command has finished */
#define CPE_IRQ_FG_COMMAND_DONE        (1UL << 2)
/** @brief IEEE 802.15.4 mode only: the last foreground radio
 *   operation command in a chain of commands has finished */
#define CPE_IRQ_LAST_FG_COMMAND_DONE   (1UL << 3)
/** @brief Packet transmitted */
#define CPE_IRQ_TX_DONE                (1UL << 4)
/** @brief BLE mode: acknowledgement received on a transmitted packet.
 *   IEEE 802.15.4 mode: transmitted automatic ACK frame */
#define CPE_IRQ_TX_ACK                 (1UL << 5)
/** @brief BLE mode: transmitted LL control packet */
#define CPE_IRQ_TX_CTRL                (1UL << 6)
/** @brief BLE mode: acknowledgement received on a transmitted LL
 *   control packet */
#define CPE_IRQ_TX_CTRL_ACK            (1UL << 7)
/** @brief BLE mode only: acknowledgement received on a transmitted
 *   LL control packet, and acknowledgement transmitted for that packet */
#define CPE_IRQ_TX_CTRL_ACK_ACK        (1UL << 8)
/** @brief BLE mode only: packet retransmitted */
#define CPE_IRQ_TX_RETRANS             (1UL << 9)
/** @brief TX queue data entry state changed to finished */
#define CPE_IRQ_TX_ENTRY_DONE          (1UL << 10)
/** @brief BLE mode only: a buffer change is complete after
 *   CMD_BLE_ADV_PAYLOAD */
#define CPE_IRQ_TX_BUFFER_CHANGED      (1UL << 11)
#ifdef CPU_VARIANT_X2
/** @brief A radio operation command has gone into active state */
#  define CPE_IRQ_COMMAND_STARTED        (1UL << 12)
/** @brief IEEE 802.15.4 mode only: a foreground radio operation
 *   command has gone into active state */
#  define CPE_IRQ_FG_COMMAND_STARTED     (1UL << 13)
#else
/** @brief Interrupt flag 12 */
#  define CPE_IRQ_IRQ12                  (1UL << 12)
/** @brief Interrupt flag 13 */
#  define CPE_IRQ_IRQ13                  (1UL << 13)
#endif
/** @brief Interrupt flag 14 */
#define CPE_IRQ_IRQ14                  (1UL << 14)
/** @brief Interrupt flag 15 */
#define CPE_IRQ_IRQ15                  (1UL << 15)
/** @brief Packet received correctly */
#define CPE_IRQ_RX_OK                  (1UL << 16)
/** @brief Packet received with CRC error */
#define CPE_IRQ_RX_NOK                 (1UL << 17)
/** @brief Packet received, but can be ignored */
#define CPE_IRQ_RX_IGNORED             (1UL << 18)
/** @brief BLE mode only: packet received with CRC OK, not to be
 *   ignored, no payload */
#define CPE_IRQ_RX_EMPTY               (1UL << 19)
/** @brief BLE mode only: LL control packet received with CRC OK, not
 *   to be ignored */
#define CPE_IRQ_RX_CTRL                (1UL << 20)
/** @brief BLE mode only: LL control packet received with CRC OK,
 *   not to be ignored, then acknowledgement sent */
#define CPE_IRQ_RX_CTRL_ACK            (1UL << 21)
/** @brief Packet received that did not fit in the RX queue */
#define CPE_IRQ_RX_BUF_FULL            (1UL << 22)
/** @brief RX queue data entry changing state to finished */
#define CPE_IRQ_RX_ENTRY_DONE          (1UL << 23)
/** @brief Data written to partial read RX buffer */
#define CPE_IRQ_RX_DATA_WRITTEN        (1UL << 24)
/** @brief Specified number of bytes written to partial read
 *   RX buffer */
#define CPE_IRQ_RX_N_DATA_WRITTEN      (1UL << 25)
/** @brief Packet reception stopped before packet was done */
#define CPE_IRQ_RX_ABORTED             (1UL << 26)
/** @brief Interrupt flag 27 */
#define CPE_IRQ_IRQ27                  (1UL << 27)
/** @brief The phase-locked loop in the frequency synthesizer has
 *   reported loss of lock */
#define CPE_IRQ_SYNTH_NO_LOCK          (1UL << 28)
/** @brief As part of the boot process, the CPE has opened access
 *   to RF Core modules and memories */
#define CPE_IRQ_MODULES_UNLOCKED       (1UL << 29)
/** @brief The boot of the command and packet engine (CPE) is finished */
#define CPE_IRQ_BOOT_DONE              (1UL << 30)
/** @brief The CPE has observed an unexpected error. A reset of the
 *   CPE is needed, by switching the RF Core power domain off and on */
#define CPE_IRQ_INTERNAL_ERROR         (1UL << 31)
/** @} */

#define RFACKIFG_ACKFLAG 0x1   /**< RFACKIFG: command acknowledgement flag */
/** @} */

/**
 * @ingroup cpu_specific_peripheral_memory_map
 * @{
 */
#define RFC_DBELL_BASE (PERIPH_BASE + 0x41000) /**< RFC_DBELL base address */
#define RFC_DBELL_BASE_NONBUF (PERIPH_BASE_NONBUF + 0x41000) /**< RFC_DBELL base address */
/** @} */

#define RFC_DBELL ((rfc_dbell_regs_t *) (RFC_DBELL_BASE)) /**< RFC_DBELL register bank */
/** RFC_DBELL register bank, non-buffered access */
#define RFC_DBELL_NONBUF ((rfc_dbell_regs_t *) (RFC_DBELL_BASE_NONBUF))

/**
 * @brief   RFC_PWR registers
 */
typedef struct {
    reg32_t PWMCLKEN; /**< RF Core Power Management and Clock Enable */
} rfc_pwr_regs_t;

/**
 * @brief   RFC_PWR definitions
 * @{
 */
#define PWMCLKEN_RFCTRC 0x400
#define PWMCLKEN_FSCA   0x200
#define PWMCLKEN_PHA    0x100
#define PWMCLKEN_RAT    0x80
#define PWMCLKEN_RFERAM 0x40
#define PWMCLKEN_MDMRAM 0x10
#define PWMCLKEN_MDM    0x8
#define PWMCLKEN_CPERAM 0x4
#define PWMCLKEN_CPE    0x2
#define PWMCLKEN_RFC    0x1
/** @} */

/**
 * @ingroup cpu_specific_peripheral_memory_map
 * @{
 */
#define RFC_PWR_BASE (PERIPH_BASE + 0x40000) /**< RFC_PWR base address */
#define RFC_PWR_BASE_NONBUF (PERIPH_BASE_NONBUF + 0x40000) /**< RFC_PWR base address */
/** @} */

#define RFC_PWR ((rfc_pwr_regs_t *) (RFC_PWR_BASE)) /**< RFC_PWR register bank */
#define RFC_PWR_NONBUF ((rfc_pwr_regs_t *) (RFC_PWR_BASE_NONBUF)) /**< RFC_PWR register bank */

#ifdef __cplusplus
}
#endif

/** @} */
