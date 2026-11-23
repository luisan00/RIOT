/*
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     cpu_cc26x2_cc13x2_rf
 * @{
 *
 * @file
 * @brief       CC13x2/CC26x2 2.4 GHz IEEE 802.15.4 O-QPSK radio driver
 *
 * Uses the IEEE 802.15.4 mode of the RF Core. The references in the comments
 * are:
 *
 * - [TRM]: CC13x2, CC26x2 SimpleLink Wireless MCU Technical Reference Manual,
 *   SWCU185G, chapter 25.
 * - [SDK]: SimpleLink CC13xx/CC26xx SDK 8.33.00.16.
 * - [RFCFG]: the radio settings of [SDK],
 *   `source/ti/devices/radioconfig/.meta/config`.
 *
 * In this mode the RF Core runs two levels of radio operation commands
 * ([TRM] 25.5.4). The receiver is a background level operation, and a
 * CSMA-CA, a transmission and the reception of an ACK run on top of it as a
 * chain of foreground level operations. The driver uses that as follows:
 *
 * - RX: CMD_IEEE_RX with the frame filter, the automatic ACK and the source
 *   matching of the RF Core.
 * - TX and CCA: the radio HAL asks for them with the radio idle, so the
 *   driver starts the receiver for the duration of the operation, set up to
 *   store nothing and to send no ACK. It gives the CCA of the CSMA-CA and
 *   detects the ACK of the frame that was sent.
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include "byteorder.h"
#include "cpu.h"
#include "irq.h"
#include "net/ieee802154.h"
#include "net/ieee802154/radio.h"
#include "random.h"

#include "cc26x2_cc13x2_rf.h"
#include "cc26x2_cc13x2_rfc.h"
#include "cc26xx_cc13xx_rfc_ieee_cmd.h"
#include "cc26xx_cc13xx_rfc_ieee_mailbox.h"
#include "cc26xx_cc13xx_rfc_mailbox.h"
#include "rf_conf.h"
#include "vendor/rf_patch_cpe_ieee_802_15_4.h"

#define ENABLE_DEBUG 0
#include "debug.h"

/**
 * @brief   Number of receive buffers
 */
#ifndef CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF
#  define CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF (4)
#endif

/**
 * @brief   Board hook to select the 2.4 GHz RF path
 */
#ifndef CC26X2_CC13X2_RF_FRONTEND_SETUP_24GHZ
#  define CC26X2_CC13X2_RF_FRONTEND_SETUP_24GHZ()
#endif

/**
 * @name    Timing of the O-QPSK PHY in the 2.4 GHz band
 *
 * The data rate is 250 kb/s (IEEE Std 802.15.4-2024, 13.2.2): an octet takes
 * 32 us, two symbols of 16 us. The radio timer (RAT) runs at 4 MHz.
 * @{
 */
#define RAT_TICKS_PER_US        (4U)
#define OCTET_US                (32U)
/** preamble (4), SFD (1) and PHR (1) */
#define SHR_PHR_LEN             (6U)
/** macSifsPeriod and macLifsPeriod, 12 and 40 symbols */
#define SIFS_US                 (192U)
#define LIFS_US                 (640U)
/** macAckWaitDuration, 54 symbols for this PHY */
#define ACK_WAIT_US             (864U)
/** @} */

/**
 * @brief   Correlation peaks for the carrier sense of the CCA
 *
 * The correlator based carrier sense is busy if it saw more than this many
 * correlation peaks in the last 8 symbol periods, 3 at most ([TRM]
 * 25.5.4.1.5). An O-QPSK signal gives one per symbol. With 0, noise alone
 * made a clear channel busy in 40 % of the CCAs on a CC1352P1.
 */
#define CCA_CORR_THR            (3U)

/**
 * @name    Receive buffers
 *
 * A general data entry holds one frame: a length byte, the PSDU without the
 * FCS and the RSSI ([TRM] 25.5.3.1 and the `rxConfig` of the receive
 * command).
 * @{
 */
#define RX_APPENDED_LEN         (1U)
#define RX_ELEMENT_LEN          (1U + IEEE802154_FRAME_LEN_MAX - IEEE802154_FCS_LEN + \
                                 RX_APPENDED_LEN)
#define RX_BUF_LEN              ((sizeof(rfc_data_entry_t) + RX_ELEMENT_LEN + 3U) & ~3U)

typedef union {
    rfc_data_entry_t entry;
    uint8_t raw[RX_BUF_LEN];
} rx_buf_t;

static rx_buf_t _rx_buf[CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF] __attribute__((aligned(4)));
static rfc_data_queue_t _rx_queue;
static rfc_ieee_rx_output_t _rx_output;
/** @} */

/** PSDU to transmit, without the FCS */
static uint8_t _tx_buf[IEEE802154_FRAME_LEN_MAX - IEEE802154_FCS_LEN];

/*
 * Overrides of TI's setting "IEEE 802.15.4-2006, 250 kbps, OQPSK, DSSS = 1:8"
 * ([RFCFG], `cc1352p_ieee_15_4_pg21/cmd_settings/setting_ieee_802_15_4.json`,
 * the same two for the CC1352R). The coexistence entries of that setting
 * belong to an optional feature and are left out.
 */
static uint32_t _overrides[] = {
    /* Rx: Set LNA bias current offset to +15 to saturate trim to max
     * (default: 0) */
    (uint32_t)0x000F8883,
    /* Tx: Set DCDC settings IPEAK=3, dither = off */
    (uint32_t)0x00F388D3,
    RFC_END_OVERRIDE,
};

