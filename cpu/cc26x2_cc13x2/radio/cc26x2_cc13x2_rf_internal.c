/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup         cpu_cc26x2_cc13x2_rf
 * @{
 *
 * @file
 * @brief           CC13x2 sub-GHz IEEE 802.15.4g radio: operation of the RF Core
 *
 * Power, reception, transmission, CCA and channel of the radio, with the
 * command structures of cc26x2_cc13x2_rf_settings.c. The documents referred
 * to as [TRM], [SDK] and [RFCFG] are listed there.
 *
 * @author          Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author          Luis A. Ruiz <luisan00@hotmail.com>
 * @}
 */

#include <errno.h>
#include <string.h>

#include "cc26x2_cc13x2_rf_internal.h"
#include "cc26x2_cc13x2_rfc.h"
#include "vendor/rf_patch_cpe_prop.h"

#include "net/ieee802154.h"

#include "byteorder.h"
#include "irq.h"

#define ENABLE_DEBUG 0
#include "debug.h"

/**
 * @brief   Transmit buffer length.
 *
 * Format is:
 *
 * ```
 * +-----+------+
 * | PHR | PSDU |
 * +-----+------+
 * ```
 */
#define TX_BUF_LEN         ALIGN_TO_4(IEEE802154_PHY_MR_FSK_PHR_LEN + \
                                      IEEE802154G_FRAME_LEN_MAX)

/**
 * @brief   Receive buffer length.
 *
 * This is the length of an individual receive buffer.
 *
 * Format is:
 *
 * ```
 * +------------------+------+--------+
 * | rfc_data_entry_t | PSDU | Status |
 * +------------------+------+--------+
 * ```
 */
#define RX_BUF_LEN         ALIGN_TO_4(sizeof(rfc_data_entry_t) + \
                                      IEEE802154G_FRAME_LEN_MAX +  \
                                      CC26X2_CC13X2_RF_RX_STATUS_LEN)

static uint8_t _tx_buf[TX_BUF_LEN]  __attribute__ ((aligned(4))); /**< Transmit buffer */
/** PHR and PSDU of the frame in the transmit buffer */
static uint16_t _tx_pkt_len;

/** Transmit buffer of the ACK frames. It is not the one of the frames of the
 *  upper layer: that frame has to stay where it is, for its retransmissions,
 *  when a frame is received and acknowledged between two attempts. */
static uint8_t _tx_ack_buf[ALIGN_TO_4(IEEE802154_PHY_MR_FSK_PHR_LEN +
                                      IEEE802154_ACK_FRAME_LEN)]
    __attribute__ ((aligned(4)));
/** PHR and PSDU of the frame in the ACK buffer */
static uint16_t _tx_ack_pkt_len;
static rfc_data_queue_t _rx_queue __attribute__ ((aligned(4)));  /**< Receive data queue */
/** Receive buffer configured as a circular buffer, e.g.: the last one points
 * to the first, each buffer contains @ref rfc_data_entry_t which is used
 * by the RF Core to mark the buffers that have available data when a packet
 * is received, each buffer represents a different packet. */
typedef union {
    rfc_data_entry_t entry;
    uint8_t raw[RX_BUF_LEN];
} rx_buf_t;

static rx_buf_t _rx_buf[CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF]
    __attribute__ ((aligned(4)));

/**
 * Most frames that can wait to be read at a time. An entry that holds one
 * stays Finished and out of the ring the RF Core writes to, see
 * _rx_ring_build(), which needs one entry at least.
 */
#define RX_KEPT_MAX         (CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF - 1)

/** Entries with a frame that was accepted and waits to be read, oldest
 *  first */
static rfc_data_entry_general_t *_rx_kept[RX_KEPT_MAX];
static uint8_t _rx_kept_numof;

/** CMD_PROP_RX_ADV was accepted by the RF Core and not stopped since */
static bool _rx_submitted;

/** Counters and RSSI that the radio CPU updates while RX runs */
static rfc_prop_output_t _rx_output;

static uint8_t _short_addr[IEEE802154_SHORT_ADDRESS_LEN];
static uint8_t _long_addr[IEEE802154_LONG_ADDRESS_LEN];
static uint16_t _pan_id;

enum {
    STATE_OFF,              /**< the RF Core is off */
    STATE_RFC_POWER_ON,     /**< waiting for the boot of the RF Core */
    STATE_RFC_SETUP,        /**< waiting for the setup command chain */
    STATE_FINISHED,         /**< the radio is on */
} _poweron_state;

/**
 * @brief   Upper bound of the polls of a command status
 *
 * A setup command followed by CMD_FS takes well below a millisecond. The
 * bound, several tens of milliseconds at 48 MHz, only keeps a misbehaving
 * RF Core from blocking the caller forever.
 */
#define CMD_POLL_MAX                    (1000000UL)

/*
 * Waits until a radio operation command has a final status. [SDK] does the
 * same for CMD_SYNC_STOP_RAT in RF_corePowerDown(). The status field is
 * written before COMMAND_DONE and LAST_COMMAND_DONE are raised
 * ([TRM] 25.3.2.5), so it does not depend on a flag that an earlier aborted
 * command may still raise.
 */
