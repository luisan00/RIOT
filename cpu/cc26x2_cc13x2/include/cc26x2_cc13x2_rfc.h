/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         cpu_cc26x2_cc13x2
 * @{
 *
 * @file
 * @brief           CC26x2/CC13x2 RF Core common functions
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stdint.h>
#include <stdbool.h>

#include "cc26xx_cc13xx_rfc_common_cmd.h"
#include "cc26xx_cc13xx_rfc_mailbox.h"
#include "cc26xx_cc13xx_rfc_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Request the RF Core to power on.
 *
 *  - Powers on the radio core power domain.
 *  - Enables the RF Core clock gate.
 *  - Boot RF Core.
 */
void cc26x2_cc13x2_rfc_request_on(void);

/**
 * @brief   Confirm that the RF Core is on.
 *
 * @pre cc26x2_cc13x2_rfc_request_on MUST have been called before.
 *
 * @return -EAGAIN if the RF Core has not been started yet.
 * @return 0 if started.
 */
int cc26x2_cc13x2_rfc_confirm_on(void);

/**
 * @brief   Finalize the power on of the RF Core.
 *
 * @pre cc26x2_cc13x2_rfc_confirm_on MUST have been previously
 *      reported that the RF Core is on.
 *
 * - Switches the HF clock source to the XOSC crystal.
 * - Patches the CPE if @p cpe_patch_fn is provided.
 * - Turns on the clock line to the radio core, necessary
 *   for @ref RFC_CMD_SYNC_START_RAT commands.
 *
 * @param[in]   cpe_patch_fn    Function used to patch the CPE, can
 *                              be NULL if it doesn't needs to be
 *                              patched.
 */
void cc26x2_cc13x2_rfc_finish_on(void (* cpe_patch_fn)(void));

/**
 * @brief   Power the RF Core off
 *
 * Masks the RF Core interrupts, stops its clocks, switches its power domain
 * off and takes the system back to the HF RC oscillator.
 *
 * @pre No radio operation command is running, the synthesizer was powered
 *      down with CMD_FS_POWERDOWN and, if the radio timer is to resume later,
 *      it was stopped with CMD_SYNC_STOP_RAT.
 */
void cc26x2_cc13x2_rfc_power_off(void);

/**
 * @brief   Request the RF Core to execute a command.
 *
 * @param[in]   cmd   The command to send.
 *
 * @return CMDSTA register value.
 */
uint32_t cc26x2_cc13x2_rfc_request_execute(uintptr_t cmd);

/**
 * @brief   Confirm execution of the previously requested command
 *          execution.
 *
 * @pre cc26x2_cc13x2_rfc_request_execture MUST have been called before.
 *
 * @note    This only reports that the command, or the last command of a
 *          chain, has ended (LAST_COMMAND_DONE). The result is in the
 *          `status` field of the command structure.
 *
 * @return -EAGAIN if the command has not been finished.
 * @return 0 if the command has finished, with or without error.
 */
int cc26x2_cc13x2_rfc_confirm_execute(void);

/**
 * @brief   Has a radio operation command ended?
 *
 * A command that was accepted by the RF Core goes through @ref RFC_IDLE,
 * @ref RFC_PENDING and @ref RFC_ACTIVE. Every other status value is final:
 * the command ended, with or without error, or it was skipped.
 *
 * @param[in]   status  The `status` field of the command.
 *
 * @return true if the status is final.
 * @return false if the command has not started yet or is running.
 */
static inline bool cc26x2_cc13x2_rfc_cmd_has_ended(uint16_t status)
{
    return status > RFC_ACTIVE;
}

/**
 * @brief   Abort the running radio operation command and wait for its end.
 *
 * Sends the direct command CMD_ABORT, which ends the command as soon as
 * possible: a frame being received or transmitted is dropped. Does nothing
 * if the command has already ended.
 *
 * @pre     The command was accepted by the RF Core: the CMDSTA value
 *          returned by cc26x2_cc13x2_rfc_request_execute() for it was Done.
 *
 * @note    COMMAND_DONE and LAST_COMMAND_DONE of the aborted command can
 *          still be raised shortly after this function returns.
 *
 * @param[in]   status  The `status` field of the command to abort.
 *
 * @return 0 if the command is not running anymore.
 * @return -EIO if the RF Core rejected CMD_ABORT.
 * @return -ETIMEDOUT if the command did not end.
 */
int cc26x2_cc13x2_rfc_abort_cmd(const volatile uint16_t *status);

/**
 * @brief   End a running radio operation command with CMD_STOP, unless it is
 *          in the middle of a packet
 *
 * Unlike cc26x2_cc13x2_rfc_abort_cmd() this does not interrupt a packet that
 * is being received or transmitted: the command then ends by itself once the
 * packet is over.
 *
 * @pre     The command was accepted by the RF Core: the CMDSTA value
 *          returned by cc26x2_cc13x2_rfc_request_execute() for it was Done.
 *
 * @param[in]   status  The `status` field of the command to stop.
 *
 * @return 0 if the command is not running anymore.
 * @return -EBUSY if the command is still running: it handles a packet and
 *         ends after it.
 * @return -EIO if the RF Core rejected CMD_STOP.
 */
int cc26x2_cc13x2_rfc_stop_cmd(const volatile uint16_t *status);

/**
 * @brief   Is data available on the data queue?
 *
 * Loops over the queue to check for finished entries.
 *
 * @note The data queue must be configured as a circular buffer (no last entry).
 *
 * @note After processing the data entry you must set the `status` field to
 *       @ref RFC_DATA_ENTRY_PENDING so the radio CPU can use it again.
 *
 * @pre @p queue != NULL
 *
 * @param[in] queue The data queue to check.
 *
 * @return The finished data entry.
 * @return NULL if no data is available.
 */
rfc_data_entry_t *cc26x2_cc13x2_rfc_queue_recv(rfc_data_queue_t *queue);

#ifdef __cplusplus
}
#endif

/** @} */
