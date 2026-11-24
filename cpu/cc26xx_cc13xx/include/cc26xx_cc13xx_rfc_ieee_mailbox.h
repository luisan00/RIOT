/*
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         cpu_cc26xx_cc13xx
 * @{
 *
 * @file
 * @brief           CC26xx/CC13xx RF Core IEEE 802.15.4 mode status codes
 *
 * The values of the `status` field of the IEEE 802.15.4 radio operation
 * commands (SWCU185G, Table 25-82).
 *
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Radio operation status
 * @{
 */
/**
 * @brief   Operation not finished
 */
enum {
    RFC_IEEE_SUSPENDED          = 0x2001,   /**< Operation suspended */
};

/**
 * @brief   Operation finished normally
 */
enum {
    RFC_IEEE_DONE_OK            = 0x2400,   /**< Operation ended normally */
    RFC_IEEE_DONE_BUSY          = 0x2401,   /**< CSMA-CA operation ended with
                                                 failure */
    RFC_IEEE_DONE_STOPPED       = 0x2402,   /**< Operation stopped after stop
                                                 command */
    RFC_IEEE_DONE_ACK           = 0x2403,   /**< ACK packet received with
                                                 pending data bit cleared */
    RFC_IEEE_DONE_ACKPEND       = 0x2404,   /**< ACK packet received with
                                                 pending data bit set */
    RFC_IEEE_DONE_TIMEOUT       = 0x2405,   /**< Operation ended due to
                                                 timeout */
    RFC_IEEE_DONE_BGEND         = 0x2406,   /**< Foreground operation ended
                                                 because the necessary
                                                 background level operation
                                                 ended */
    RFC_IEEE_DONE_ABORT         = 0x2407,   /**< Operation aborted by
                                                 command */
};

/**
 * @brief   Operation finished with error
 */
enum {
    RFC_IEEE_ERROR_PAR          = 0x2800,   /**< Illegal parameter */
    RFC_IEEE_ERROR_NO_SETUP     = 0x2801,   /**< Operation using RX or TX
                                                 attempted when not in
                                                 IEEE 802.15.4 mode */
    RFC_IEEE_ERROR_NO_FS        = 0x2802,   /**< Operation using RX or TX
                                                 attempted without the
                                                 synthesizer configured */
    RFC_IEEE_ERROR_SYNTH_PROG   = 0x2803,   /**< Synthesizer programming
                                                 failed to complete on time */
    RFC_IEEE_ERROR_RXOVF        = 0x2804,   /**< Receiver overflowed during
                                                 operation */
    RFC_IEEE_ERROR_TXUNF        = 0x2805,   /**< Transmitter underflowed
                                                 during operation */
};
/** @} */

#ifdef __cplusplus
} /* end of extern "C" */
#endif

/** @} */