static int _wait_cmd(const volatile uint16_t *status)
{
    for (unsigned long i = 0; i < CMD_POLL_MAX; i++) {
        if (cc26x2_cc13x2_rfc_cmd_has_ended(*status)) {
            return 0;
        }
    }

    return -ETIMEDOUT;
}

/*
 * Prepares the RX data queue and the setup command chain. Nothing is sent to
 * the RF Core here.
 *
 * RX data queue ([TRM] 25.3.2.7, Tables 25-10 and 25-11):
 *
 * - Each entry is a "general" data entry (type 0) with a 2-byte element
 *   length (`lensz = 2`). `length` is the storage after the 8-byte entry
 *   header.
 * - `status` is written as Pending (0) before the entry is handed to the
 *   radio CPU, which moves it to Active, Busy and Finished. The driver sets
 *   it back to Pending after reading the frame.
 * - The entries form a ring. `last_entry = NULL` with a non-NULL `curr_entry`
 *   means that no further entry may be appended to the queue.
 *
 * Setup chain, started by cc26x2_cc13x2_rf_confirm_on():
 *
 *     CMD_SYNC_START_RAT -> CMD_PROP_RADIO_DIV_SETUP -> CMD_FS
 *
 * [SDK] builds the same three commands in another order in
 * RF_fsmSetupState(): setup, CMD_SYNC_START_RAT, CMD_FS. Both orders respect
 * what [TRM] asks for: the setup before CMD_FS, and the RAT running before
 * any RX or TX command.
 */
void cc26x2_cc13x2_rf_internal_init(void)
{
    cc26x2_cc13x2_rf_settings_init();

    /* Initialize data entries as general, with a length field of 2 bytes
     * (uint16_t) and the correct RX buffer size */
    for (unsigned i = 0; i < CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF; i++) {
        /* verify that the compiler put our RX buffers on aligned memory */
        assert(((uintptr_t)&_rx_buf[i] & 0x3) == 0);

        /* the initial bytes of an RX buffer is the rfc_data_entry_t structure */
        rfc_data_entry_t *entry = &_rx_buf[i].entry;

        entry->status = RFC_DATA_ENTRY_PENDING;
        entry->config.type = RFC_DATA_ENTRY_TYPE_GEN;
        entry->config.lensz = sizeof(uint16_t);
        entry->length = RX_BUF_LEN - sizeof(rfc_data_entry_t);

        if ((i + 1) == CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF) {
            /* Point to the first entry if this is the last */
            entry->next_entry = &_rx_buf[0].entry;
        }
        else {
            entry->next_entry = &_rx_buf[i + 1].entry;
        }
    }

    /* Put a pointer to the receive buffers on the received queue, last entry is
     * null, this to make a circular buffer */
    _rx_queue.curr_entry = &_rx_buf[0].entry;
    _rx_queue.last_entry = NULL;

    rf_cmd_prop_rx_adv.queue = &_rx_queue;
    rf_cmd_prop_rx_adv.output = &_rx_output;
    rf_cmd_prop_rx_ack.queue = &_rx_queue;
    rf_cmd_prop_rx_ack.output = &_rx_output;

    uint64_t default_freq = cc26x2_cc13x2_rf_chan_freq(CC26X2_CC13X2_RF_CHAN_MIN);
    cc26x2_cc13x2_rf_freq_convert(default_freq,
                                  &rf_cmd_fs.frequency,
                                  &rf_cmd_fs.fract_freq);

    rf_cmd_prop_radio_div_setup.condition.rule = RFC_COND_ALWAYS;
    rf_cmd_prop_radio_div_setup.condition.skip_no = 0;
    rf_cmd_prop_radio_div_setup.next_op = (rfc_op_t *)&rf_cmd_fs;

    rf_cmd_sync_start_rat.next_op = &rf_cmd_prop_radio_div_setup;
    rf_cmd_sync_start_rat.condition.rule = RFC_COND_ALWAYS;
    /* the RAT starts from 0 the first time, cc26x2_cc13x2_rf_off() stores
     * the offset to resume from */
    rf_cmd_sync_start_rat.rat0 = 0;

    memset(_short_addr, 0, IEEE802154_SHORT_ADDRESS_LEN);
    memset(_long_addr, 0, IEEE802154_LONG_ADDRESS_LEN);
    _pan_id = 0;
    DEBUG("[cc26x2_cc13x2_rf]: radio initialised\n");
}

void cc26x2_cc13x2_rf_request_on(void)
{
    cc26x2_cc13x2_rfc_request_on();
    _poweron_state = STATE_RFC_POWER_ON;
}

