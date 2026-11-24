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
 * @brief           CC26xx/CC13xx RF Core IEEE 802.15.4 commands definitions
 *
 * The commands of the IEEE 802.15.4 mode of the RF Core, for the O-QPSK PHY
 * in the 2.4 GHz band. They are described in section 25.5 of the CC13x2,
 * CC26x2 Technical Reference Manual (SWCU185G), and the structures follow
 * `rf_ieee_cmd.h` of TI's driverlib.
 *
 * The receiver runs as a background level operation, and transmissions,
 * CSMA-CA and the reception of an ACK run on top of it as foreground level
 * operations.
 *
 * The structures of the immediate commands are aligned to 4 bytes, as the
 * command pointer written to CMDR must be (SWCU185G, 25.3.2.1). The other
 * ones are by their members.
 *
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stdint.h>

#include "cc26xx_cc13xx_rfc_common_cmd.h"
#include "cc26xx_cc13xx_rfc_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    IEEE 802.15.4 radio operation commands
 * @{
 */
#define RFC_CMD_IEEE_RX             (0x2801) /**< Run receiver (background) */
#define RFC_CMD_IEEE_ED_SCAN        (0x2802) /**< Run energy detect scan
                                                  (background) */
#define RFC_CMD_IEEE_TX             (0x2C01) /**< Transmit packet */
#define RFC_CMD_IEEE_CSMA           (0x2C02) /**< Perform CSMA-CA */
#define RFC_CMD_IEEE_RX_ACK         (0x2C03) /**< Receive acknowledgment */
#define RFC_CMD_IEEE_ABORT_BG       (0x2C04) /**< Abort background level
                                                  operation */
/** @} */

/**
 * @name    IEEE 802.15.4 immediate and direct commands
 * @{
 */
#define RFC_CMD_IEEE_MOD_CCA        (0x2001) /**< Modify CCA parameters of the
                                                  running receiver */
#define RFC_CMD_IEEE_MOD_FILT       (0x2002) /**< Modify frame filtering
                                                  parameters of the running
                                                  receiver */
#define RFC_CMD_IEEE_MOD_SRC_MATCH  (0x2003) /**< Modify source matching
                                                  parameters of the running
                                                  receiver */
#define RFC_CMD_IEEE_ABORT_FG       (0x2401) /**< Abort foreground level
                                                  operation */
#define RFC_CMD_IEEE_STOP_FG        (0x2402) /**< Stop foreground level
                                                  operation */
#define RFC_CMD_IEEE_CCA_REQ        (0x2403) /**< Request CCA and RSSI
                                                  information */
/** @} */

/**
 * @name    States reported by CMD_IEEE_CCA_REQ
 * @{
 */
#define RFC_IEEE_CCA_IDLE           (0) /**< Channel is idle */
#define RFC_IEEE_CCA_BUSY           (1) /**< Channel is busy */
#define RFC_IEEE_CCA_INVALID        (2) /**< State cannot be determined yet */
/** @} */

/**
 * @brief   Receiver configuration of CMD_IEEE_RX
 */
typedef struct {
    uint8_t auto_flush_crc:1; /**< Remove packets with CRC error from the RX
                                   queue */
    uint8_t auto_flush_ign:1; /**< Remove packets that can be ignored
                                   according to frame filtering from the RX
                                   queue */
    uint8_t include_phy_hdr:1; /**< Include the received PHY header field in
                                    the stored packet */
    uint8_t include_crc:1; /**< Include the received CRC field in the stored
                                packet */
    uint8_t append_rssi:1; /**< Append an RSSI byte to the packet */
    uint8_t append_corr_crc:1; /**< Append a correlation value and CRC result
                                    byte to the packet */
    uint8_t append_src_ind:1; /**< Append an index from the source matching
                                   algorithm */
    uint8_t append_timestamp:1; /**< Append a timestamp to the packet */
} rfc_ieee_rx_config_t;

/**
 * @brief   Frame filtering options of CMD_IEEE_RX and CMD_IEEE_MOD_FILT
 */
typedef struct {
    uint16_t frame_filt_en:1; /**< Enable frame filtering */
    uint16_t frame_filt_stop:1; /**< 0: receive all packets to the end, 1:
                                     stop receiving a frame once frame
                                     filtering has rejected it */
    uint16_t auto_ack_en:1; /**< Enable auto ACK */
    uint16_t slotted_ack_en:1; /**< 0: non-slotted ACK, 1: slotted ACK */
    uint16_t auto_pend_en:1; /**< Enable auto-pend */
    uint16_t default_pend:1; /**< Value of the pending data bit in auto ACK
                                  packets that are not subject to auto-pend */
    uint16_t pend_data_req_only:1; /**< 0: use auto-pend for any packet, 1:
                                        for data request packets only */
    uint16_t pan_coord:1; /**< Device is PAN coordinator */
    uint16_t max_frame_version:2; /**< Reject frames whose frame version
                                       field is greater than this value */
    uint16_t fcf_reserved_mask:3; /**< Value to AND with the reserved part of
                                       the FCF; the frame is rejected if the
                                       result is not zero */
    uint16_t modify_ft_filter:2; /**< Treatment of the MSB of the frame type
                                      before frame type filtering: 0: no
                                      modification, 1: invert, 2: set to 0,
                                      3: set to 1 */
    uint16_t strict_len_filter:1; /**< 0: accept ACK frames of any length
                                       >= 5, 1: only of length 5 */
} rfc_ieee_frame_filt_opt_t;

/**
 * @brief   Frame types to accept in frame filtering
 */
typedef struct {
    uint8_t accept_ft0_beacon:1; /**< Accept beacon frames */
    uint8_t accept_ft1_data:1; /**< Accept data frames */
    uint8_t accept_ft2_ack:1; /**< 0: reject ACK frames unless an ACK receive
                                   command runs, 1: always accept */
    uint8_t accept_ft3_mac_cmd:1; /**< Accept MAC command frames */
    uint8_t accept_ft4_reserved:1; /**< Accept frames of type 100 */
    uint8_t accept_ft5_reserved:1; /**< Accept frames of type 101 */
    uint8_t accept_ft6_reserved:1; /**< Accept frames of type 110 */
    uint8_t accept_ft7_reserved:1; /**< Accept frames of type 111 */
} rfc_ieee_frame_types_t;

/**
 * @brief   CCA options
 */
typedef struct {
    uint8_t cca_en_energy:1; /**< Enable energy scan as CCA source */
    uint8_t cca_en_corr:1; /**< Enable correlator based carrier sense as CCA
                                source */
    uint8_t cca_en_sync:1; /**< Enable sync found based carrier sense as CCA
                                source */
    uint8_t cca_corr_op:1; /**< Operator between energy and correlator based
                                CCA: 0: busy if either is busy, 1: busy if
                                both are busy */
    uint8_t cca_sync_op:1; /**< Operator between sync found based CCA and the
                                others: 0: always busy if ccaSync is busy,
                                1: always idle if ccaSync is idle */
    uint8_t cca_corr_thr:2; /**< Threshold for the number of correlation
                                 peaks in correlator based carrier sense */
    uint8_t __reserved:1; /**< Reserved */
} rfc_ieee_cca_opt_t;

/**
 * @brief   Output structure of CMD_IEEE_RX
 */
typedef struct {
    uint8_t tx_ack; /**< Total number of transmitted ACK frames */
    uint8_t rx_beacon; /**< Number of received beacon frames */
    uint8_t rx_data; /**< Number of received data frames */
    uint8_t rx_ack; /**< Number of received acknowledgment frames */
    uint8_t rx_mac_cmd; /**< Number of received MAC command frames */
    uint8_t rx_reserved; /**< Number of received frames with reserved frame
                              type */
    uint8_t rx_nok; /**< Number of received frames with CRC error */
    uint8_t rx_ignored; /**< Number of received frames that are to be
                             ignored */
    uint8_t rx_buf_full; /**< Number of received frames discarded because the
                              RX buffer was full */
    int8_t last_rssi; /**< RSSI of the last received frame */
    int8_t max_rssi; /**< Highest RSSI observed in the operation */
    uint8_t __dummy0; /**< Padding */
    rfc_ratmr_t beacon_timestamp; /**< Time stamp of the last received beacon
                                       frame */
} rfc_ieee_rx_output_t;

/**
 * @brief   CMD_IEEE_RX: run the receiver
 *
 * A background level operation: it goes on until it is ended, and the
 * foreground level commands run while it is active.
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint8_t channel; /**< Channel to tune to at the start of the operation.
                          0: use the existing channel, 11-26: IEEE 802.15.4
                          channel, the frequency is (2405 + 5 * (channel -
                          11)) MHz, 60-207: the frequency is (2300 + channel)
                          MHz */
    rfc_ieee_rx_config_t rx_config; /**< Receiver configuration */
    rfc_data_queue_t *rx_queue; /**< Pointer to the receive queue */
    rfc_ieee_rx_output_t *output; /**< Pointer to the output structure (NULL:
                                       do not store results) */
    rfc_ieee_frame_filt_opt_t frame_filt_opt; /**< Frame filtering options */
    rfc_ieee_frame_types_t frame_types; /**< Frame types to receive in frame
                                             filtering */
    rfc_ieee_cca_opt_t cca_opt; /**< CCA options */
    int8_t cca_rssi_thr; /**< RSSI threshold for CCA */
    uint8_t __dummy0; /**< Padding */
    uint8_t num_ext_entries; /**< Number of extended address entries */
    uint8_t num_short_entries; /**< Number of short address entries */
    uint32_t *ext_entry_list; /**< Pointer to the list of extended address
                                   entries */
    uint32_t *short_entry_list; /**< Pointer to the list of short address
                                     entries */
    uint64_t local_ext_addr; /**< The extended address of the local device */
    uint16_t local_short_addr; /**< The short address of the local device */
    uint16_t local_pan_id; /**< The PAN ID of the local device */
    uint16_t __dummy1; /**< Padding */
    uint8_t __dummy2; /**< Padding */
    rfc_trigger_t end_trigger; /**< Trigger that causes the device to end the
                                    RX operation */
    rfc_ratmr_t end_time; /**< Time used together with end_trigger */
} rfc_cmd_ieee_rx_t;

