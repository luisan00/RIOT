/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup         cpu_cc26xx_cc13xx
 * @{
 *
 * @file
 * @brief           CC26xx/CC13xx RF Core queue management
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <assert.h>
#include "cc26xx_cc13xx_rfc_queue.h"

rfc_data_entry_t *cc26x2_cc13x2_rfc_queue_recv(rfc_data_queue_t *queue)
{
    assert(queue);

    rfc_data_entry_t *start_entry = queue->curr_entry;
    rfc_data_entry_t *curr_entry = start_entry;

    do {
        if (((volatile rfc_data_entry_t *)curr_entry)->status ==
            RFC_DATA_ENTRY_FINISHED) {
            return curr_entry;
        }

        curr_entry = curr_entry->next_entry;
    } while (curr_entry != start_entry);

    return NULL;
}