/*
 * Power-on state machine, polled by the HAL:
 *
 * - STATE_RFC_POWER_ON: wait for the CPE boot. Then switch to XOSC_HF, patch
 *   the CPE and submit the setup chain. CMDSTA Done only tells that the chain
 *   was accepted ([TRM] Table 25-2).
 * - STATE_RFC_SETUP: wait for LAST_COMMAND_DONE, raised after the last
 *   command of the chain ([TRM] 25.3.2.5).
 *
 * If a command of the chain ends with an error its result is ABORT, the rest
 * of the chain is not run and LAST_COMMAND_DONE is raised all the same
 * ([TRM] 25.3.2.5.2). So the status field of each command is checked before
 * the radio is reported as on.
 */
int cc26x2_cc13x2_rf_confirm_on(void)
{
    uint32_t cmdsta;

    switch (_poweron_state) {
        case STATE_RFC_POWER_ON:
            if (cc26x2_cc13x2_rfc_confirm_on() != 0) {
                return -EAGAIN;
            }
            cc26x2_cc13x2_rfc_finish_on(rf_patch_cpe_prop);

            /* a command must be submitted with its status set to IDLE
             * ([TRM] 25.3.2.2) */
            rf_cmd_sync_start_rat.status = RFC_IDLE;
            rf_cmd_prop_radio_div_setup.status = RFC_IDLE;
            rf_cmd_fs.status = RFC_IDLE;
            _rx_submitted = false;

            cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_sync_start_rat);
            if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
                DEBUG("[cc26x2_cc13x2_rf]: setup chain rejected, cmdsta=%08lx\n",
                      cmdsta);
                return -EIO;
            }
            _poweron_state = STATE_RFC_SETUP;
            return -EAGAIN;

        case STATE_RFC_SETUP:
            if (cc26x2_cc13x2_rfc_confirm_execute() != 0) {
                return -EAGAIN;
            }
            if ((rf_cmd_sync_start_rat.status != RFC_DONE_OK) ||
                (rf_cmd_prop_radio_div_setup.status != RFC_PROP_DONE_OK) ||
                (rf_cmd_fs.status != RFC_DONE_OK)) {
                DEBUG("[cc26x2_cc13x2_rf]: setup chain failed, status: RAT=%04x "
                      "setup=%04x FS=%04x\n", rf_cmd_sync_start_rat.status,
                      rf_cmd_prop_radio_div_setup.status, rf_cmd_fs.status);
                return -EIO;
            }
            _poweron_state = STATE_FINISHED;
            return 0;

        case STATE_FINISHED:
            break;

        default:
            /* off: cc26x2_cc13x2_rf_request_on() was not called */
            return -EIO;
    }

    return 0;
}

/*
 * Switches the radio off. Mode, channel and TX power stay in the command
 * structures, so the next cc26x2_cc13x2_rf_request_on() brings the radio
 * back as it was.
 *
 * 1. End the receive command, if it runs.
 * 2. CMD_FS_POWERDOWN, then CMD_SYNC_STOP_RAT, see their structures. The
 *    returned `rat0` is stored for the next CMD_SYNC_START_RAT.
 * 3. Power the RF Core off, see cc26x2_cc13x2_rfc_power_off().
 *
 * If the two commands fail the RF Core is powered off all the same, and the
 * RAT starts from the previous offset next time.
 */
void cc26x2_cc13x2_rf_off(void)
{
    if (_poweron_state == STATE_FINISHED) {
        uint32_t cmdsta;

        cc26x2_cc13x2_rf_rx_stop();

        rf_cmd_fs_powerdown.status = RFC_IDLE;
        rf_cmd_sync_stop_rat.status = RFC_IDLE;
        cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_fs_powerdown);
        if (((cmdsta & 0xFF) == RFC_CMDSTA_DONE) &&
            (_wait_cmd(&rf_cmd_fs_powerdown.status) == 0) &&
            (rf_cmd_fs_powerdown.status == RFC_DONE_OK) &&
            (_wait_cmd(&rf_cmd_sync_stop_rat.status) == 0) &&
            (rf_cmd_sync_stop_rat.status == RFC_DONE_OK)) {
            rf_cmd_sync_start_rat.rat0 = rf_cmd_sync_stop_rat.rat0;
        }
        else {
            DEBUG("[cc26x2_cc13x2_rf]: power-down commands failed, cmdsta=%08lx "
                  "FS=%04x RAT=%04x\n", cmdsta, rf_cmd_fs_powerdown.status,
                  rf_cmd_sync_stop_rat.status);
        }
    }

    cc26x2_cc13x2_rfc_power_off();
    _rx_submitted = false;
    _poweron_state = STATE_OFF;
}

/*
 * Starts CMD_PROP_CS. The result is not available when this function
 * returns: the command ends later with LAST_COMMAND_DONE and
 * PROP_DONE_IDLE (0x3407) or PROP_DONE_BUSY (0x3408) in its status field
 * ([TRM] Table 25-188). The caller must have stopped RX before, as only one
 * radio operation command can run at a time ([TRM] 25.3.2.5).
 */
int cc26x2_cc13x2_rf_request_cca(void)
{
    uint32_t cmdsta;

    rf_cmd_prop_cs.status = RFC_IDLE;
    rf_cmd_prop_cs.next_op = NULL;
    rf_cmd_prop_cs.condition.rule = RFC_COND_NEVER;
    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_prop_cs);
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: CMD_PROP_CS failed with cmdsta=%08lx",
              cmdsta);
        return -EIO;
    }

    return 0;
}