/**
 * @brief   CMD_IEEE_ED_SCAN: run an energy detect scan
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint8_t channel; /**< Channel to tune to, as in CMD_IEEE_RX */
    rfc_ieee_cca_opt_t cca_opt; /**< CCA options */
    int8_t cca_rssi_thr; /**< RSSI threshold for CCA */
    uint8_t __dummy0; /**< Padding */
    int8_t max_rssi; /**< The maximum RSSI recorded during the ED scan */
    rfc_trigger_t end_trigger; /**< Trigger that causes the device to end the
                                    operation */
    rfc_ratmr_t end_time; /**< Time used together with end_trigger */
} rfc_cmd_ieee_ed_scan_t;

/**
 * @brief   CMD_IEEE_TX: transmit a packet
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    struct {
        uint8_t include_phy_hdr:1; /**< 0: find the PHY header automatically,
                                        1: insert the PHY header from the
                                        buffer */
        uint8_t include_crc:1; /**< 0: append the automatically calculated
                                    CRC, 1: insert the FCS from the buffer */
        uint8_t __reserved:1; /**< Reserved */
        uint8_t payload_len_msb:5; /**< Most significant bits of the payload
                                        length, only for long non-standard
                                        packets in tests */
    } tx_opt; /**< Transmit options */
    uint8_t payload_len; /**< Number of bytes in the payload */
    uint8_t *payload; /**< Pointer to the payload buffer */
    rfc_ratmr_t timestamp; /**< Time stamp of the transmitted frame */
} rfc_cmd_ieee_tx_t;