/*
 * Setup chain, as in the sub-GHz driver: start the radio timer, then set the
 * RF Core up. CMD_RADIO_SETUP with `mode` 1 selects the IEEE 802.15.4 mode
 * and `lo_divider` 0 the 2.4 GHz band ([TRM] 25.3.3.1.2). The LaunchPads use
 * the differential front end with external bias in this band ([RFCFG],
 * `target_launchxl-cc1352p_2_4_ghz.json`).
 *
 * There is no CMD_FS: the receive command programs the synthesizer for its
 * channel ([TRM] 25.5.4.1).
 */
static rfc_cmd_radio_setup_t _cmd_radio_setup = {
    .command_no = RFC_CMD_RADIO_SETUP,
    .start_trigger = { .type = RFC_TRIG_NOW },
    .condition = { .rule = RFC_COND_NEVER },
    .mode = RFC_RADIO_SETUP_MODE_IEEE802154,
    .lo_divider = 0,
    .config = { .front_end_mode = 0, .bias_mode = 1 },
    .reg_override = _overrides,
};

static rfc_cmd_sync_start_rat_t _cmd_sync_start_rat = {
    .command_no = RFC_CMD_SYNC_START_RAT,
    .next_op = &_cmd_radio_setup,
    .start_trigger = { .type = RFC_TRIG_NOW },
    .condition = { .rule = RFC_COND_ALWAYS },
};

/* power-down, see cc26x2_cc13x2_rf_off() of the sub-GHz driver */
static rfc_cmd_sync_stop_rat_t _cmd_sync_stop_rat = {
    .command_no = RFC_CMD_SYNC_STOP_RAT,
    .start_trigger = { .type = RFC_TRIG_NOW },
    .condition = { .rule = RFC_COND_NEVER },
};

static rfc_cmd_fs_powerdown_t _cmd_fs_powerdown = {
    .command_no = RFC_CMD_FS_POWERDOWN,
    .next_op = &_cmd_sync_stop_rat,
    .start_trigger = { .type = RFC_TRIG_NOW },
    .condition = { .rule = RFC_COND_ALWAYS },
};

static rfc_cmd_set_tx_power_t _cmd_set_tx_power = {
    .command_no = RFC_CMD_SET_TX_POWER,
};

/*
 * Receiver, [TRM] 25.5.4.1. The frame filter, the frame types and the
 * automatic ACK depend on what the receiver is started for, see _rx_start().
 *
 * - Frames with a CRC error and frames rejected by the filter are removed
 *   from the queue, and the reception of a rejected frame stops at once.
 * - A stored frame has the RSSI appended.
 */
static rfc_cmd_ieee_rx_t _cmd_rx = {
    .command_no = RFC_CMD_IEEE_RX,
    .start_trigger = { .type = RFC_TRIG_NOW },
    .condition = { .rule = RFC_COND_NEVER },
    .rx_config = {
        .auto_flush_crc = 1,
        .auto_flush_ign = 1,
        .append_rssi = 1,
    },
    .rx_queue = &_rx_queue,
    .output = &_rx_output,
    .cca_rssi_thr = CONFIG_IEEE802154_CCA_THRESH_DEFAULT,
    .end_trigger = { .type = RFC_TRIG_NEVER },
};

/*
 * CSMA-CA, [TRM] 25.5.4.3. The RF Core draws the backoffs, of 320 us each,
 * and takes the CCA from the receiver. `be`, `mac_max_be` and
 * `mac_max_csma_backoffs` are set for each transmission.
 */
static rfc_cmd_ieee_csma_t _cmd_csma = {
    .command_no = RFC_CMD_IEEE_CSMA,
    .condition = { .rule = RFC_COND_STOP_ON_FALSE },
    .csma_config = { .init_cw = 1, .slotted = 0, .rx_off_mode = 0 },
    .end_trigger = { .type = RFC_TRIG_NEVER },
};

/* Transmission, [TRM] 25.5.4.4. The RF Core adds the PHY header and the FCS. */
static rfc_cmd_ieee_tx_t _cmd_tx = {
    .command_no = RFC_CMD_IEEE_TX,
    .payload = _tx_buf,
};

/*
 * Reception of the ACK, [TRM] 25.5.4.5. It ends with IEEE_DONE_ACK or
 * IEEE_DONE_ACKPEND when the ACK with the sequence number of the frame
 * arrives, or with IEEE_DONE_TIMEOUT macAckWaitDuration after its start, the
 * end of the transmission.
 */
static rfc_cmd_ieee_rx_ack_t _cmd_rx_ack = {
    .command_no = RFC_CMD_IEEE_RX_ACK,
    .start_trigger = { .type = RFC_TRIG_NOW },
    .condition = { .rule = RFC_COND_NEVER },
    .end_trigger = { .type = RFC_TRIG_REL_START },
    .end_time = ACK_WAIT_US * RAT_TICKS_PER_US,
};

enum {
    POWER_OFF,
    POWER_RFC_ON,       /**< waiting for the boot of the RF Core */
    POWER_SETUP,        /**< waiting for the setup chain */
    POWER_ON,
};

static uint8_t _power = POWER_OFF;

enum {
    BG_NONE,            /**< the receiver is off */
    BG_RX,              /**< receiving, for the upper layer */
    BG_QUIET,           /**< receiver on for a TX or a CCA: stores nothing */
};

enum {
    FG_NONE,
    FG_TX,              /**< CSMA-CA, transmission and ACK reception */
    FG_CCA,             /**< a single CCA */
};