/*
 * RX is on from the moment CMD_PROP_RX_ADV is accepted until its status
 * field gets a final value. The status alone is not enough: right after the
 * command is accepted it still reads IDLE, until the radio CPU starts the
 * command and writes PENDING or ACTIVE ([TRM] 25.3.2.2).
 */
static bool _rx_is_on(void)
{
    return _rx_submitted &&
           !cc26x2_cc13x2_rfc_cmd_has_ended(rf_cmd_prop_rx_adv.status);
}

/*
 * Links the entries of the RX queue that are free into the ring the RF Core
 * writes to, and makes the first of them the current one. No receive command
 * may be running.
 *
 * An entry that is Finished is left out: it holds a frame that waits to be
 * read, or one the interrupt handler has not looked at yet. The RF Core
 * therefore never comes across one of them, which would end the receive
 * command with PROP_ERROR_RXBUF ([TRM] Table 25-186). Any other entry is
 * free, whatever state a command that was stopped or aborted left it in.
 */
static int _rx_ring_build(void)
{
    rfc_data_entry_t *first = NULL;
    rfc_data_entry_t *prev = NULL;

    for (unsigned i = 0; i < CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF; i++) {
        rfc_data_entry_t *entry = &_rx_buf[i].entry;

        if (((volatile rfc_data_entry_t *)entry)->status == RFC_DATA_ENTRY_FINISHED) {
            continue;
        }
        entry->status = RFC_DATA_ENTRY_PENDING;
        if (first == NULL) {
            first = entry;
        }
        else {
            prev->next_entry = entry;
        }
        prev = entry;
    }
    if (first == NULL) {
        return -ENOBUFS;
    }
    prev->next_entry = first;
    _rx_queue.curr_entry = first;

    return 0;
}

/*
 * Starts CMD_PROP_RX_ADV:
 *
 * 1. Build the ring of free entries, see _rx_ring_build(). Without a free
 *    entry RX is not started and this function returns -ENOBUFS.
 * 2. Submit the RX command. CMDSTA SchedulingError (0x86) means that another
 *    radio operation command is still running; it is not affected by the
 *    rejected command ([TRM] Table 25-2). This function then returns -EBUSY
 *    and leaves the running command alone.
 *
 * The synthesizer must already be programmed: this command needs a previous
 * CMD_FS ([TRM] 25.10.5.4), from the setup chain or from
 * cc26x2_cc13x2_rf_change_chan().
 */
static int _rx_start(void)
{
    uint32_t cmdsta;

    if (_rx_is_on()) {
        return 0;
    }
    _rx_submitted = false;

    if (_rx_ring_build() != 0) {
        DEBUG("[cc26x2_cc13x2_rf]: no free RX entry\n");
        return -ENOBUFS;
    }

    /* RX is not running, so the command structure may be written */
    rf_cmd_prop_rx_adv.status = RFC_IDLE;
    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_prop_rx_adv);
    if ((cmdsta & 0xFF) == RFC_CMDSTA_SCHEDULINGERROR) {
        DEBUG("[cc26x2_cc13x2_rf]: RX rejected, another command is running\n");
        return -EBUSY;
    }
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: RX rejected, cmdsta=%08lx\n", cmdsta);
        return -EIO;
    }

    _rx_submitted = true;

    return 0;
}

int cc26x2_cc13x2_rf_rx_start(void)
{
    /* RX is started and stopped from threads and from interrupts: the check,
     * the submission and the update of _rx_submitted must not be split */
    unsigned key = irq_disable();
    int res = _rx_start();
    irq_restore(key);

    return res;
}

/*
 * Ends CMD_PROP_RX_ADV with CMD_ABORT and waits until the command has ended,
 * with PROP_DONE_ABORT (0x3405, [TRM] Table 25-186). A frame being received
 * at that moment is dropped: an aborted reception is removed from the RX
 * queue right away ([TRM] 25.10.4.1).
 *
 * After this function returns the radio CPU is idle, so a new radio operation
 * command can be submitted.
 */
void cc26x2_cc13x2_rf_rx_stop(void)
{
    unsigned key = irq_disable();

    if (_rx_submitted) {
        int res = cc26x2_cc13x2_rfc_abort_cmd(&rf_cmd_prop_rx_adv.status);
        if (res == 0) {
            _rx_submitted = false;
        }
        else {
            /* RX is still running and stays marked as such */
            DEBUG("[cc26x2_cc13x2_rf]: could not stop RX (%d)\n", res);
        }
    }

    irq_restore(key);
}

/*
 * Ends CMD_PROP_RX_ADV with CMD_STOP, which leaves a frame that is being
 * received alone, see cc26x2_cc13x2_rfc_stop_cmd(). This is how the driver
 * learns that a reception is in progress: the RF Core has no other
 * documented way to tell.
 *
 * If it returns -EBUSY the receive command goes on until the end of the
 * frame and then ends with PROP_DONE_STOPPED (0x3404). The frame is handled
 * as any other, and cc26x2_cc13x2_rf_rx_start() starts RX again.
 */
