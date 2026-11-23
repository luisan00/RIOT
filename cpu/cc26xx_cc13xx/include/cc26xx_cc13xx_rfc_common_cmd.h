/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup         cpu_cc26xx_cc13xx
 * @{
 *
 * @file
 * @brief           CC26xx/CC13xx RF Core common commands definitions
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 */

#include <stdint.h>

#include "cc26xx_cc13xx_rfc_mailbox.h"
#include "cc26xx_cc13xx_rfc_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Direct commands
 * @{
 */
#define RFC_CMD_ABORT (0x0401) /**< End the running command as soon as
                                    possible */
#define RFC_CMD_STOP  (0x0402) /**< Stop a running command gracefully */
#define RFC_CMD_GET_RSSI (0x0403) /**< Read the RSSI of the running receiver,
                                       returned in byte 2 of CMDSTA */
/** @} */

/**
 * @brief   Radio operation start trigger
 */
typedef struct {
    uint8_t type:4; /**< The type of trigger */
    uint8_t ena_cmd:1; /**< 0h = No alternative trigger command,
                            1h = CMD_TRIGGER can be used as an alternative
                            trigger */
    uint8_t trigger_no:2; /**< The trigger number of the CMD_TRIGGER command that
                               triggers this action */
    uint8_t past_trig:1; /**< 0: A trigger in the past is never triggered, or for
                              start of commands, give an error */
} rfc_trigger_t;
/**
 * @brief   Condition for running next operation
 */
typedef struct {
    uint8_t rule:4; /**< Rule for how to proceed */
    uint8_t skip_no:4; /**< Skip number + 1 if the rule involves skipping */
} rfc_cond_t;

/**
 * @brief   General radio operation.
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
} rfc_op_t;

/**
 * @brief   CMD_FS
 * @{
 */
#define RFC_CMD_FS (0x0803) /**< CMD_FS command ID */
/**
 * @brief   Tune frequency synthesizer
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint16_t frequency; /**< The frequency in MHz to tune to*/
    uint16_t fract_freq; /**< Fractional part of the frequency to tune to */
    struct {
        uint8_t txmode:1; /**< 0h = Start synth in RX mode, 1h = Start synth in
                               TX mode */
        uint8_t ref_freq:6; /**< 0h = Use default reference frequenc, others =
                                 Use reference frequency 48 MHz */
    } synth_conf; /**< Synthesizer configuration */
    uint8_t __dummy0; /**< Reserved, always write 0 */
    uint8_t __dummy1; /**< Reserved */
    uint8_t __dummy2; /**< Reserved */
    uint16_t __dummy3; /**< Reserved */
} rfc_cmd_fs_t;
/** @} */

/**
 * @brief   CMD_FS
 * @{
 */
#define RFC_CMD_FS_POWERDOWN (0x080D) /**< CMD_FS command ID */
/**
 * @brief   Powerdown frequency synthesizer
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
} rfc_cmd_fs_powerdown_t;
/** @} */

/**
 * @brief   CMD_SYNC_STOP_RAT
 * @{
 */
#define RFC_CMD_SYNC_STOP_RAT (0x0809) /**< CMD_SYNC_STOP_RAT command ID */
/**
 * @brief   Synchronize and Stop Radio Timer Command
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint16_t __dummy0; /**< Reserved */
    rfc_ratmr_t rat0; /**< The returned RAT timer value corresponding to the
                           value the RAT would have had when the RTC was zero */
} rfc_cmd_sync_stop_rat_t;
/** @} */

/**
 * @brief   CMD_SYNC_START_RAT
 * @{
 */
#define RFC_CMD_SYNC_START_RAT (0x080A) /**< CMD_SYNC_STOP_RAT command ID */
/**
 * @brief   Synchrously Start Radio Timer Command
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint16_t __dummy0; /**< Reserved */
    rfc_ratmr_t rat0; /**< The desired RAT timer value corresponding to the
                           value the RAT would have had when the RTC was zero.
                           This parameter is returned by CMD_SYNC_STOP_RAT */
} rfc_cmd_sync_start_rat_t;
/** @} */

