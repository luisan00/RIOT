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
 * @brief           CC26x2/CC13x2 RF Core: power, commands and data queue,
 *                  shared by the two radio drivers
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
 * @brief   Start to power the RF Core up
 *
 * Switches the power domain of the RF Core on, enables its clocks and lets
 * it boot. The boot takes a moment: @ref cc26x2_cc13x2_rfc_confirm_on tells
 * when it is over.
 */
void cc26x2_cc13x2_rfc_request_on(void);

/**
 * @brief   Check whether the RF Core has booted
 *
 * @pre     @ref cc26x2_cc13x2_rfc_request_on was called.
 *
 * @return  0 if the RF Core has booted
 * @return  -EAGAIN if it is still booting
 */
int cc26x2_cc13x2_rfc_confirm_on(void);

/**
 * @brief   Finish the power-up of the RF Core, once it has booted
 *
 * - Switches the system to the HF crystal oscillator, which the radio needs.
 * - Patches the firmware of the RF Core (CPE) with @p cpe_patch_fn.
 * - Enables the RTC update signal that the commands to start and stop the
 *   radio timer synchronize to.
 *
 * After this the RF Core takes the setup commands of the radio.
 *
 * @pre     @ref cc26x2_cc13x2_rfc_confirm_on has returned 0.
 *
 * @param[in]   cpe_patch_fn    function that applies the CPE patch, or NULL
 *                              if there is none to apply
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
 * @brief   Send a command to the RF Core and wait until it has taken it
 *
 * For a radio operation command this is not the end of the command, only
 * its start: see @ref cc26x2_cc13x2_rfc_confirm_execute.
 *
 * @param[in]   cmd     pointer to the command structure, or a direct command
 *                      built with RFC_CMDR_DIR_CMD()
 *
 * @return  the value of the CMDSTA register: its low byte is
 *          #RFC_CMDSTA_DONE if the RF Core accepted the command
 */
uint32_t cc26x2_cc13x2_rfc_request_execute(uintptr_t cmd);

/**
 * @brief   Check whether the radio operation command that was sent, or the
 *          last one of its chain, has ended
 *
 * @pre     The command was sent with @ref cc26x2_cc13x2_rfc_request_execute.
 *
 * @note    This only tells that the command has ended (LAST_COMMAND_DONE).
 *          How it ended is in the `status` field of the command structure.
 *
 * @return  0 if the command has ended, with or without error
 * @return  -EAGAIN if it has not ended yet
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
 * @brief   Look for a received frame in a data queue
 *
 * Goes through the entries of the queue and returns the first one the RF
 * Core has finished.
 *
 * @note    The entries of the queue have to form a ring (no last entry).
 *
 * @note    When the frame has been read, set the `status` field of the entry
 *          back to @ref RFC_DATA_ENTRY_PENDING, so that the RF Core can use
 *          it again.
 *
 * @pre     @p queue != NULL
 *
 * @param[in]   queue   the data queue
 *
 * @return  the finished entry
 * @return  NULL if there is none
 */
rfc_data_entry_t *cc26x2_cc13x2_rfc_queue_recv(rfc_data_queue_t *queue);

#ifdef __cplusplus
}
#endif

/** @} */