int cc26x2_cc13x2_rf_rx_stop_unless_receiving(void)
{
    int res = 0;
    unsigned key = irq_disable();

    if (_rx_submitted) {
        res = cc26x2_cc13x2_rfc_stop_cmd(&rf_cmd_prop_rx_adv.status);
        if (res == 0) {
            _rx_submitted = false;
        }
    }

    irq_restore(key);

    return res;
}

bool cc26x2_cc13x2_rf_rx_has_ended(void)
{
    return !_rx_is_on();
}

/**
 * @brief   Longest wait for the first RSSI value of a receiver that has just
 *          started, in microseconds
 */
#define CCA_RSSI_WAIT_US                (400U)

/*
 * CCA from the receiver that is running, as the RF Core does it itself in
 * its IEEE 802.15.4 mode, where "while the receiver is running, the radio CPU
 * monitors some signals for use in clear-channel assessment" and the CSMA-CA
 * operation "checks the CCA state from the background-level operation"
 * ([TRM] 25.5.4.1.5 and 25.5.4.3). The proprietary mode has no such state,
 * but the same source: the direct command CMD_GET_RSSI returns the RSSI of
 * the running receiver, in dBm in byte 2 of CMDSTA, or -128 if there is none
 * yet ([TRM] 25.3.3.2.3). Measured on a CC1312R with CMD_PROP_RX_ADV:
 *
 * - The command takes 10 us and does not disturb a reception.
 * - The first value is there 95 to 205 us after RX started, from 300 down to
 *   50 ksym/s, and a new one about every 3.5 symbol periods after that.
 * - On a free channel it reads -122 to -96 dBm. When a frame starts it is at
 *   the level of the frame within one or two values, at the very start of
 *   the preamble, long before the sync word.
 *
 * The channel is busy at the first value at or above the threshold, CCA mode
 * 1 of the standard, and clear when the values stayed below it for
 * phyCcaDuration. RX is not touched: with the carrier sense command instead,
 * RX has to be stopped first, and a frame whose preamble is on the air at
 * that moment is lost.
 */
int cc26x2_cc13x2_rf_rx_cca(void)
{
    uint32_t start = RFC_RAT->RATCNT;
    uint32_t first = 0;
    bool valid = false;

    while (_rx_is_on()) {
        uint32_t cmdsta =
            cc26x2_cc13x2_rfc_request_execute(RFC_CMDR_DIR_CMD(RFC_CMD_GET_RSSI));
        uint32_t now = RFC_RAT->RATCNT;
        int8_t rssi = (int8_t)(cmdsta >> 16);

        if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
            break;
        }
        if (rssi == -128) {
            /* the RAT counts 250 ns */
            if ((now - start) > (CCA_RSSI_WAIT_US * 4)) {
                break;
            }
        }
        else if (rssi >= rf_cmd_prop_cs.rssi_thr) {
            return 1;
        }
        else if (!valid) {
            valid = true;
            first = now;
        }
        else if ((now - first) >= (CC26X2_CC13X2_RF_CCA_DURATION_US * 4)) {
            return 0;
        }
    }

    return -ENODEV;
}

/*
 * [TRM] 25.10.5.2.1, note: "The IEEE 802.15.4g PHY header must be presented
 * MSB first to the RF Core. In IEEE 802.15.4g specification, the payload part
 * is LSb first, however the payload length info in physical layer header
 * (PHR) is MSb first. This means that the payload needs to be flipped in the
 * System CPU. This can be achieved with the System CPU assembly instruction
 * RBIT."
 *
 * Checked against an AT86RF215: without the reversal its frames arrive with
 * a correct CRC, which is computed over the bit stream, and every byte
 * bit-reversed.
 */
void cc26x2_cc13x2_rf_copy_psdu(uint8_t *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        /* RBIT reverses the 32 bits of a register */
        dst[i] = (uint8_t)(__RBIT(src[i]) >> 24);
    }
}

static bool _rx_is_kept(const rfc_data_entry_general_t *entry)
{
    for (unsigned i = 0; i < _rx_kept_numof; i++) {
        if (_rx_kept[i] == entry) {
            return true;
        }
    }

    return false;
}

rfc_data_entry_general_t *cc26x2_cc13x2_rf_rx_next(void)
{
    rfc_data_entry_general_t *res = NULL;
    unsigned key = irq_disable();

    for (unsigned i = 0; (i < CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF) && (res == NULL); i++) {
        rfc_data_entry_general_t *entry = (rfc_data_entry_general_t *)&_rx_buf[i].entry;

        if ((((volatile rfc_data_entry_general_t *)entry)->status == RFC_DATA_ENTRY_FINISHED) &&
            !_rx_is_kept(entry)) {
            res = entry;
        }
    }
    irq_restore(key);

    return res;
}