/**
 * @brief   CMD_RADIO_SETUP
 * @{
 */
#define RFC_CMD_RADIO_SETUP (0x0802) /**< CMD_RADIO_SETUP command ID */

#define RFC_RADIO_SETUP_MODE_BLE        (0x00) /**< Bluetooth low energy */
#define RFC_RADIO_SETUP_MODE_IEEE802154 (0x01) /**< IEEE 802.15.4 */
#define RFC_RADIO_SETUP_MODE_KEEP       (0xFF) /**< Keep the existing mode and
                                                    update overrides only */
/**
 * @brief   Radio setup command for the pre-defined schemes
 *
 * Sets the RF Core up in one of the modes it has in ROM, like the IEEE
 * 802.15.4 one for the 2.4 GHz band (SWCU185G, section 25.3.3.1.2). The
 * proprietary mode has commands of its own.
 */
typedef struct {
    uint16_t command_no; /**< The command ID number */
    uint16_t status; /**< An integer telling the status of the command. */
    void *next_op; /**< Pointer to the next operation to run */
    rfc_ratmr_t start_time; /**< Absolute or relative start time */
    rfc_trigger_t start_trigger; /**< Identification of the trigger that
                                      starts the operation */
    rfc_cond_t condition; /**< Condition for running next command */
    uint8_t mode; /**< The main mode to use, one of RFC_RADIO_SETUP_MODE_* */
    uint8_t lo_divider; /**< LO divider setting to use. 0 selects the 2.4 GHz
                             band, the other supported values are 2, 4, 5, 6,
                             10, 12, 15 and 30. */
    struct {
        uint16_t front_end_mode:3; /**< 0h: differential mode, 1h: single-ended
                                        mode RFP, 2h: single-ended mode RFN,
                                        5h and 6h: the same two with external
                                        front end control on the RF pins */
        uint16_t bias_mode:1; /**< 0h: internal bias, 1h: external bias */
        uint16_t analog_cfg_mode:6; /**< 0x00: write analog configuration,
                                         required the first time after boot
                                         and when changing frequency band or
                                         front-end configuration. 0x2D: keep
                                         analog configuration */
        uint16_t no_fs_powerup:1; /**< 0h: power up the frequency synth, 1h:
                                       do not power it up */
    } config; /**< Configuration options */
    uint16_t tx_power; /**< Transmit power */
    uint32_t *reg_override; /**< Pointer to a list of hardware and
                                 configuration registers to override. If NULL,
                                 no override is used. */
} rfc_cmd_radio_setup_t;
/** @} */

/**
 * @brief   CMD_SET_TX_POWER
 * @{
 */
#define RFC_CMD_SET_TX_POWER (0x0010)
/**
 * @brief   Set Transmit Power
 */
typedef struct {
   uint16_t command_no; /**< The command ID number */
   uint16_t tx_power; /**< New TX power setting */
} __attribute__((aligned(4))) rfc_cmd_set_tx_power_t;
/** @} */

/**
 * @brief   CMD_SET_TX20_POWER
 * @{
 */
#define RFC_CMD_SET_TX20_POWER (0x0014)
/**
 * @brief   Set Transmit Power of the 20 dBm PA
 *
 * Only allowed when the 20 dBm PA was selected by the radio setup command
 * (SWCU185G, section 25.3.3.2.17).
 */
typedef struct {
   uint16_t command_no; /**< The command ID number */
   uint16_t __dummy0; /**< Reserved */
   uint32_t tx20_power; /**< New TX power setting of the 20 dBm PA */
} rfc_cmd_set_tx20_power_t;
/** @} */

/**
 * @brief   CMD_CLEAR_RX
 * @{
 */
#define RFC_CMD_CLEAR_RX (0x0008)
/**
 * @brief   Clear all RX Queue Entries
 */
typedef struct {
   uint16_t command_no; /**< The command ID number */
   uint16_t __dummy0; /**< Padding */
   rfc_data_queue_t *queue; /**< Pointer to the queue structure to be cleared */
} rfc_cmd_clear_rx_t;
/** @} */

#ifdef __cplusplus
} /* end of extern "C" */
#endif

/** @} */