static volatile uint8_t _bg = BG_NONE;
static volatile uint8_t _fg = FG_NONE;

/** The receive command was asked to stop while a frame was coming in */
static volatile bool _rx_stopping;
/** A received frame is indicated once its ACK has been sent */
static volatile bool _rx_ind_pending;

static struct {
    ieee802154_filter_mode_t filter;    /**< frame filter mode */
    bool pending;                       /**< frame pending bit of the ACKs */
    bool pan_coord;                     /**< the device is PAN coordinator */
    int8_t csma_retries;                /**< < 0: no CCA, 0: one CCA, > 0:
                                             CSMA-CA with that many backoffs */
    ieee802154_csma_be_t csma_be;       /**< backoff exponents */
    uint8_t retrans;                    /**< retransmissions of a frame */
    uint8_t channel;
} _cfg = {
    .filter = IEEE802154_FILTER_ACCEPT,
    .csma_retries = CONFIG_IEEE802154_DEFAULT_CSMA_CA_RETRIES,
    .csma_be = { .min = CONFIG_IEEE802154_DEFAULT_CSMA_CA_MIN_BE,
                 .max = CONFIG_IEEE802154_DEFAULT_CSMA_CA_MAX_BE },
    .retrans = CONFIG_IEEE802154_DEFAULT_MAX_FRAME_RETRANS,
    .channel = CONFIG_IEEE802154_DEFAULT_CHANNEL,
};

/* state of the transmission in progress */
static struct {
    bool csma;          /**< the chain starts with the CSMA-CA command */
    bool ack_req;       /**< the frame asks for an ACK */
    bool own_bg;        /**< the receiver was started for this operation */
    uint8_t retrans;    /**< retransmissions done */
} _tx;

static volatile int _tx_result = -EAGAIN;
static uint8_t _tx_retrans;
static volatile int _cca_result = -EAGAIN;

/* Earliest start of the next transmission, to keep the interframe spacing */
static uint32_t _ifs_end;
static bool _ifs_valid;

static ieee802154_dev_t *_hal;

static bool _ended(uint16_t status)
{
    return cc26x2_cc13x2_rfc_cmd_has_ended(status);
}

static bool _exec(void *cmd)
{
    return (cc26x2_cc13x2_rfc_request_execute((uintptr_t)cmd) & 0xFF) == RFC_CMDSTA_DONE;
}

/*
 * Starts the receiver, [TRM] 25.5.4.1.
 *
 * For the upper layer (`quiet` false) the frame filter follows the filter
 * mode of the radio HAL:
 *
 * - ACCEPT: third level filtering with the addresses of the device; beacon,
 *   data and MAC command frames are stored and acknowledged if they ask for
 *   it ([TRM] 25.5.4.1.3). The RF Core sends the ACK 192 us after the frame.
 *   ACK frames are not stored: the driver waits for the ACK of a frame it
 *   sent with the ACK receive command.
 * - PROMISC and SNIFFER: no filtering, every frame with a good FCS is stored
 *   and none is acknowledged.
 *
 * For a transmission or a CCA (`quiet` true) no frame type is accepted, so
 * nothing is stored or acknowledged. The receiver still maintains the CCA,
 * and the ACK receive command still sees the ACK it waits for: "the packet
 * is processed further even if frameTypes.bAcceptFt2Ack is 0" ([TRM]
 * 25.5.4.1.1.1).
 *
 * The CCA is busy while a frame is being received, whatever the mode
 * (`cca_en_sync`, `cca_sync_op` 0), as the standard asks ([TRM] 25.5.4.1.5).
 */
static int _rx_start(bool quiet)
{
    bool filter = quiet || (_cfg.filter == IEEE802154_FILTER_ACCEPT);
    bool accept = !quiet && (_cfg.filter == IEEE802154_FILTER_ACCEPT);

    _cmd_rx.status = RFC_IDLE;
    _cmd_rx.channel = _cfg.channel;
    _cmd_rx.frame_filt_opt = (rfc_ieee_frame_filt_opt_t) {
        .frame_filt_en = filter,
        .frame_filt_stop = 1,
        .auto_ack_en = accept,
        .default_pend = _cfg.pending,
        .pan_coord = _cfg.pan_coord,
        .max_frame_version = 3,
    };
    _cmd_rx.frame_types = (rfc_ieee_frame_types_t) {
        .accept_ft0_beacon = accept,
        .accept_ft1_data = accept,
        .accept_ft3_mac_cmd = accept,
    };
    _cmd_rx.cca_opt.cca_en_sync = 1;
    _cmd_rx.cca_opt.cca_sync_op = 0;
    _cmd_rx.cca_opt.cca_corr_thr = CCA_CORR_THR;

    if (!_exec(&_cmd_rx)) {
        DEBUG("[cc26x2_cc13x2_rf]: CMD_IEEE_RX rejected\n");
        return -EIO;
    }
    _bg = quiet ? BG_QUIET : BG_RX;

    return 0;
}

/* Ends the receiver and any foreground operation at once ([TRM] 25.5.4.1.4) */
static void _rx_abort(void)
{
    unsigned key = irq_disable();

    if (_bg != BG_NONE) {
        cc26x2_cc13x2_rfc_abort_cmd(&_cmd_rx.status);
        _bg = BG_NONE;
    }
    _rx_stopping = false;
    _rx_ind_pending = false;

    irq_restore(key);
}

/* Interframe spacing after the frame that was just sent (IEEE Std
 * 802.15.4-2024, 6.3.1): its end is the time stamp of its start, written by
 * the RF Core, plus its duration. */