bool cc26x2_cc13x2_rf_rx_keep(rfc_data_entry_general_t *entry)
{
    bool res = false;
    unsigned key = irq_disable();

    if (_rx_kept_numof < RX_KEPT_MAX) {
        _rx_kept[_rx_kept_numof++] = entry;
        res = true;
    }
    irq_restore(key);

    return res;
}

rfc_data_entry_general_t *cc26x2_cc13x2_rf_recv(void)
{
    rfc_data_entry_general_t *res;
    unsigned key = irq_disable();

    res = _rx_kept_numof ? _rx_kept[0] : NULL;
    irq_restore(key);

    return res;
}

void cc26x2_cc13x2_rf_rx_release(void)
{
    unsigned key = irq_disable();

    if (_rx_kept_numof) {
        _rx_kept[0]->status = RFC_DATA_ENTRY_PENDING;
        _rx_kept_numof--;
        for (unsigned i = 0; i < _rx_kept_numof; i++) {
            _rx_kept[i] = _rx_kept[i + 1];
        }
    }
    irq_restore(key);
}

void cc26x2_cc13x2_rf_rx_release_last(void)
{
    unsigned key = irq_disable();

    if (_rx_kept_numof) {
        _rx_kept[--_rx_kept_numof]->status = RFC_DATA_ENTRY_PENDING;
    }
    irq_restore(key);
}

void cc26x2_cc13x2_rf_rx_release_all(void)
{
    unsigned key = irq_disable();

    while (_rx_kept_numof) {
        _rx_kept[--_rx_kept_numof]->status = RFC_DATA_ENTRY_PENDING;
    }
    irq_restore(key);
}

/* Writes the PHR and the PSDU of a frame into a transmit buffer. Returns
 * their length, which is what CMD_PROP_TX_ADV takes as packet length. */
static int _frame_write(uint8_t *tx_buf, size_t tx_buf_len, const iolist_t *iolist)
{
    const size_t max_psdu_len = tx_buf_len - IEEE802154_PHY_MR_FSK_PHR_LEN;
    const size_t fcs_len = sizeof(uint16_t);
    size_t total_len;
    size_t psdu_len = 0;

    /* PHY header size reserved (two bytes), after the PHR the
     * PSDU starts */
    uint8_t *buf_pos = tx_buf + IEEE802154_PHY_MR_FSK_PHR_LEN;
    for (const iolist_t *iol = iolist; iol; iol = iol->iol_next) {
        psdu_len += iol->iol_len;
        if (psdu_len > max_psdu_len) {
            return -EOVERFLOW;
        }

        cc26x2_cc13x2_rf_copy_psdu(buf_pos, iol->iol_base, iol->iol_len);
        buf_pos += iol->iol_len;
    }

    /* Length in MR-FSK PHR. Includes the PSDU + FCS */
    total_len = psdu_len + fcs_len;
    if (total_len > IEEE802154G_FRAME_LEN_MAX) {
        return -EOVERFLOW;
    }

    /* Prepare the MR-FSK PHR
     * - No mode switching.
     * - Total length: PSDU + FCS length
     * - FCS type (CRC-16)
     *
     * Total length = len (payload) + CRC length
     *
     * The Radio will flip the bits around, so tx_buf[0] must have the
     * length LSBs (PHR[15:8] and tx_buf[1] will have PHR[7:0]
     *
     * This is the example of [TRM] 25.10.5.2.1 with the CRC-16 bit added.
     * The radio reads the 16-bit header from the buffer in little-endian
     * order and sends it MSB first ([TRM] 25.10.5.3.2), so tx_buf[1] goes
     * out before tx_buf[0]. With `whiten_mode = 7` the radio CPU reads
     * these header bits, counted from the LSB of the 16-bit header:
     *
     * - bit 11, 0x08 in tx_buf[1]: whitening enabled,
     * - bit 12, 0x10 in tx_buf[1]: 16-bit CRC instead of the 32-bit one,
     * - bit 15: header-only frame, never set here.
     *
     * Only the PHR is given in this form. The PSDU bytes were bit-reversed
     * when they were copied into the buffer, see
     * cc26x2_cc13x2_rf_copy_psdu(). */
    tx_buf[0] = (((total_len) >> 0) & 0xFF);
    tx_buf[1] = (((total_len) >> 8) & 0xFF) + 0x08 + 0x10;

    /* The length of the buffer used to transmit is the PHR + PSDU,
     * it does not include the FCS size as the radio writes it on
     * it's own depending on the configured FCS, which is CRC-16 in
     * our case */
    return IEEE802154_PHY_MR_FSK_PHR_LEN + psdu_len;
}

int cc26x2_cc13x2_rf_write(const iolist_t *iolist)
{
    int res = _frame_write(_tx_buf, sizeof(_tx_buf), iolist);

    if (res < 0) {
        return res;
    }
    _tx_pkt_len = res;

    return 0;
}

int cc26x2_cc13x2_rf_write_ack(const iolist_t *iolist)
{
    int res = _frame_write(_tx_ack_buf, sizeof(_tx_ack_buf), iolist);

    if (res < 0) {
        return res;
    }
    _tx_ack_pkt_len = res;

    return 0;
}

