/*
 * SPDX-FileCopyrightText: 2020 Locha Inc
 * SPDX-FileCopyrightText: 2022 Francisco Acosta
 * SPDX-FileCopyrightText: 2026 luisan00
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     cpu_cc26x2_cc13x2_rf
 * @{
 *
 * @file
 * @brief       CC13x2 sub-GHz IEEE 802.15.4g radio: operations of the radio HAL
 *
 * @author      Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author      Francisco Acosta <fco.ja.ac@gmail.com>
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include <errno.h>
#include <string.h>

#include "irq.h"
#include "net/ieee802154/radio.h"
#include "periph/timer.h"
#include "random.h"
#include "ztimer.h"

#include "cc26x2_cc13x2_rf.h"
#include "cc26x2_cc13x2_rf_internal.h"
#include "cc26x2_cc13x2_rfc.h"

#define ENABLE_DEBUG 0
#include "debug.h"

/** The FCS is calculated by the radio */
#define ACK_PSDU_LEN        (IEEE802154_ACK_FRAME_LEN - IEEE802154_FCS_LEN)

#define MAC_TIMER_CHAN_ACK  (0U) /**< MAC timer channel of the delay before an
                                      ACK frame */
#define MAC_TIMER_CHAN_IFS  (1U) /**< MAC timer channel of the interframe
                                      spacing */

/* The MAC timer counts microseconds */
#define TIMER_FREQ          (1000000UL)

/**
 * @brief   Time without transmitting after a frame was sent, in microseconds
 *
 * IEEE Std 802.15.4-2024, 6.3.1: two frames of a device are separated by at
 * least max(macSifsPeriod, aTurnaroundTime) after a short frame and
 * max(macLifsPeriod, aTurnaroundTime) after a long one. For the SUN FSK PHY
 * in the 470 to 928 MHz bands the MAC symbol period is 20 us (Table 20-1),
 * so macSifsPeriod (12 symbols) is 240 us and macLifsPeriod (40 symbols) is
 * 800 us (11.1.4), and aTurnaroundTime is 1 ms (Table 12-1): both spaces are
 * 1 ms. The timer starts when the RF Core reports the end of the
 * transmission, so the space on the air is longer by the start-up time of
 * the next one.
 */
#define IFS_US              (1000U)

/**
 * @brief   Backoff period of CSMA-CA, in microseconds
 *
 * IEEE Std 802.15.4-2024, 6.3.2.1 and Table 8-36: macUnitBackoffPeriod is
 * aTurnaroundTime plus phyCcaDuration, 1 ms and 8 symbol periods of 20 us
 * for the SUN FSK PHY in the bands of this driver. It is the period the
 * SubMAC uses when it does the backoffs.
 */
#define CSMA_BACKOFF_PERIOD_US  (IEEE802154G_ATURNAROUNDTIME_US + \
                                 IEEE802154_CCA_DURATION_IN_SYMBOLS * \
                                 IEEE802154_MR_FSK_SYMBOL_TIME_US)

/** Highest backoff exponent that is accepted, the maximum of macMaxBe
 *  (IEEE Std 802.15.4-2024, Table 8-36) */
#define CSMA_BE_MAX         (8U)

#if IS_USED(MODULE_IEEE802154_SUBMAC)
/* The SubMAC starts the radio on this channel. Some bands do not have the
 * default one (they do not start at channel 0), and then the SubMAC could not
 * configure the PHY at all. */
#  if (CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL < CC26X2_CC13X2_RF_CHAN_MIN) || \
      (CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL > CC26X2_CC13X2_RF_CHAN_MAX)
#    error "CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL is not a channel of the band"
#  endif
#  ifdef CC26X2_CC13X2_RF_CHAN_GAP_MIN
#    if (CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL >= CC26X2_CC13X2_RF_CHAN_GAP_MIN) && \
        (CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL <= CC26X2_CC13X2_RF_CHAN_GAP_MAX)
#      error "CONFIG_IEEE802154_DEFAULT_SUBGHZ_CHANNEL is not a channel of the band"
#    endif
#  endif
#endif

static const ieee802154_radio_ops_t cc26x2_cc13x2_rf_ops;
static ieee802154_dev_t *cc26x2_cc13x2_rf_hal;

enum {
    STATE_IDLE,
    STATE_TX,
    STATE_RX,
};

static volatile uint8_t _state;
static bool _rx_enabled;

static struct {
    bool promisc  : 1; /**< whether the device is in promiscuous mode */
    bool auto_ack : 1; /**< whether the radio sends ACKs automatically */
    bool ifs      : 1; /**< whether the interframe spacing after a frame of
                            ours is running */
} _cfg = {
    .promisc  = false,
    .auto_ack = true,
    .ifs      = false,
};

/**
 * CSMA-CA parameters, see _set_csma_params(): a negative number of retries
 * sends a frame at once, 0 after one CCA, and a positive number runs the
 * unslotted CSMA-CA algorithm, where it is macMaxCsmaBackoffs.
 */
static struct {
    int8_t retries;
    uint8_t min_be;
    uint8_t max_be;
} _csma_conf = {
    .retries = 0,
    .min_be = CONFIG_IEEE802154_DEFAULT_CSMA_CA_MIN_BE,
    .max_be = CONFIG_IEEE802154_DEFAULT_CSMA_CA_MAX_BE,
};

/** A transmission with CSMA-CA is under way and its frame is not on the air
 *  yet: a backoff runs or is over, see _csma_backoff() */
static volatile bool _csma_active;

/** The backoff is over, but the CCA has to wait for an ACK that is being
 *  sent or for the IFS */
static volatile bool _csma_due;

static uint8_t _csma_nb;    /**< NB of the algorithm: backoffs so far */
static uint8_t _csma_be;    /**< BE of the algorithm: backoff exponent */

static void _csma_timer_cb(void *arg);
static ztimer_t _csma_timer = { .callback = _csma_timer_cb };

static uint8_t _ack[ACK_PSDU_LEN];

enum {
    CCA_RESULT_NONE,    /**< no result yet */
    CCA_RESULT_IDLE,    /**< the channel is clear */
    CCA_RESULT_BUSY,    /**< the channel is busy */
} _cca_result;