static void _ifs_start(void)
{
    unsigned psdu_len = _cmd_tx.payload_len + IEEE802154_FCS_LEN;
    unsigned ifs = (psdu_len > IEEE802154_SIFS_MAX_FRAME_SIZE) ? LIFS_US : SIFS_US;

    _ifs_end = _cmd_tx.timestamp +
               (((SHR_PHR_LEN + psdu_len) * OCTET_US) + ifs) * RAT_TICKS_PER_US;
    _ifs_valid = true;
}

/* CSMA-CA with `retries` backoffs after a busy channel; 0 is a single CCA
 * without any backoff before it ([TRM] 25.5.4.3) */
static void _csma_prepare(int8_t retries)
{
    _cmd_csma.status = RFC_IDLE;
    _cmd_csma.start_trigger = (rfc_trigger_t) { .type = RFC_TRIG_NOW };
    _cmd_csma.start_time = 0;
    _cmd_csma.nb = 0;
    _cmd_csma.remaining_periods = 0;
    /* the seed of the pseudo-random generator must not be 0 */
    _cmd_csma.random_state = (uint16_t)random_uint32() | 1;
    _cmd_csma.be = (retries > 0) ? _cfg.csma_be.min : 0;
    _cmd_csma.mac_max_be = (retries > 0) ? _cfg.csma_be.max : 0;
    _cmd_csma.mac_max_csma_backoffs = (retries > 0) ? retries : 0;
}

/*
 * Submits the foreground chain of a transmission, on top of the receiver
 * ([TRM] 25.5.4 and Table 25-81):
 *
 *   [CMD_IEEE_CSMA] -> CMD_IEEE_TX [-> CMD_IEEE_RX_ACK]
 *
 * - CSMA-CA, unless the transmission is direct. Its result is FALSE if the
 *   channel stays busy, which stops the chain (COND_STOP_ON_FALSE).
 * - The ACK reception if the frame asks for an ACK.
 *
 * The first command waits for the end of the interframe spacing: its start
 * trigger is that absolute time, and "now" once it has passed.
 */
static int _fg_submit(void)
{
    rfc_op_t *first = _tx.csma ? (rfc_op_t *)&_cmd_csma : (rfc_op_t *)&_cmd_tx;

    _csma_prepare(_cfg.csma_retries);
    _cmd_csma.next_op = &_cmd_tx;
    _cmd_csma.condition.rule = RFC_COND_STOP_ON_FALSE;

    _cmd_tx.status = RFC_IDLE;
    _cmd_tx.start_trigger = (rfc_trigger_t) { .type = RFC_TRIG_NOW };
    _cmd_tx.start_time = 0;
    _cmd_rx_ack.status = RFC_IDLE;
    if (_tx.ack_req) {
        _cmd_tx.next_op = &_cmd_rx_ack;
        _cmd_tx.condition.rule = RFC_COND_ALWAYS;
        _cmd_rx_ack.seq_no = _tx_buf[2];
    }
    else {
        _cmd_tx.next_op = NULL;
        _cmd_tx.condition.rule = RFC_COND_NEVER;
    }

    if (_ifs_valid) {
        if ((int32_t)(_ifs_end - RFC_RAT->RATCNT) > 0) {
            first->start_trigger.type = RFC_TRIG_ABSTIME;
            first->start_trigger.past_trig = 1;
            first->start_time = _ifs_end;
        }
        else {
            _ifs_valid = false;
        }
    }

    return _exec(first) ? 0 : -EIO;
}

/*
 * State of the foreground chain of a transmission:
 * -EAGAIN while it runs, 0 for a frame that was sent (and acknowledged, if
 * it asked for that), 1 for an ACK with the frame pending bit, -EBUSY for a
 * channel that stayed busy, -ETIMEDOUT for a missing ACK and -EIO for any
 * error.
 */
static int _fg_tx_state(void)
{
    if (_tx.csma) {
        uint16_t status = _cmd_csma.status;

        if (!_ended(status)) {
            return -EAGAIN;
        }
        if (status != RFC_IEEE_DONE_OK) {
            return (status == RFC_IEEE_DONE_BUSY) ? -EBUSY : -EIO;
        }
    }

    uint16_t status = _cmd_tx.status;

    if (!_ended(status)) {
        return -EAGAIN;
    }
    if (status != RFC_IEEE_DONE_OK) {
        return -EIO;
    }

    if (_tx.ack_req) {
        status = _cmd_rx_ack.status;
        if (!_ended(status)) {
            return -EAGAIN;
        }
        switch (status) {
        case RFC_IEEE_DONE_ACK:
            return 0;
        case RFC_IEEE_DONE_ACKPEND:
            return 1;
        case RFC_IEEE_DONE_TIMEOUT:
            return -ETIMEDOUT;
        default:
            return -EIO;
        }
    }

    return 0;
}

/* The foreground chain of a transmission has ended */
static void _tx_done(int res)
{
    if (_cmd_tx.status == RFC_IEEE_DONE_OK) {
        _ifs_start();
    }

    if ((res == -ETIMEDOUT) && (_tx.retrans < _cfg.retrans)) {
        /* no ACK: send the frame again, with CSMA-CA as the first time */
        _tx.retrans++;
        if (_fg_submit() == 0) {
            return;
        }
        res = -EIO;
    }

    _fg = FG_NONE;
    if (_tx.own_bg) {
        _rx_abort();
    }
    _tx_retrans = _tx.retrans;
    _tx_result = res;
    _hal->cb(_hal, IEEE802154_RADIO_CONFIRM_TX_DONE);
}