/*
 * Starts CMD_PROP_TX_ADV with the frame prepared by cc26x2_cc13x2_rf_write().
 * CMDSTA Done means that the command was accepted. The frame has been sent
 * when LAST_COMMAND_DONE is raised and the status field is PROP_DONE_OK
 * (0x3400, [TRM] Table 25-184).
 *
 * With `cca` the RF Core runs CMD_PROP_CS first and only goes on to
 * CMD_PROP_TX_ADV if the channel is clear, see rf_cmd_prop_cs. On a busy
 * channel LAST_COMMAND_DONE is raised when the carrier sense ends, and the
 * transmit command keeps the status IDLE: cc26x2_cc13x2_rf_tx_chain_state()
 * tells the cases apart.
 *
 * With `wait_ack` the RF Core goes on from CMD_PROP_TX_ADV, if the frame was
 * sent, to the receive command of the ACK, see rf_cmd_prop_rx_ack.
 * LAST_COMMAND_DONE is then raised when that one ends.
 *
 * With `ack` the frame is the one prepared by cc26x2_cc13x2_rf_write_ack(),
 * sent at once and alone.
 *
 * SchedulingError means that another radio operation command is still
 * running. The caller must stop RX with cc26x2_cc13x2_rf_rx_stop() before:
 * the ring of the RX queue is also rebuilt here for the wait.
 */
static int _request_transmit(bool ack, bool cca, bool wait_ack)
{
    uint32_t cmdsta;
    uintptr_t cmd = (uintptr_t)&rf_cmd_prop_tx_adv;

    rf_cmd_prop_tx_adv.pkt = ack ? _tx_ack_buf : _tx_buf;
    rf_cmd_prop_tx_adv.pkt_len = ack ? _tx_ack_pkt_len : _tx_pkt_len;
    rf_cmd_prop_tx_adv.status = RFC_IDLE;
    rf_cmd_prop_cs.status = RFC_IDLE;
    rf_cmd_prop_rx_ack.status = RFC_IDLE;
    if (wait_ack) {
        /* the receive command needs a free entry for the ACK; if a frame
         * is still to be looked at, the interrupt handler frees one */
        if (_rx_ring_build() != 0) {
            return -EBUSY;
        }
        rf_cmd_prop_tx_adv.next_op = (rfc_op_t *)&rf_cmd_prop_rx_ack;
        rf_cmd_prop_tx_adv.condition.rule = RFC_COND_STOP_ON_FALSE;
    }
    else {
        rf_cmd_prop_tx_adv.next_op = NULL;
        rf_cmd_prop_tx_adv.condition.rule = RFC_COND_NEVER;
    }
    if (cca) {
        rf_cmd_prop_cs.next_op = (rfc_op_t *)&rf_cmd_prop_tx_adv;
        rf_cmd_prop_cs.condition.rule = RFC_COND_STOP_ON_TRUE;
        cmd = (uintptr_t)&rf_cmd_prop_cs;
    }

    cmdsta = cc26x2_cc13x2_rfc_request_execute(cmd);
    switch (cmdsta & 0xFF) {
        case RFC_CMDSTA_DONE:
            break;
        /* previous command, if any, has not been finished */
        case RFC_CMDSTA_SCHEDULINGERROR:
            return -EBUSY;
        default:
            DEBUG("[cc26x2_cc13x2_rf]: couldn't send TX command, cmdsta=%08lx\n",
                  cmdsta);
            return -EIO;
    }

    return 0;
}

int cc26x2_cc13x2_rf_request_transmit(bool cca, bool wait_ack)
{
    return _request_transmit(false, cca, wait_ack);
}

int cc26x2_cc13x2_rf_request_transmit_ack(void)
{
    return _request_transmit(true, false, false);
}

int cc26x2_cc13x2_rf_tx_chain_state(bool cca, bool wait_ack)
{
    uint16_t cs = rf_cmd_prop_cs.status;
    uint16_t tx = rf_cmd_prop_tx_adv.status;

    if (cc26x2_cc13x2_rfc_cmd_has_ended(tx)) {
        if (tx != RFC_PROP_DONE_OK) {
            return -EIO;
        }
        /* however the wait for the ACK ended, the frame was sent: what
         * was received tells whether the ACK came */
        if (wait_ack &&
            !cc26x2_cc13x2_rfc_cmd_has_ended(rf_cmd_prop_rx_ack.status)) {
            return -EAGAIN;
        }
        return 0;
    }
    if (cca && cc26x2_cc13x2_rfc_cmd_has_ended(cs) && (cs != RFC_PROP_DONE_IDLE)) {
        /* the chain stopped at the carrier sense */
        return ((cs == RFC_PROP_DONE_BUSY) || (cs == RFC_PROP_DONE_BUSYTIMEOUT))
               ? -EBUSY : -EIO;
    }

    return -EAGAIN;
}

void cc26x2_cc13x2_rf_set_cca_threshold(int8_t threshold)
{
    rf_cmd_prop_cs.rssi_thr = threshold;
}