/** CMD_PROP_CS was accepted by the RF Core and its end is awaited */
static volatile bool _cca_pending;

enum {
    TX_STATE_IDLE,         /**< Nothing is being transmitted */
    TX_STATE_TX,           /**< A frame of the upper layer is on its way:
                                its CCA, the frame or the wait for its ACK */
    TX_STATE_ACK_WAIT,     /**< A frame was received and its ACK is due: the
                                timer of the AIFS is running */
    TX_STATE_TX_ACK,       /**< The ACK of a received frame is being sent */
};

static volatile uint8_t _tx_state;

/** The transmission in progress starts with the carrier sense command of
 *  the RF Core, see _tx_submit() */
static bool _tx_cca;

/** The frame of the transmission in progress is followed by the wait for its
 *  ACK, see rf_cmd_prop_rx_ack */
static bool _tx_wait_ack;

/** The frame given to _write() requests an ACK */
static bool _tx_ack_req;

/** Sequence number of the frame given to _write() */
static uint8_t _tx_seq;

/** Frame Pending field of the ACK of the last transmission */
static bool _tx_frame_pending;

/** The result of a transmission is known but not reported yet: a frame came
 *  instead of its ACK and is acknowledged first, see _tx_done() */
static volatile bool _tx_report;

/**
 * RX was asked to stop while a frame was coming in. It ends by itself after
 * the frame and is started again from the interrupt of the end of the
 * command, see _rx_stop_unless_receiving().
 */
static volatile bool _rx_stopping;

static void _rx_abort(void);
static int _rx_stop_unless_receiving(void);
static bool _ack_pending(void);
static void _csma_attempt(ieee802154_dev_t *dev);
static void _csma_busy(ieee802154_dev_t *dev);

static volatile int _tx_result = -EAGAIN;