/* The CSMA-CA command of a single CCA has ended */
static void _cca_done(void)
{
    _fg = FG_NONE;
    if (_tx.own_bg) {
        _rx_abort();
    }
    _cca_result = (_cmd_csma.status == RFC_IEEE_DONE_OK) ? 1 : 0;
    _hal->cb(_hal, IEEE802154_RADIO_CONFIRM_CCA);
}

static uint8_t *_rx_element(rfc_data_entry_t *entry, unsigned *psdu_len)
{
    uint8_t *element = &((rfc_data_entry_general_t *)entry)->data;

    if ((element[0] < RX_APPENDED_LEN) || (element[0] > RX_ELEMENT_LEN - 1)) {
        return NULL;
    }
    *psdu_len = element[0] - RX_APPENDED_LEN;

    return element + 1;
}

/*
 * A frame was stored in the queue. If the RF Core is going to acknowledge
 * it, the frame is indicated after the ACK (TX_ACK interrupt): the upper
 * layer asks the radio to go idle when it gets the frame, and that must not
 * fall on the ACK. The conditions are those of [TRM] 25.5.4.1.3; the frame
 * has already passed the filter and the FCS check.
 */
static void _handle_rx(void)
{
    rfc_data_entry_t *entry = cc26x2_cc13x2_rfc_queue_recv(&_rx_queue);
    unsigned len;
    const uint8_t *psdu;

    if (entry == NULL) {
        return;
    }
    psdu = _rx_element(entry, &len);
    if (psdu == NULL) {
        entry->status = RFC_DATA_ENTRY_PENDING;
        return;
    }

    if ((_bg == BG_RX) && _cmd_rx.frame_filt_opt.auto_ack_en && (len >= 3)) {
        uint8_t type = psdu[0] & IEEE802154_FCF_TYPE_MASK;
        bool broadcast = ((psdu[1] & IEEE802154_FCF_DST_ADDR_MASK) ==
                          IEEE802154_FCF_DST_ADDR_SHORT) &&
                         (len >= 7) && (psdu[5] == 0xFF) && (psdu[6] == 0xFF);

        if ((psdu[0] & IEEE802154_FCF_ACK_REQ) && !broadcast &&
            ((type == IEEE802154_FCF_TYPE_DATA) ||
             (type == IEEE802154_FCF_TYPE_MACCMD))) {
            _rx_ind_pending = true;
            return;
        }
    }

    _hal->cb(_hal, IEEE802154_RADIO_INDICATION_RX_DONE);
}

/*
 * Handler of the RF_CPE0 interrupt ([TRM] 25.5.2):
 *
 * - RX_ENTRY_DONE: a frame was stored in the queue.
 * - TX_ACK: the automatic ACK of a frame was sent.
 * - LAST_FG_COMMAND_DONE: the foreground chain of a transmission or of a CCA
 *   ended or stopped.
 * - LAST_COMMAND_DONE: the receiver ended, which the driver only waits for
 *   after it asked it to stop during a frame.
 */
void cc26x2_cc13x2_rfc_isr(void)
{
    uint32_t flags = RFC_DBELL->RFCPEIFG & RFC_DBELL->RFCPEIEN;

    /* flags are cleared by writing 0 to their bit ([TRM] 25.2.3.1) */
    RFC_DBELL_NONBUF->RFCPEIFG = ~flags;

    if (flags & CPE_IRQ_RX_ENTRY_DONE) {
        if (_rx_ind_pending) {
            /* the ACK of the previous frame was not seen: do not lose it */
            _rx_ind_pending = false;
            _hal->cb(_hal, IEEE802154_RADIO_INDICATION_RX_DONE);
        }
        _handle_rx();
    }

    if ((flags & CPE_IRQ_TX_ACK) && _rx_ind_pending) {
        _rx_ind_pending = false;
        _hal->cb(_hal, IEEE802154_RADIO_INDICATION_RX_DONE);
    }

    if (flags & (CPE_IRQ_LAST_FG_COMMAND_DONE | CPE_IRQ_LAST_COMMAND_DONE)) {
        if (_fg == FG_TX) {
            int res = _fg_tx_state();

            if (res != -EAGAIN) {
                _tx_done(res);
            }
        }
        else if ((_fg == FG_CCA) && _ended(_cmd_csma.status)) {
            _cca_done();
        }

        if (_rx_stopping && _ended(_cmd_rx.status)) {
            /* the frame that kept the receiver on is over */
            _rx_stopping = false;
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            if (_bg == BG_RX) {
                _bg = BG_NONE;
                _rx_start(false);
            }
        }
    }
}

static int _write(ieee802154_dev_t *dev, const iolist_t *iolist)
{
    (void)dev;
    size_t len = 0;

    /* nothing to send until a frame was written in full */
    _cmd_tx.payload_len = 0;
    for (const iolist_t *iol = iolist; iol; iol = iol->iol_next) {
        if (len + iol->iol_len > sizeof(_tx_buf)) {
            return -EOVERFLOW;
        }
        if (iol->iol_len) {
            memcpy(&_tx_buf[len], iol->iol_base, iol->iol_len);
            len += iol->iol_len;
        }
    }
    _cmd_tx.payload_len = len;

    return 0;
}

static int _len(ieee802154_dev_t *dev)
{
    (void)dev;
    rfc_data_entry_t *entry = cc26x2_cc13x2_rfc_queue_recv(&_rx_queue);
    unsigned len;

    if ((entry == NULL) || (_rx_element(entry, &len) == NULL)) {
        return 0;
    }

    return len;
}

