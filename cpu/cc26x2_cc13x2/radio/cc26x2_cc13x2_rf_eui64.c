/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     cpu_cc26x2_cc13x2_rf
 * @{
 *
 * @file
 * @brief       IEEE address of the CC13x2/CC26x2
 *
 * The references in the comments are those of cc26x2_cc13x2_rf_internal.c.
 *
 * @author      Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include <stdint.h>

#include "byteorder.h"
#include "cpu.h"

#include "cc26x2_cc13x2_rf.h"

/*
 * Reads the IEEE address of the device.
 *
 * - FCFG1:MAC_15_4_0 holds "the first 32-bits of the 64-bit MAC 15.4
 *   address" and MAC_15_4_1 the last ones ([TRM] 11.4.1.44 and 11.4.1.45),
 *   programmed at production.
 * - CCFG:IEEE_MAC_0 and IEEE_MAC_1 hold bits 31:0 and 63:32 of a custom
 *   address, which is used when it is not all ones, the erased state
 *   (`hw_ccfg.h` of [SDK]).
 *
 * Both are numbers, so the first byte in memory is the last one of the
 * EUI-64. On a LAUNCHXL-CC1352P1 the FCFG words read 0x1CA5C2EF and
 * 0x00124B00 for the address 00:12:4B:00:1C:A5:C2:EF, which starts with
 * TI's OUI 00:12:4B.
 */
void cc26x2_cc13x2_rf_get_eui64(eui64_t *addr)
{
    uint64_t val = ((uint64_t)CCFG->IEEE_MAC_1 << 32) | CCFG->IEEE_MAC_0;

    if (val == UINT64_MAX) {
        val = ((uint64_t)FCFG->MAC_15_4_1 << 32) | FCFG->MAC_15_4_0;
    }

    addr->uint64 = byteorder_htonll(val);
}