static inline uint16_t _entry_len(const rfc_data_entry_general_t *entry)
{
    const uint8_t *data = &entry->data;

    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline bool _is_ack_frame(const uint8_t *psdu)
{
    return (psdu[0] & IEEE802154_FCF_TYPE_MASK) == IEEE802154_FCF_TYPE_ACK;
}

/* Whether an ACK frame is the one of the frame given to _write() */
static bool _is_tx_ack(const rfc_data_entry_general_t *entry)
{
    uint8_t psdu[ACK_PSDU_LEN];

    if (_entry_len(entry) != ACK_PSDU_LEN + CC26X2_CC13X2_RF_RX_STATUS_LEN) {
        return false;
    }
    cc26x2_cc13x2_rf_copy_psdu(psdu, (&entry->data) + sizeof(uint16_t), sizeof(psdu));
    if (!_is_ack_frame(psdu) || (psdu[2] != _tx_seq)) {
        return false;
    }
    _tx_frame_pending = psdu[0] & IEEE802154_FCF_FRAME_PEND;

    return true;
}

/*
 * Takes or drops a frame the RF Core has received.
 *
 * A frame that is taken is kept until the upper layer reads it. It is
 * indicated at once if the upper layer has the radio in RX, after its ACK if
 * it requests one, and otherwise when the radio is set to RX again: a frame
 * can come in while the ACK of a frame of ours is awaited, when the upper
 * layer is in the middle of that transmission and cannot take it.
 */
static void _rx_entry(ieee802154_dev_t *dev, rfc_data_entry_general_t *entry)
{
    /* Reject malformed entries before accessing the MHR. */
    uint16_t pktlen = _entry_len(entry);
    if (pktlen < CC26X2_CC13X2_RF_RX_STATUS_LEN + IEEE802154_MIN_FRAME_LEN ||
        pktlen > IEEE802154G_FRAME_LEN_MAX + CC26X2_CC13X2_RF_RX_STATUS_LEN) {
        entry->status = RFC_DATA_ENTRY_PENDING;
        return;
    }

    /* The PSDU is stored with the bit order of the RF Core and is left as
     * it is: _read() reverses it when the frame is fetched. The filter only
     * needs the MAC header, taken from a bit-reversed copy. A frame shorter
     * than a full header leaves the rest of the copy zeroed. */
    uint8_t psdu[IEEE802154_MAX_HDR_LEN] = { 0 };
    size_t mhr_len = pktlen - CC26X2_CC13X2_RF_RX_STATUS_LEN;

    if (mhr_len > sizeof(psdu)) {
        mhr_len = sizeof(psdu);
    }
    cc26x2_cc13x2_rf_copy_psdu(psdu, (&entry->data) + sizeof(uint16_t), mhr_len);

    bool send_ack = false;

    if (!_cfg.promisc) {
        /* An ACK frame is not for the upper layer: the one of a frame of
         * ours is taken by the transmission, see _tx_done(). Anything else
         * goes through the address filter. */
        if (_is_ack_frame(psdu) || !cc26x2_cc13x2_rf_l2_filter(psdu)) {
            entry->status = RFC_DATA_ENTRY_PENDING;
            return;
        }
        send_ack = (psdu[0] & IEEE802154_FCF_ACK_REQ) && _cfg.auto_ack;
    }

    /* Without room to keep it, or with the ACK of another frame still due,
     * the frame is dropped without ACK: its sender sends it again. */
    if ((send_ack && _ack_pending()) || !cc26x2_cc13x2_rf_rx_keep(entry)) {
        entry->status = RFC_DATA_ENTRY_PENDING;
        return;
    }

    if (send_ack) {
        /* the ACK carries the sequence number of the frame */
        _ack[2] = psdu[2];

        /* RX has to end before the ACK can be sent */
        _rx_abort();

        /* The ACK goes out AIFS after the frame. Until then every request
         * that needs the radio is refused, see _ack_pending(). The frame is
         * indicated after its ACK. */
        _tx_state = TX_STATE_ACK_WAIT;
        timer_set(CC26X2_CC13X2_RF_TIMER, MAC_TIMER_CHAN_ACK,
                  cc26x2_cc13x2_rf_ack_delay_us());
        timer_start(CC26X2_CC13X2_RF_TIMER);
    }
    else if (_rx_enabled) {
        dev->cb(dev, IEEE802154_RADIO_INDICATION_RX_DONE);
    }
}

static void _handle_rx(ieee802154_dev_t *dev)
{
    rfc_data_entry_general_t *entry;

    while ((entry = cc26x2_cc13x2_rf_rx_next()) != NULL) {
        _rx_entry(dev, entry);
    }
}

static void _set_ifs_timer(void)
{
    _cfg.ifs = true;

    timer_set(CC26X2_CC13X2_RF_TIMER, MAC_TIMER_CHAN_IFS, IFS_US);
    timer_start(CC26X2_CC13X2_RF_TIMER);
}

/* Whether CMD_PROP_TX_ADV was submitted and its end is awaited */
static bool _tx_running(void)
{
    return (_tx_state == TX_STATE_TX) || (_tx_state == TX_STATE_TX_ACK);
}

/*
 * Whether a received frame still has to be acknowledged. The ACK belongs to
 * the reception of the frame: from the end of the frame until the ACK is on
 * the air the radio is as busy as while the frame was coming in, although
 * RX is off, and nothing else may be started. The frame is indicated after
 * its ACK.
 */
static bool _ack_pending(void)
{
    return (_tx_state == TX_STATE_ACK_WAIT) || (_tx_state == TX_STATE_TX_ACK);
}

/* Whether the end of a transmission or of a CCA is awaited */
static bool _op_pending(void)
{
    return _tx_running() || _cca_pending;
}

/* Restart RX if it is not running and it is wanted: the upper layer asked
 * for it, or a transmission is in the backoff of CSMA-CA, during which the
 * radio listens. Only one radio operation command runs at a time: not while
 * a transmission, an ACK or a CCA is under way, RX is restored when they
 * end. */
static void _restore_rx(void)
{
    if ((_rx_enabled || _csma_active) && (_tx_state == TX_STATE_IDLE) &&
        !_cca_pending && (cc26x2_cc13x2_rf_rx_start() == 0) && _rx_enabled) {
        _state = STATE_RX;
    }
}

/* Stops RX at once. A frame that is being received is lost. */
static void _rx_abort(void)
{
    unsigned key = irq_disable();

    cc26x2_cc13x2_rf_rx_stop();
    if (_rx_stopping) {
        _rx_stopping = false;
        if (!_op_pending()) {
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
        }
    }

    irq_restore(key);
}

/*
 * Gives up the ACK of a received frame that is still waiting for its AIFS,
 * for a forced transition to idle. The frame, the last one that was kept,
 * is dropped: its sender gets no ACK and sends it again. An ACK that is
 * already on the air goes on, and its frame is indicated later.
 */
static void _ack_cancel(void)
{
    if (_tx_state == TX_STATE_ACK_WAIT) {
        timer_clear(CC26X2_CC13X2_RF_TIMER, MAC_TIMER_CHAN_ACK);
        if (!_cfg.ifs) {
            timer_stop(CC26X2_CC13X2_RF_TIMER);
        }
        _tx_state = TX_STATE_IDLE;
        _tx_report = false;
        cc26x2_cc13x2_rf_rx_release_last();
    }
}

/*
 * The ACK of a received frame is out, or could not be sent: the frame is
 * indicated either way, its sender retransmits if it misses the ACK. If the
 * frame came while the ACK of a frame of ours was awaited, the result of
 * that transmission is reported now, and the frame stays kept until the
 * upper layer sets the radio to RX.
 */
static void _ack_done(ieee802154_dev_t *dev, bool sent)
{
    _tx_state = TX_STATE_IDLE;
    _restore_rx();
    if (sent) {
        _set_ifs_timer();
    }
    if (_tx_report) {
        _tx_report = false;
        dev->cb(dev, IEEE802154_RADIO_CONFIRM_TX_DONE);
    }
    if (_rx_enabled) {
        dev->cb(dev, IEEE802154_RADIO_INDICATION_RX_DONE);
    }
    /* a backoff that ended meanwhile; with the IFS it goes on when that
     * is over */
    if (_csma_due && !_cfg.ifs) {
        _csma_attempt(dev);
    }
}

/*
 * Submits the transmission of the frame of the upper layer, after a CCA if
 * `cca` is set.
 *
 * The CCA is the one of the receiver while it runs, see
 * cc26x2_cc13x2_rf_rx_cca(): on a busy channel RX is left alone, and so it
 * is while a frame is being received. This is how the RF Core does CSMA-CA
 * in its IEEE 802.15.4 mode. Only if the receiver is off the RF Core senses
 * the carrier with a command of its own before the frame.
 *
 * RX must have ended before anything is submitted, only one radio operation
 * command runs at a time.
 *
 * Returns 0 if the RF Core took the commands, 1 if the channel is busy or a
 * frame has just been received, and nothing was started, and a negative
 * errno on error.
 */
static int _tx_submit(bool cca)
{
    int res;

    _tx_cca = false;
    if (!cca) {
        _rx_abort();
    }
    else {
        res = cc26x2_cc13x2_rf_rx_cca();
        if ((res > 0) || (_rx_stop_unless_receiving() != 0)) {
            return 1;
        }
        if (cc26x2_cc13x2_rf_rx_next() != NULL) {
            /* A frame has just come in and its interrupt is still to be
             * served: it may need its ACK now. A transmission would hold
             * that ACK back until the sender has given up and sent the
             * frame again. */
            _restore_rx();
            return 1;
        }
        /* without a running receiver: carrier sense before the frame */
        _tx_cca = (res < 0);
    }

    _tx_state = TX_STATE_TX;
    RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;
    RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_LAST_COMMAND_DONE;

    if ((res = cc26x2_cc13x2_rf_request_transmit(_tx_cca, _tx_wait_ack)) != 0) {
        DEBUG("[cc26x2_cc13x2_rf]: failed to transmit\n");
        RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
        _tx_state = TX_STATE_IDLE;
        _restore_rx();
    }

    return res;
}

/*
 * Unslotted CSMA-CA, IEEE Std 802.15.4-2024, 6.3.2.1 and Figure 6-2: wait a
 * random number of backoff periods from 0 to 2^BE - 1, then do a CCA. On a
 * clear channel the frame is sent. On a busy one NB and BE grow by one, BE
 * up to its maximum, and the next backoff starts, until NB is above the
 * number of retries: the transmission then ends with a busy medium.
 *
 * The radio listens during the backoff. The standard leaves that open, and
 * with the radio off two nodes that have frames for each other would both
 * send while the other one cannot hear. A frame that comes in is taken as
 * any other, see _rx_entry(): the upper layer gets it after this
 * transmission.
 */
static void _csma_backoff(void)
{
    uint32_t periods = random_uint32_range(0, 1UL << _csma_be);

    _csma_due = false;
    _restore_rx();
    ztimer_set(ZTIMER_USEC, &_csma_timer, periods * CSMA_BACKOFF_PERIOD_US);
}

/* The CCA found a busy channel, or a frame was coming in when it was due */
static void _csma_busy(ieee802154_dev_t *dev)
{
    if (++_csma_nb > _csma_conf.retries) {
        _csma_active = false;
        _tx_result = -EBUSY;
        dev->cb(dev, IEEE802154_RADIO_CONFIRM_TX_DONE);
        return;
    }
    if (_csma_be < _csma_conf.max_be) {
        _csma_be++;
    }
    _csma_backoff();
}

/*
 * The backoff is over: CCA and, on a clear channel, the frame, see
 * cc26x2_cc13x2_rf_request_transmit(). While the ACK of a received frame is
 * due or on the air, and during the IFS after it, this waits: _ack_done()
 * and the IFS timer call it again.
 */
static void _csma_attempt(ieee802154_dev_t *dev)
{
    if ((_tx_state != TX_STATE_IDLE) || _cfg.ifs) {
        _csma_due = true;
        return;
    }
    _csma_due = false;

    int res = _tx_submit(true);

    if (res > 0) {
        _csma_busy(dev);
    }
    else if (res < 0) {
        _csma_active = false;
        _tx_result = (res == -EAGAIN) ? -EIO : res;
        dev->cb(dev, IEEE802154_RADIO_CONFIRM_TX_DONE);
    }
}

static void _csma_timer_cb(void *arg)
{
    (void)arg;

    if (_csma_active) {
        _csma_attempt(cc26x2_cc13x2_rf_hal);
    }
}

/* Gives up a transmission that is still in its backoff */
static void _csma_cancel(void)
{
    ztimer_remove(ZTIMER_USEC, &_csma_timer);
    _csma_active = false;
    _csma_due = false;
}

/*
 * Stops RX unless a frame is being received, for the operations that must
 * not disturb a reception: IEEE Std 802.15.4-2024, 11.2.8, "if a request to
 * perform CCA is received by the PHY during reception of a PPDU, CCA shall
 * report a busy medium".
 *
 * With -EBUSY the RF Core keeps receiving and ends the receive command after
 * the frame, see cc26x2_cc13x2_rf_rx_stop_unless_receiving(). The interrupt
 * of the end of the command is enabled to start RX again: a frame that is
 * dropped by the RF Core (CRC error, illegal length) has no other one.
 */
static int _rx_stop_unless_receiving(void)
{
    unsigned key = irq_disable();
    int res = cc26x2_cc13x2_rf_rx_stop_unless_receiving();

    if (res == -EBUSY) {
        /* the flag is not cleared: if the command has just ended the
         * interrupt has to come */
        _rx_stopping = true;
        RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_LAST_COMMAND_DONE;
    }
    else if (_rx_stopping) {
        /* a previous request has taken effect meanwhile */
        _rx_stopping = false;
        if (!_op_pending()) {
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
        }
    }

    irq_restore(key);

    return res;
}

/*
 * CMD_PROP_TX_ADV has ended: PROP_DONE_OK (0x3400) if the frame was sent,
 * any other status is an error (SWCU185G, Table 25-184). If the frame
 * requests an ACK the receive command chained to it has ended too, and what
 * it received is looked at here:
 *
 * - The ACK of the frame: the transmission succeeded.
 * - Nothing, or another ACK: -ETIMEDOUT, the ACK did not come.
 * - Another frame: the ACK did not come either, the other node was sending
 *   instead of listening. Its frame is taken as any other. If both nodes
 *   dropped what they receive while they wait for an ACK, neither would
 *   ever get a frame through as long as both have something to send.
 */
static void _tx_done(ieee802154_dev_t *dev)
{
    int res = cc26x2_cc13x2_rf_tx_chain_state(_tx_cca, _tx_wait_ack);
    bool sent = (res == 0);
    uint8_t tx_state = _tx_state;

    _tx_state = TX_STATE_IDLE;

    if (tx_state == TX_STATE_TX_ACK) {
        _ack_done(dev, true);
        return;
    }

    if (_csma_active) {
        if (res == -EBUSY) {
            _csma_busy(dev);
            return;
        }
        _csma_active = false;
    }

    if (sent && _tx_wait_ack) {
        rfc_data_entry_general_t *entry;

        res = -ETIMEDOUT;
        while ((entry = cc26x2_cc13x2_rf_rx_next()) != NULL) {
            if (_is_tx_ack(entry)) {
                res = 0;
                entry->status = RFC_DATA_ENTRY_PENDING;
            }
            else {
                _rx_entry(dev, entry);
            }
        }
    }

    /* -EBUSY: the channel was busy and nothing was sent. The radio stays
     * idle, as the HAL asks, and the upper layer decides what comes next. */
    _tx_result = res;
    if (_tx_state == TX_STATE_ACK_WAIT) {
        /* a frame that came instead of the ACK is acknowledged first, the
         * upper layer would start the next attempt on top of that */
        _tx_report = true;
        return;
    }
    if (sent) {
        _set_ifs_timer();
    }
    dev->cb(dev, IEEE802154_RADIO_CONFIRM_TX_DONE);
}

/* CMD_PROP_CS has ended: PROP_DONE_IDLE (0x3407) for a clear channel,
 * PROP_DONE_BUSY (0x3408) for a busy one (SWCU185G, Table 25-188). An error
 * status is reported as a busy channel. */
static void _cca_done(ieee802154_dev_t *dev)
{
    _cca_result = (rf_cmd_prop_cs.status == RFC_PROP_DONE_IDLE) ? CCA_RESULT_IDLE
                                                                : CCA_RESULT_BUSY;
    _cca_pending = false;
    rf_cmd_prop_cs.status = RFC_IDLE;

    dev->cb(dev, IEEE802154_RADIO_CONFIRM_CCA);

    _restore_rx();
}

/*
 * Handler of the RF_CPE0 interrupt. It uses two of the CPE interrupts of the
 * proprietary mode (SWCU185G, section 25.10.3 and Table 25-181):
 *
 * - RX_ENTRY_DONE (bit 23): an entry of the RX queue changed its state to
 *   Finished. For a general data entry this happens after a frame is fully
 *   received, unless the frame is automatically flushed (section 25.10.4.1).
 *   It is enabled by _confirm_on() and stays enabled.
 * - LAST_COMMAND_DONE (bit 1): a radio operation command, or the last one of
 *   a chain, has ended (section 25.3.2.5). It is only enabled while a TX or
 *   a CCA is pending.
 *
 * A flag is set in RFCPEIFG even if it is not enabled in RFCPEIEN, so both
 * registers are checked. Flags are cleared by writing 0 to their bit, with a
 * plain write and not a read-modify-write (section 25.2.3.1).
 *
 * LAST_COMMAND_DONE does not tell which command has ended. An aborted RX
 * command raises it as well as the end of a TX or of a carrier sense, and the
 * flag of an aborted command can come late, see
 * cc26x2_cc13x2_rfc_abort_cmd(). So the flag only counts once the status
 * field of the command the driver is waiting for has a final value.
 */
void cc26x2_cc13x2_rfc_isr(void)
{
    ieee802154_dev_t *dev = cc26x2_cc13x2_rf_hal;

    /* Check if CPE_IRQ_RX_ENTRY_DONE is enabled and the flag is present */
    if ((RFC_DBELL->RFCPEIFG & CPE_IRQ_RX_ENTRY_DONE) &&
        (RFC_DBELL->RFCPEIEN & CPE_IRQ_RX_ENTRY_DONE)) {
        RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_RX_ENTRY_DONE;
        /* what comes in while the ACK of a frame is awaited is looked at
         * when the wait ends, see _tx_done() */
        if (_tx_state != TX_STATE_TX) {
            _handle_rx(dev);
        }
    }

    if ((RFC_DBELL->RFCPEIFG & CPE_IRQ_LAST_COMMAND_DONE) &&
        (RFC_DBELL->RFCPEIEN & CPE_IRQ_LAST_COMMAND_DONE)) {
        /* Clear the flag before reading the status fields: if the awaited
         * command ends after the read, the flag is raised again. */
        RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;

        if (_tx_running() &&
            (cc26x2_cc13x2_rf_tx_chain_state(_tx_cca, _tx_wait_ack) != -EAGAIN)) {
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            _tx_done(dev);
        }
        else if (_cca_pending &&
                 cc26x2_cc13x2_rfc_cmd_has_ended(rf_cmd_prop_cs.status)) {
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            _cca_done(dev);
        }
        else if (_rx_stopping && !_op_pending() &&
                 cc26x2_cc13x2_rf_rx_has_ended()) {
            /* the frame that kept RX on is over, and it was indicated above
             * if it was a good one */
            _rx_stopping = false;
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            _restore_rx();
        }
        /* otherwise the flag belongs to a command that was aborted before:
         * keep waiting */
    }
}

static int _write(ieee802154_dev_t *dev, const iolist_t *iolist)
{
    (void)dev;

    /* The Frame Control field tells whether an ACK has to be awaited after
     * the frame, and the sequence number which one. */
    uint8_t mhr[3] = { 0 };
    size_t len = 0;

    for (const iolist_t *iol = iolist; iol && (len < sizeof(mhr)); iol = iol->iol_next) {
        const uint8_t *base = iol->iol_base;

        for (size_t i = 0; (i < iol->iol_len) && (len < sizeof(mhr)); i++) {
            mhr[len++] = base[i];
        }
    }
    _tx_ack_req = (len == sizeof(mhr)) && (mhr[0] & IEEE802154_FCF_ACK_REQ);
    _tx_seq = mhr[2];

    return cc26x2_cc13x2_rf_write(iolist);
}

/* Reads the oldest of the frames that wait and closes its reception: the
 * entry goes back to the RX queue whatever the result is. */
static int _read(ieee802154_dev_t *dev, void *buf, size_t max_size,
                 ieee802154_rx_info_t *info)
{
    (void)dev;
    rfc_data_entry_general_t *entry;
    uint8_t *psdu;
    uint16_t pktlen;
    int res;

    /* the upper layer was told about a frame, so one should be waiting */
    entry = cc26x2_cc13x2_rf_recv();
    if (entry == NULL) {
        return -ENOENT;
    }

    /* the entry starts with the length, in two bytes */
    pktlen = _entry_len(entry);
    psdu = (&entry->data) + sizeof(uint16_t);

    if (pktlen < CC26X2_CC13X2_RF_RX_STATUS_LEN ||
        pktlen > IEEE802154G_FRAME_LEN_MAX + CC26X2_CC13X2_RF_RX_STATUS_LEN) {
        res = -ENOENT;
    }
    else if (buf == NULL) {
        res = 0;
    }
    else if (max_size < (size_t)(pktlen - CC26X2_CC13X2_RF_RX_STATUS_LEN)) {
        DEBUG("[cc26x2_cc13x2_rf]: buffer is too small\n");
        res = -ENOBUFS;
    }
    else {
        pktlen -= CC26X2_CC13X2_RF_RX_STATUS_LEN;
        DEBUG("[cc26x2_cc13x2_rf]: reading packet of length %i\n", pktlen);
        if (info != NULL) {
            /* the RF Core appends the RSSI after the PSDU */
            int8_t rssi = (int8_t)psdu[pktlen];

            DEBUG("RSSI: %i\n", (int)rssi);
            info->rssi = ieee802154_dbm_to_rssi(rssi);
            info->lqi = cc26x2_cc13x2_rf_rssi_to_lqi(rssi);
            info->timestamp = 0;
        }
        cc26x2_cc13x2_rf_copy_psdu(buf, psdu, pktlen);
        res = pktlen;
    }
    cc26x2_cc13x2_rf_rx_release();

    return res;
}

static int _len(ieee802154_dev_t *dev)
{
    (void)dev;

    /* the upper layer was told about a frame, so one should be waiting */
    rfc_data_entry_general_t *entry = cc26x2_cc13x2_rf_recv();
    if (entry == NULL) {
        return -ENOENT;
    }

    uint16_t pktlen = _entry_len(entry);
    if (pktlen < CC26X2_CC13X2_RF_RX_STATUS_LEN ||
        pktlen > IEEE802154G_FRAME_LEN_MAX + CC26X2_CC13X2_RF_RX_STATUS_LEN) {
        return -ENOENT;
    }

    return pktlen - CC26X2_CC13X2_RF_RX_STATUS_LEN;
}

static int _off(ieee802154_dev_t *dev)
{
    (void)dev;

    /* nothing of the MAC timing or of a pending operation survives */
    timer_stop(CC26X2_CC13X2_RF_TIMER);
    _cfg.ifs = false;
    _rx_enabled = false;
    _rx_stopping = false;
    _cca_pending = false;
    _tx_report = false;
    _csma_cancel();
    _tx_state = TX_STATE_IDLE;
    _state = STATE_IDLE;
    cc26x2_cc13x2_rf_rx_release_all();

    cc26x2_cc13x2_rf_off();

    return 0;
}

static int _request_op(ieee802154_dev_t *dev, ieee802154_hal_op_t op, void *ctx)
{
    int res;

    switch (op) {
    case IEEE802154_HAL_OP_TRANSMIT:
        if (_csma_conf.retries > 0) {
            /* CSMA-CA: the frame goes out after a backoff and a CCA. The
             * ACK of a received frame and the IFS do not hold the request
             * back, they are waited for when the backoff is over. */
            unsigned key = irq_disable();

            _tx_result = -EAGAIN;
            _tx_wait_ack = _tx_ack_req;
            _tx_frame_pending = false;
            _state = STATE_TX;
            _csma_nb = 0;
            _csma_be = _csma_conf.min_be;
            _csma_active = true;
            _csma_backoff();
            irq_restore(key);
            break;
        }
        if (_cfg.ifs) {
            DEBUG("[cc26x2_cc13x2_rf]: waiting IFS period\n");
            return -EBUSY;
        }
        /* Not reached through the SubMAC, which goes to idle before and is
         * refused there. A transmission started now would be hit by the ACK,
         * or hit it. */
        if (_ack_pending()) {
            DEBUG("[cc26x2_cc13x2_rf]: an ACK is pending\n");
            return -EBUSY;
        }

        _tx_result = -EAGAIN;
        _tx_wait_ack = _tx_ack_req;
        _tx_frame_pending = false;

        res = _tx_submit(_csma_conf.retries == 0);
        if (res > 0) {
            /* a busy channel, or a frame is being received */
            _tx_result = -EBUSY;
            _state = STATE_TX;
            dev->cb(dev, IEEE802154_RADIO_CONFIRM_TX_DONE);
            return 0;
        }
        if (res < 0) {
            return res;
        }
        _state = STATE_TX;
        break;
    case IEEE802154_HAL_OP_SET_RX:
        if (_state != STATE_IDLE && _state != STATE_RX) {
            return -EBUSY;
        }
        {
            unsigned key = irq_disable();

            _rx_enabled = true;
            if (_ack_pending()) {
                /* RX is started again when the ACK has been sent */
                _state = STATE_RX;
                irq_restore(key);
                break;
            }
            irq_restore(key);
        }
        _tx_state = TX_STATE_IDLE;
        /* -ENOBUFS: every RX entry holds a frame that waits to be read.
         * RX is started when the upper layer has read one and asks again. */
        res = cc26x2_cc13x2_rf_rx_start();
        if ((res != 0) && (res != -ENOBUFS)) {
            _rx_enabled = false;
            _state = STATE_IDLE;
            return res;
        }
        _state = STATE_RX;
        /* a frame that was kept while the upper layer could not take it */
        if (cc26x2_cc13x2_rf_recv() != NULL) {
            dev->cb(dev, IEEE802154_RADIO_INDICATION_RX_DONE);
        }
        break;
    case IEEE802154_HAL_OP_SET_IDLE: {
        assert(ctx);
        bool force = *((bool*) ctx);
        if (!force && _state != STATE_IDLE && _state != STATE_RX) {
            return -EBUSY;
        }
        /* an interrupt between the steps must not restart RX */
        unsigned key = irq_disable();

        if (force) {
            _rx_enabled = false;
            _csma_cancel();
            _rx_abort();
            _ack_cancel();
        }
        else if (_ack_pending()) {
            /* a frame was received and its ACK is not out yet: as busy as
             * while the frame was coming in */
            irq_restore(key);
            return -EBUSY;
        }
        else if (_rx_stop_unless_receiving() != 0) {
            /* a frame is being received: RX stays on */
            irq_restore(key);
            return -EBUSY;
        }
        else {
            _rx_enabled = false;
        }
        _state = STATE_IDLE;
        irq_restore(key);
        break;
    }
    case IEEE802154_HAL_OP_CCA:
        if ((_tx_state == TX_STATE_TX) || _csma_active) {
            return -EBUSY;
        }
        /* While the receiver runs the CCA is its own, see
         * cc26x2_cc13x2_rf_rx_cca(). The channel is also busy during the
         * reception of a frame, which may be below the threshold, and until
         * its ACK has been sent. RX goes on in all these cases. */
        res = _ack_pending() ? 1 : cc26x2_cc13x2_rf_rx_cca();
        if ((res > 0) || (_rx_stop_unless_receiving() != 0)) {
            _cca_result = CCA_RESULT_BUSY;
            dev->cb(dev, IEEE802154_RADIO_CONFIRM_CCA);
            return 0;
        }
        if (res == 0) {
            /* clear; the check for a reception has ended RX */
            _cca_result = CCA_RESULT_IDLE;
            dev->cb(dev, IEEE802154_RADIO_CONFIRM_CCA);
            _restore_rx();
            return 0;
        }
        /* The receiver is off: the RF Core senses the carrier with a radio
         * operation command of its own. */
        _state = STATE_IDLE;
        _cca_result = CCA_RESULT_NONE;
        _cca_pending = true;
        RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;
        RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_LAST_COMMAND_DONE;

        if ((res = cc26x2_cc13x2_rf_request_cca()) != 0) {
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            _cca_pending = false;
            _restore_rx();
            return res;
        }
        break;
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

    switch (op) {
    case IEEE802154_HAL_OP_TRANSMIT:
        if (_tx_result == -EAGAIN) {
            return -EAGAIN;
        }
        int res = _tx_result;
        _tx_result = -EAGAIN;
        _state = STATE_IDLE;
        if (res == -EBUSY) {
            /* the CCA before the transmission found the channel busy */
            res = 0;
            if (info != NULL) {
                info->status = TX_STATUS_MEDIUM_BUSY;
                info->retrans = 0;
            }
        }
        else if (res == -ETIMEDOUT) {
            /* the frame was sent and its ACK did not come */
            res = 0;
            if (info != NULL) {
                info->status = TX_STATUS_NO_ACK;
                info->retrans = 0;
            }
        }
        else if (res == 0 && info != NULL) {
            info->status = _tx_frame_pending ? TX_STATUS_FRAME_PENDING
                                             : TX_STATUS_SUCCESS;
            info->retrans = 0;
        }
        return res;
    case IEEE802154_HAL_OP_SET_RX:
        return _state == STATE_RX ? 0 : -EAGAIN;
    case IEEE802154_HAL_OP_SET_IDLE:
        return (_state == STATE_IDLE && _tx_state == TX_STATE_IDLE) ? 0 : -EAGAIN;
    case IEEE802154_HAL_OP_CCA:
        if (_cca_result == CCA_RESULT_NONE) {
            return -EAGAIN;
        }
        assert(ctx != NULL);
        *((bool *)ctx) = _cca_result == CCA_RESULT_IDLE;
        _cca_result = CCA_RESULT_NONE;
        return 0;
    default:
        assert(false);
        return -ENOTSUP;
    }
}

static int _request_on(ieee802154_dev_t *dev)
{
    (void)dev;
    _tx_state = TX_STATE_IDLE;
    cc26x2_cc13x2_rf_request_on();
    return 0;
}

static int _confirm_on(ieee802154_dev_t *dev)
{
    (void)dev;
    if (cc26x2_cc13x2_rf_confirm_on() != 0) {
        return -EAGAIN;
    }

    /* from now on a received frame raises an interrupt */
    RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_RX_ENTRY_DONE;

    return 0;
}

static int _set_cca_threshold(ieee802154_dev_t *dev, int8_t threshold)
{
    (void)dev;
    cc26x2_cc13x2_rf_set_cca_threshold(threshold);
    return 0;
}

static int _set_cca_mode(ieee802154_dev_t *dev, ieee802154_cca_mode_t mode)
{
    (void)dev;

    if (mode == IEEE802154_CCA_MODE_ED_THRESHOLD) {
        return 0;
    }

    return -ENOTSUP;
}

/*
 * conf->phy_mode selects what is applied:
 *
 * - IEEE802154_PHY_MR_FSK: conf is an ieee802154_mr_fsk_conf_t, as the SubMAC
 *   passes it. The symbol rate and the modulation index select the mode, see
 *   cc26x2_cc13x2_rf_set_fsk_mode(). Only 2-FSK without FEC is supported.
 * - IEEE802154_PHY_NO_OP: only the channel and the TX power change, as for the
 *   SubMAC shortcuts ieee802154_set_channel_number() and
 *   ieee802154_set_tx_power().
 * - Any other PHY is not supported by this radio.
 */
static int _config_phy(ieee802154_dev_t *dev, const ieee802154_phy_conf_t *conf)
{
    (void)dev;
    int ret;

    switch (conf->phy_mode) {
    case IEEE802154_PHY_MR_FSK: {
        const ieee802154_mr_fsk_conf_t *fsk = (const ieee802154_mr_fsk_conf_t *)conf;

        if ((fsk->mod_ord != 2) || (fsk->fec != IEEE802154_FEC_NONE)) {
            DEBUG("[cc26x2_cc13x2_rf]: only 2-FSK without FEC is supported\n");
            return -EINVAL;
        }
        if ((ret = cc26x2_cc13x2_rf_set_fsk_mode(fsk->srate, fsk->mod_idx)) != 0) {
            return ret;
        }
        break;
    }
    case IEEE802154_PHY_NO_OP:
        break;
    default:
        DEBUG("[cc26x2_cc13x2_rf]: PHY mode %d not supported\n", (int)conf->phy_mode);
        return -EINVAL;
    }

    if ((ret = cc26x2_cc13x2_rf_change_chan(conf->channel)) != 0) {
        DEBUG("[cc26x2_cc13x2_rf]: couldn't change channel to %d\n",
              conf->channel);
        return ret;
    }

    if ((ret = cc26x2_cc13x2_rf_set_tx_pwr(conf->pow)) != 0) {
        DEBUG("[cc26x2_cc13x2_rf]: couldn't set transmit power to %d dBm",
              conf->pow);
        return ret;
    }

    DEBUG("[cc26x2_cc13x2_rf]: channel %d, TX pwr: %d dBm\n", conf->channel, conf->pow);

    return 0;
}

static int _config_addr_filter(ieee802154_dev_t *dev, ieee802154_af_cmd_t cmd, const void *value)
{
    (void) dev;
    const network_uint16_t *short_addr = value;
    const eui64_t *ext_addr = value;
    const uint16_t *pan_id = value;
    switch (cmd) {
        case IEEE802154_AF_SHORT_ADDR:
            cc26x2_cc13x2_rf_set_short_addr(*short_addr);
            break;
        case IEEE802154_AF_EXT_ADDR:
            cc26x2_cc13x2_rf_set_long_addr(*ext_addr);
            break;
        case IEEE802154_AF_PANID:
            cc26x2_cc13x2_rf_set_pan_id(*pan_id);
            break;
        case IEEE802154_AF_PAN_COORD:
            return -ENOTSUP;
    }

    return 0;
}

static int _set_csma_params(ieee802154_dev_t *dev,
                            const ieee802154_csma_be_t *bd,
                            int8_t retries)
{
    (void)dev;

    if (bd != NULL) {
        if ((bd->min > bd->max) || (bd->max > CSMA_BE_MAX)) {
            return -EINVAL;
        }
        _csma_conf.min_be = bd->min;
        _csma_conf.max_be = bd->max;
    }
    /* Negative: a frame is sent right away. 0: it starts with a CCA and only
     * goes on if the channel is clear. Positive: CSMA-CA with that many
     * retries, see _csma_backoff(). */
    _csma_conf.retries = retries;

    return 0;
}

static int _config_src_addr_match(ieee802154_dev_t *dev, ieee802154_src_match_t cmd,
                                  const void *value)
{
    (void)dev;

    switch (cmd) {
        case IEEE802154_SRC_MATCH_EN:
            _ack[0] = IEEE802154_FCF_TYPE_ACK;
            if (*((const bool *)value)) {
                _ack[0] |= IEEE802154_FCF_FRAME_PEND;
            }
            break;
        default:
            return -ENOTSUP;
    }
    return 0;
}

static int _set_frame_filter_mode(ieee802154_dev_t *dev, ieee802154_filter_mode_t mode)
{
    (void)dev;

    switch (mode) {
        case IEEE802154_FILTER_ACCEPT:
            _cfg.promisc = false;
            _cfg.auto_ack = true;
            break;
        case IEEE802154_FILTER_PROMISC:
            _cfg.promisc = true;
            _cfg.auto_ack = false;
            break;
        case IEEE802154_FILTER_ACK_ONLY:
            /* the radio waits for the ACK of a frame itself
             * (IEEE802154_CAP_IRQ_ACK_TIMEOUT): the mode is not to be
             * implemented then */
            return -ENOTSUP;
        case IEEE802154_FILTER_SNIFFER:
            return -ENOTSUP;
        default:
            return -ENOTSUP;
    }

    return 0;
}

static int _get_frame_filter_mode(ieee802154_dev_t *dev,
                                  ieee802154_filter_mode_t *mode)
{
    (void)dev;

    if (_cfg.promisc) {
        *mode = IEEE802154_FILTER_PROMISC;
    }
    else {
        *mode = IEEE802154_FILTER_ACCEPT;
    }

    return 0;
}

void cc26x2_cc13x2_rf_hal_setup(ieee802154_dev_t *hal)
{
    /* There is one radio and its state is global, so hal->priv is not
     * used. The descriptor is kept for the interrupt handler. */
    hal->driver = &cc26x2_cc13x2_rf_ops;
    cc26x2_cc13x2_rf_hal = hal;
}

static const ieee802154_radio_ops_t cc26x2_cc13x2_rf_ops = {
    .caps = IEEE802154_CAP_SUB_GHZ
          | IEEE802154_CAP_AUTO_ACK
          | IEEE802154_CAP_AUTO_CSMA
          | IEEE802154_CAP_IRQ_ACK_TIMEOUT
          | IEEE802154_CAP_IRQ_TX_DONE
          | IEEE802154_CAP_IRQ_CCA_DONE
          | IEEE802154_CAP_PHY_MR_FSK,

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
    .config_addr_filter = _config_addr_filter,
    .config_src_addr_match = _config_src_addr_match,
    .set_csma_params = _set_csma_params,
    .set_frame_filter_mode = _set_frame_filter_mode,
    .get_frame_filter_mode = _get_frame_filter_mode,
};

static void _timer_cb(void *arg, int chan)
{
    (void)arg;

    if ((chan == MAC_TIMER_CHAN_ACK) && (_tx_state == TX_STATE_ACK_WAIT)) {
        iolist_t iolist = {
            .iol_next = NULL,
            .iol_base = _ack,
            .iol_len = ACK_PSDU_LEN,
        };

        /* send the ACK frame, from its own buffer: the frame of the upper
         * layer may be waiting for a retransmission in the other one */
        cc26x2_cc13x2_rf_write_ack(&iolist);

        /* an ACK is sent without CCA and is not acknowledged */
        _tx_cca = false;
        _tx_wait_ack = false;
        _tx_state = TX_STATE_TX_ACK;
        RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;
        RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_LAST_COMMAND_DONE;

        if (cc26x2_cc13x2_rf_request_transmit_ack() != 0) {
            /* The ACK is lost. Go on as after an ACK that was sent. */
            DEBUG("[cc26x2_cc13x2_rf]: failed to transmit the ACK\n");
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            _ack_done(cc26x2_cc13x2_rf_hal, false);
        }
    }
    else if (chan == MAC_TIMER_CHAN_IFS) {
        _cfg.ifs = false;
        if (_csma_due) {
            /* a backoff of CSMA-CA ended during the IFS */
            _csma_attempt(cc26x2_cc13x2_rf_hal);
        }
        else {
            _restore_rx();
        }
    }

    /* timer_stop() stops both channels: not while the other one is armed */
    if (!_cfg.ifs && (_tx_state != TX_STATE_ACK_WAIT)) {
        timer_stop(CC26X2_CC13X2_RF_TIMER);
    }
}

void cc26x2_cc13x2_rf_init(void)
{
    _ack[0] = IEEE802154_FCF_TYPE_ACK; /* FCF */
    _ack[1] = 0; /* FCF */

    cc26x2_cc13x2_rf_internal_init();

    int result = timer_init(CC26X2_CC13X2_RF_TIMER, TIMER_FREQ, _timer_cb, NULL);
    assert(result >= 0);
    (void)result;
    timer_stop(CC26X2_CC13X2_RF_TIMER);
}