static int _read(ieee802154_dev_t *dev, void *buf, size_t max_size,
                 ieee802154_rx_info_t *info)
{
    (void)dev;
    rfc_data_entry_t *entry = cc26x2_cc13x2_rfc_queue_recv(&_rx_queue);
    unsigned len;
    const uint8_t *psdu;
    int res;

    if (entry == NULL) {
        return -ENOENT;
    }
    psdu = _rx_element(entry, &len);
    if (psdu == NULL) {
        res = -ENOENT;
    }
    else if (buf == NULL) {
        /* drop the frame */
        res = 0;
    }
    else if (max_size < len) {
        res = -ENOBUFS;
    }
    else {
        if (info != NULL) {
            int8_t rssi = (int8_t)psdu[len];

            info->rssi = ieee802154_dbm_to_rssi(rssi);
            info->lqi = cc26x2_cc13x2_rf_rssi_to_lqi(rssi);
            info->timestamp = 0;
        }
        memcpy(buf, psdu, len);
        res = len;
    }
    entry->status = RFC_DATA_ENTRY_PENDING;

    return res;
}

static int _off(ieee802154_dev_t *dev)
{
    (void)dev;

    if (_power == POWER_ON) {
        _rx_abort();
        _fg = FG_NONE;

        _cmd_fs_powerdown.status = RFC_IDLE;
        _cmd_sync_stop_rat.status = RFC_IDLE;
        if (_exec(&_cmd_fs_powerdown)) {
            /* both commands end at once: wait for the last one */
            for (unsigned i = 0; i < 100000U; i++) {
                if (_ended(_cmd_sync_stop_rat.status) ||
                    (_ended(_cmd_fs_powerdown.status) &&
                     (_cmd_fs_powerdown.status != RFC_DONE_OK))) {
                    break;
                }
            }
            if (_cmd_sync_stop_rat.status == RFC_DONE_OK) {
                _cmd_sync_start_rat.rat0 = _cmd_sync_stop_rat.rat0;
            }
        }
    }

    cc26x2_cc13x2_rfc_power_off();
    _ifs_valid = false;
    _power = POWER_OFF;

    return 0;
}

static int _request_on(ieee802154_dev_t *dev)
{
    (void)dev;

    cc26x2_cc13x2_rfc_request_on();
    _power = POWER_RFC_ON;

    return 0;
}

/* Power-on state machine, as in the sub-GHz driver */
static int _confirm_on(ieee802154_dev_t *dev)
{
    (void)dev;

    switch (_power) {
    case POWER_RFC_ON:
        if (cc26x2_cc13x2_rfc_confirm_on() != 0) {
            return -EAGAIN;
        }
        cc26x2_cc13x2_rfc_finish_on(rf_patch_cpe_ieee_802_15_4);

        _cmd_sync_start_rat.status = RFC_IDLE;
        _cmd_radio_setup.status = RFC_IDLE;
        _bg = BG_NONE;
        _fg = FG_NONE;
        if (!_exec(&_cmd_sync_start_rat)) {
            return -EIO;
        }
        _power = POWER_SETUP;
        return -EAGAIN;

    case POWER_SETUP:
        if (cc26x2_cc13x2_rfc_confirm_execute() != 0) {
            return -EAGAIN;
        }
        if ((_cmd_sync_start_rat.status != RFC_DONE_OK) ||
            (_cmd_radio_setup.status != RFC_DONE_OK)) {
            DEBUG("[cc26x2_cc13x2_rf]: setup chain failed, RAT=%04x setup=%04x\n",
                  _cmd_sync_start_rat.status, _cmd_radio_setup.status);
            return -EIO;
        }
        RFC_DBELL_NONBUF->RFCPEIFG = 0;
        RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_RX_ENTRY_DONE | CPE_IRQ_TX_ACK |
                                      CPE_IRQ_LAST_FG_COMMAND_DONE;
        _power = POWER_ON;
        return 0;

    case POWER_ON:
        return 0;

    default:
        return -EIO;
    }
}

static int _request_op(ieee802154_dev_t *dev, ieee802154_hal_op_t op, void *ctx)
{
    (void)dev;

    switch (op) {
    case IEEE802154_HAL_OP_TRANSMIT:
    case IEEE802154_HAL_OP_CCA:
        if (_fg != FG_NONE) {
            return -EBUSY;
        }
        if ((op == IEEE802154_HAL_OP_TRANSMIT) && (_cmd_tx.payload_len == 0)) {
            /* the frame did not fit, see _write() */
            return -EINVAL;
        }
        _tx.own_bg = (_bg == BG_NONE);
        if (_tx.own_bg && (_rx_start(true) != 0)) {
            return -EIO;
        }
        if (op == IEEE802154_HAL_OP_CCA) {
            /* the CSMA-CA command alone, with a single CCA */
            _cca_result = -EAGAIN;
            _csma_prepare(0);
            _cmd_csma.next_op = NULL;
            _cmd_csma.condition.rule = RFC_COND_NEVER;
            _fg = FG_CCA;
            if (_exec(&_cmd_csma)) {
                break;
            }
        }
        else {
            _tx_result = -EAGAIN;
            _tx.csma = (_cfg.csma_retries >= 0);
            _tx.ack_req = (_tx_buf[0] & IEEE802154_FCF_ACK_REQ) &&
                          (_cmd_tx.payload_len >= 3);
            _tx.retrans = 0;
            _fg = FG_TX;
            if (_fg_submit() == 0) {
                break;
            }
        }
        /* the foreground chain was rejected */
        _fg = FG_NONE;
        if (_tx.own_bg) {
            _rx_abort();
        }
        return -EIO;

    case IEEE802154_HAL_OP_SET_RX:
        if (_fg != FG_NONE) {
            return -EBUSY;
        }
        if (_bg == BG_RX) {
            break;
        }
        _rx_abort();
        return _rx_start(false);

    case IEEE802154_HAL_OP_SET_IDLE: {
        assert(ctx);
        bool force = *((bool *)ctx);

        if (force) {
            _rx_abort();
            _fg = FG_NONE;
            break;
        }
        if (_fg != FG_NONE) {
            return -EBUSY;
        }
        if (_bg == BG_RX) {
            /* CMD_STOP leaves a frame that is being received alone, and the
             * receiver then ends after it ([TRM] 25.5.4.1.4) */
            unsigned key = irq_disable();
            int res = cc26x2_cc13x2_rfc_stop_cmd(&_cmd_rx.status);

            if (res != 0) {
                _rx_stopping = true;
                RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_LAST_COMMAND_DONE;
                irq_restore(key);
                return -EBUSY;
            }
            _bg = BG_NONE;
            _rx_stopping = false;
            irq_restore(key);
        }
        break;
    }

    default:
        assert(false);
        return -ENOTSUP;
    }

    return 0;
}