/**
 * @brief   CMD_IEEE_CSMA: perform CSMA-CA
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint16_t random_state; /**< The state of the pseudo-random generator */
    uint8_t mac_max_be; /**< The MAC parameter macMaxBE */
    uint8_t mac_max_csma_backoffs; /**< The MAC parameter
                                        macMaxCSMABackoffs */
    struct {
        uint8_t init_cw:5; /**< The initialization value for the CW
                                parameter */
        uint8_t slotted:1; /**< 0: non-slotted CSMA, 1: slotted CSMA */
        uint8_t rx_off_mode:2; /**< 0: RX stays on during the backoffs, 1:
                                    the receiver is suspended if no frame is
                                    being received, 2: the same, or after the
                                    frame (including the auto ACK) otherwise,
                                    3: the receiver is suspended immediately
                                    during the backoffs */
    } csma_config; /**< CSMA-CA configuration */
    uint8_t nb; /**< The NB parameter of the CSMA-CA algorithm */
    uint8_t be; /**< The BE parameter of the CSMA-CA algorithm */
    uint8_t remaining_periods; /**< The number of remaining periods from a
                                    paused backoff countdown */
    int8_t last_rssi; /**< RSSI measured at the last CCA operation */
    rfc_trigger_t end_trigger; /**< Trigger that causes the device to end the
                                    CSMA-CA operation */
    rfc_ratmr_t last_timestamp; /**< Time of the last CCA operation */
    rfc_ratmr_t end_time; /**< Time used together with end_trigger */
} rfc_cmd_ieee_csma_t;