/*
 * Runs CMD_PROP_RADIO_DIV_SETUP chained with CMD_FS, with what
 * cc26x2_cc13x2_rf_set_fsk_mode() and cc26x2_cc13x2_rf_set_tx_pwr() left in
 * the commands. The radio must be idle.
 */
int cc26x2_cc13x2_rf_run_setup(void)
{
    uint32_t cmdsta;

    rf_cmd_prop_radio_div_setup.status = RFC_IDLE;
    rf_cmd_fs.status = RFC_IDLE;

    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_prop_radio_div_setup);
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: setup rejected, cmdsta=%08lx\n", cmdsta);
        return -EIO;
    }
    /* if the setup command fails, the chain ends there and CMD_FS keeps its
     * IDLE status ([TRM] 25.3.2.5.2) */
    if ((_wait_cmd(&rf_cmd_prop_radio_div_setup.status) != 0) ||
        (rf_cmd_prop_radio_div_setup.status != RFC_PROP_DONE_OK) ||
        (_wait_cmd(&rf_cmd_fs.status) != 0) ||
        (rf_cmd_fs.status != RFC_DONE_OK)) {
        DEBUG("[cc26x2_cc13x2_rf]: setup failed, status: setup=%04x FS=%04x\n",
              rf_cmd_prop_radio_div_setup.status, rf_cmd_fs.status);
        return -EIO;
    }

    return 0;
}

/*
 * Changes the channel with a single CMD_FS and waits for it to end.
 *
 * - CMD_FS is a radio operation command ([TRM] 25.3.3.1.5). If RX is running
 *   it is rejected with SchedulingError and this function returns -EIO, so
 *   the radio must be idle.
 * - The end of the command is taken from its status field, see _wait_cmd().
 *
 * The frequency is CHAN_CENTER_FREQ0 + channel * CHAN_SPACING. The setup
 * command keeps the `center_freq` it was given at power-on.
 */
int cc26x2_cc13x2_rf_change_chan(uint16_t channel)
{
    uint32_t cmdsta;

    if (!cc26x2_cc13x2_rf_chan_is_valid(channel)) {
        DEBUG("[cc26x2_cc13x2_rf]: invalid channel value=%u\n", channel);
        return -EINVAL;
    }

    uint64_t new_freq  = cc26x2_cc13x2_rf_chan_freq(channel);
    cc26x2_cc13x2_rf_freq_convert(new_freq,
                                  &rf_cmd_fs.frequency,
                                  &rf_cmd_fs.fract_freq);

    rf_cmd_fs.status = RFC_IDLE;
    cmdsta = cc26x2_cc13x2_rfc_request_execute((uintptr_t)&rf_cmd_fs);
    if ((cmdsta & 0xFF) != RFC_CMDSTA_DONE) {
        DEBUG("[cc26x2_cc13x2_rf]: couldn't change channel, cmdsta=%08lx\n",
              cmdsta);
        return -EIO;
    }
    if ((_wait_cmd(&rf_cmd_fs.status) != 0) || (rf_cmd_fs.status != RFC_DONE_OK)) {
        DEBUG("[cc26x2_cc13x2_rf]: CMD_FS failed, status=%04x\n",
              rf_cmd_fs.status);
        return -EIO;
    }

    return 0;
}

void cc26x2_cc13x2_rf_set_short_addr(network_uint16_t short_addr)
{
    memcpy(_short_addr, short_addr.u8, IEEE802154_SHORT_ADDRESS_LEN);
}

void cc26x2_cc13x2_rf_set_long_addr(eui64_t long_addr)
{
    memcpy(_long_addr, long_addr.uint8, IEEE802154_LONG_ADDRESS_LEN);
}

void cc26x2_cc13x2_rf_set_pan_id(uint16_t pan_id)
{
    _pan_id = pan_id;
}

bool cc26x2_cc13x2_rf_l2_filter(uint8_t *mhr)
{
    uint8_t dst_addr[IEEE802154_LONG_ADDRESS_LEN];
    le_uint16_t dst_pan;
    uint8_t pan_bcast[] = IEEE802154_PANID_BCAST;

    int addr_len = ieee802154_get_dst(mhr, dst_addr, &dst_pan);
    if (addr_len < 0) {
        return false;
    }

    /* filter PAN ID */
    /* Will only work on little endian platform (all cc26xx/cc13xx ;-) ) */
    if ((memcmp(pan_bcast, dst_pan.u8, 2) != 0) &&
        (memcmp(&_pan_id, dst_pan.u8, 2) != 0)) {
        return false;
    }

    /* check destination address */
    if (((addr_len == IEEE802154_SHORT_ADDRESS_LEN) &&
          (memcmp(_short_addr, dst_addr, addr_len) == 0 ||
           memcmp(ieee802154_addr_bcast, dst_addr, addr_len) == 0)) ||
        ((addr_len == IEEE802154_LONG_ADDRESS_LEN) &&
          (memcmp(_long_addr, dst_addr, addr_len) == 0))) {
        return true;
    }

    return false;
}