static int _confirm_op(ieee802154_dev_t *dev, ieee802154_hal_op_t op, void *ctx)
{
    (void)dev;
    ieee802154_tx_info_t *info = ctx;
    int res;

    switch (op) {
    case IEEE802154_HAL_OP_TRANSMIT:
        res = _tx_result;
        if (res == -EAGAIN) {
            return -EAGAIN;
        }
        _tx_result = -EAGAIN;
        if (res == -EIO) {
            return -EIO;
        }
        if (info != NULL) {
            info->retrans = _tx_retrans;
            switch (res) {
            case 0:
                info->status = TX_STATUS_SUCCESS;
                break;
            case 1:
                info->status = TX_STATUS_FRAME_PENDING;
                break;
            case -EBUSY:
                info->status = TX_STATUS_MEDIUM_BUSY;
                break;
            default:
                info->status = TX_STATUS_NO_ACK;
                break;
            }
        }
        return 0;

    case IEEE802154_HAL_OP_SET_RX:
        return (_bg == BG_RX) ? 0 : -EAGAIN;

    case IEEE802154_HAL_OP_SET_IDLE:
        return ((_bg == BG_NONE) && (_fg == FG_NONE)) ? 0 : -EAGAIN;

    case IEEE802154_HAL_OP_CCA:
        res = _cca_result;
        if (res == -EAGAIN) {
            return -EAGAIN;
        }
        assert(ctx != NULL);
        *((bool *)ctx) = (res == 1);
        _cca_result = -EAGAIN;
        return 0;

    default:
        assert(false);
        return -ENOTSUP;
    }
}

/* The receiver takes its settings when it starts: start it again if it runs
 * for the upper layer */
static void _rx_restart(void)
{
    unsigned key = irq_disable();

    if ((_bg == BG_RX) && (_fg == FG_NONE)) {
        _rx_abort();
        _rx_start(false);
    }

    irq_restore(key);
}

static int _set_cca_threshold(ieee802154_dev_t *dev, int8_t threshold)
{
    (void)dev;

    _cmd_rx.cca_rssi_thr = threshold;
    _rx_restart();

    return 0;
}

/* CCA modes 1, 2 and 3 of the standard map to the energy and correlator
 * sources of the RF Core ([TRM] 25.5.4.1.5) */
static int _set_cca_mode(ieee802154_dev_t *dev, ieee802154_cca_mode_t mode)
{
    (void)dev;

    switch (mode) {
    case IEEE802154_CCA_MODE_ED_THRESHOLD:
        _cmd_rx.cca_opt.cca_en_energy = 1;
        _cmd_rx.cca_opt.cca_en_corr = 0;
        break;
    case IEEE802154_CCA_MODE_CARRIER_SENSING:
        _cmd_rx.cca_opt.cca_en_energy = 0;
        _cmd_rx.cca_opt.cca_en_corr = 1;
        break;
    case IEEE802154_CCA_MODE_ED_THRESH_AND_CS:
        _cmd_rx.cca_opt.cca_en_energy = 1;
        _cmd_rx.cca_opt.cca_en_corr = 1;
        _cmd_rx.cca_opt.cca_corr_op = 1;
        break;
    case IEEE802154_CCA_MODE_ED_THRESH_OR_CS:
        _cmd_rx.cca_opt.cca_en_energy = 1;
        _cmd_rx.cca_opt.cca_en_corr = 1;
        _cmd_rx.cca_opt.cca_corr_op = 0;
        break;
    default:
        return -ENOTSUP;
    }
    _rx_restart();

    return 0;
}

static int _set_tx_power(int16_t pow)
{
    const cc26x2_cc13x2_rf_pa_t *match = &cc26x2_cc13x2_rf_patable_24ghz[0];

    for (unsigned i = 0; i < CC26X2_CC13X2_PA_TABLE_24GHZ_NUMOF; i++) {
        if (cc26x2_cc13x2_rf_patable_24ghz[i].dbm >= pow) {
            match = &cc26x2_cc13x2_rf_patable_24ghz[i];
        }
        else {
            break;
        }
    }

    _cmd_radio_setup.tx_power = match->val;
    _cmd_set_tx_power.tx_power = match->val;
    if (_power == POWER_ON) {
        return _exec(&_cmd_set_tx_power) ? 0 : -EIO;
    }

    return 0;
}