/**
 * @brief   CMD_IEEE_RX_ACK: receive an acknowledgment
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint8_t seq_no; /**< Sequence number to expect */
    rfc_trigger_t end_trigger; /**< Trigger that causes the device to give up
                                    the reception of the acknowledgment */
    rfc_ratmr_t end_time; /**< Time used together with end_trigger */
} rfc_cmd_ieee_rx_ack_t;

/**
 * @brief   CMD_IEEE_MOD_CCA: modify the CCA parameters of the running
 *          receiver
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    rfc_ieee_cca_opt_t new_cca_opt; /**< New value of cca_opt */
    int8_t new_cca_rssi_thr; /**< New value of cca_rssi_thr */
} __attribute__((aligned(4))) rfc_cmd_ieee_mod_cca_t;

/**
 * @brief   CMD_IEEE_MOD_FILT: modify the frame filtering parameters of the
 *          running receiver
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    rfc_ieee_frame_filt_opt_t new_frame_filt_opt; /**< New value of
                                                       frame_filt_opt */
    rfc_ieee_frame_types_t new_frame_types; /**< New value of frame_types */
} __attribute__((aligned(4))) rfc_cmd_ieee_mod_filt_t;

/**
 * @brief   CMD_IEEE_MOD_SRC_MATCH: enable or disable a source matching entry
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    struct {
        uint8_t enable:1; /**< 0: disable the entry, 1: enable the entry */
        uint8_t src_pend:1; /**< New value of the pending bit of the entry */
        uint8_t entry_type:1; /**< 0: short address, 1: extended address */
    } options; /**< What to do with the entry */
    uint8_t entry_no; /**< Index of the entry to enable or disable */
} __attribute__((aligned(4))) rfc_cmd_ieee_mod_src_match_t;

/**
 * @brief   CMD_IEEE_CCA_REQ: request CCA and RSSI information
 *
 * An immediate command: the RF Core fills in the fields while the receiver
 * runs.
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    int8_t current_rssi; /**< The RSSI currently observed on the channel */
    int8_t max_rssi; /**< The maximum RSSI observed on the channel since RX
                          was started */
    struct {
        uint8_t cca_state:2; /**< The current CCA state, one of
                                  RFC_IEEE_CCA_* */
        uint8_t cca_energy:2; /**< The current energy detect CCA state */
        uint8_t cca_corr:2; /**< The current correlator based carrier sense
                                 CCA state */
        uint8_t cca_sync:1; /**< The current sync found based carrier sense
                                 CCA state, 0: idle, 1: busy */
    } cca_info; /**< CCA information */
} __attribute__((aligned(4))) rfc_cmd_ieee_cca_req_t;

#ifdef __cplusplus
}
#endif

/** @} */