static int _config_phy(ieee802154_dev_t *dev, const ieee802154_phy_conf_t *conf)
{
    (void)dev;

    if ((conf->phy_mode != IEEE802154_PHY_OQPSK) &&
        (conf->phy_mode != IEEE802154_PHY_NO_OP)) {
        return -EINVAL;
    }
    if ((conf->channel < IEEE802154_CHANNEL_MIN) ||
        (conf->channel > IEEE802154_CHANNEL_MAX)) {
        return -EINVAL;
    }

    _cfg.channel = conf->channel;
    _rx_restart();

    return _set_tx_power(conf->pow);
}

static int _set_frame_retrans(ieee802154_dev_t *dev, uint8_t retrans)
{
    (void)dev;

    _cfg.retrans = retrans;

    return 0;
}

static int _set_csma_params(ieee802154_dev_t *dev, const ieee802154_csma_be_t *bd,
                            int8_t retries)
{
    (void)dev;

    if (bd != NULL) {
        _cfg.csma_be = *bd;
    }
    _cfg.csma_retries = retries;

    return 0;
}

static int _set_frame_filter_mode(ieee802154_dev_t *dev, ieee802154_filter_mode_t mode)
{
    (void)dev;

    /* the radio waits for the ACKs itself and does not pass them on */
    if (mode == IEEE802154_FILTER_ACK_ONLY) {
        return -ENOTSUP;
    }

    _cfg.filter = mode;
    _rx_restart();

    return 0;
}

static int _get_frame_filter_mode(ieee802154_dev_t *dev, ieee802154_filter_mode_t *mode)
{
    (void)dev;

    *mode = _cfg.filter;

    return 0;
}

static int _config_addr_filter(ieee802154_dev_t *dev, ieee802154_af_cmd_t cmd,
                               const void *value)
{
    (void)dev;

    switch (cmd) {
    case IEEE802154_AF_SHORT_ADDR:
        _cmd_rx.local_short_addr = byteorder_ntohs(*(const network_uint16_t *)value);
        break;
    case IEEE802154_AF_EXT_ADDR:
        _cmd_rx.local_ext_addr = byteorder_ntohll(((const eui64_t *)value)->uint64);
        break;
    case IEEE802154_AF_PANID:
        _cmd_rx.local_pan_id = *(const uint16_t *)value;
        break;
    case IEEE802154_AF_PAN_COORD:
        _cfg.pan_coord = *(const bool *)value;
        break;
    }
    _rx_restart();

    return 0;
}

static int _config_src_addr_match(ieee802154_dev_t *dev, ieee802154_src_match_t cmd,
                                  const void *value)
{
    (void)dev;

    switch (cmd) {
    case IEEE802154_SRC_MATCH_EN:
        _cfg.pending = *(const bool *)value;
        break;
    default:
        return -ENOTSUP;
    }
    _rx_restart();

    return 0;
}

static const ieee802154_radio_ops_t _ops = {
    .caps = IEEE802154_CAP_24_GHZ
          | IEEE802154_CAP_FRAME_RETRANS
          | IEEE802154_CAP_FRAME_RETRANS_INFO
          | IEEE802154_CAP_AUTO_CSMA
          | IEEE802154_CAP_AUTO_ACK
          | IEEE802154_CAP_IRQ_TX_DONE
          | IEEE802154_CAP_IRQ_CCA_DONE
          | IEEE802154_CAP_PHY_OQPSK,

    .write = _write,
    .read = _read,
    .len = _len,
    .off = _off,
    .request_on = _request_on,
    .confirm_on = _confirm_on,
    .request_op = _request_op,
    .confirm_op = _confirm_op,
    .set_cca_threshold = _set_cca_threshold,
    .set_cca_mode = _set_cca_mode,
    .config_phy = _config_phy,
    .set_frame_retrans = _set_frame_retrans,
    .set_csma_params = _set_csma_params,
    .set_frame_filter_mode = _set_frame_filter_mode,
    .get_frame_filter_mode = _get_frame_filter_mode,
    .config_addr_filter = _config_addr_filter,
    .config_src_addr_match = _config_src_addr_match,
};

void cc26x2_cc13x2_rf_init(void)
{
    /* The receive buffers form a ring ([TRM] 25.4): one frame each, with a
     * length byte before it */
    for (unsigned i = 0; i < CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF; i++) {
        rfc_data_entry_t *entry = &_rx_buf[i].entry;

        entry->status = RFC_DATA_ENTRY_PENDING;
        entry->config.type = RFC_DATA_ENTRY_TYPE_GEN;
        entry->config.lensz = 1;
        entry->length = RX_BUF_LEN - sizeof(rfc_data_entry_t);
        entry->next_entry =
            &_rx_buf[(i + 1) % CONFIG_CC26X2_CC13X2_RF_RX_BUF_NUMOF].entry;
    }
    _rx_queue.curr_entry = &_rx_buf[0].entry;
    _rx_queue.last_entry = NULL;

    _cmd_rx.cca_opt.cca_en_energy = 1;
    _cmd_radio_setup.tx_power = cc26x2_cc13x2_rf_patable_24ghz[0].val;
    _cmd_set_tx_power.tx_power = cc26x2_cc13x2_rf_patable_24ghz[0].val;

    CC26X2_CC13X2_RF_FRONTEND_SETUP_24GHZ();
}

void cc26x2_cc13x2_rf_hal_setup(ieee802154_dev_t *hal)
{
    hal->driver = &_ops;
    _hal = hal;
}
