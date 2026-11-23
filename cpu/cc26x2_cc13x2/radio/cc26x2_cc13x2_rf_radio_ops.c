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
 * @brief       IEEE 802.15.4 Radio HAL implementation for the cc26x2_cc13x2 RF driver
 *
 * @author      Jean Pierre Dudey <jeandudey@hotmail.com>
 * @author      Francisco Acosta <fco.ja.ac@gmail.com>
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include <errno.h>
#include <string.h>

#include "net/ieee802154/radio.h"
#include "periph/timer.h"

#include "cc26x2_cc13x2_rf.h"
#include "cc26x2_cc13x2_rf_internal.h"
#include "cc26x2_cc13x2_rfc.h"

#define ENABLE_DEBUG 0
#include "debug.h"

/** The FCS is calculated by the radio */
#define ACK_PSDU_LEN        (IEEE802154_ACK_FRAME_LEN - IEEE802154_FCS_LEN)

#define MAC_TIMER_CHAN_ACK  (0U) /**< MAC timer channel for transmitting an ACK
                                      frame */
#define MAC_TIMER_CHAN_IFS  (1U) /**< MAC timer channel for handling IFS
                                      logic */

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
    bool ack_only : 1; /**< whether only ACK frames should be indicated */
    bool auto_ack : 1; /**< whether the radio sends ACKs automatically */
    bool ifs      : 1; /**< whether the radio is in IFS mode */
    bool cca_send : 1; /**< whether a CCA is done before a transmission */
} _cfg = {
    .promisc  = false,
    .ack_only = false,
    .auto_ack = true,
    .ifs      = false,
    .cca_send = true,
};

static uint8_t _ack[ACK_PSDU_LEN];

enum {
    CCA_RESULT_NONE,    /**< No result produced yet */
    CCA_RESULT_IDLE,    /**< Channel is Idle */
    CCA_RESULT_BUSY,    /**< Channel is Busy */
} _cca_result;

/** CMD_PROP_CS was accepted by the RF Core and its end is awaited */
static volatile bool _cca_pending;

enum {
    TX_STATE_IDLE,         /**< We are not transmitting anything */
    TX_STATE_TX,           /**< Normal TX */
    TX_STATE_TX_ACK,       /**< Transmitting ACK */
};

static volatile uint8_t _tx_state;

/** The transmission in progress was requested with a CCA before it */
static bool _tx_cca;

/**
 * RX was asked to stop while a frame was coming in. It ends by itself after
 * the frame and is started again from the interrupt of the end of the
 * command, see _rx_stop_unless_receiving().
 */
static volatile bool _rx_stopping;

static void _rx_abort(void);

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

static void _handle_rx(ieee802154_dev_t *dev)
{
    rfc_data_entry_general_t *entry = cc26x2_cc13x2_rf_recv();

    if (entry == NULL) {
        return;
    }

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

    /* Pass the PSDU (the MHR) to the address filter. */
    bool l2filter_passed = cc26x2_cc13x2_rf_l2_filter(psdu);
    bool is_ack = _is_ack_frame(psdu);
    bool is_ack_req = psdu[0] & IEEE802154_FCF_ACK_REQ;

    if (_cfg.promisc) {
        dev->cb(dev, IEEE802154_RADIO_INDICATION_RX_DONE);
    }
    else if (_cfg.ack_only) {
        if (is_ack) {
            dev->cb(dev, IEEE802154_RADIO_INDICATION_RX_DONE);
        }
        else {
            entry->status = RFC_DATA_ENTRY_PENDING;
        }
    }
    /* If the L2 filter passes, check if the frame should be indicated
     * directly or if the driver should send an ACK frame before the
     * indication. */
    else if (l2filter_passed) {
        if (is_ack_req && _cfg.auto_ack) {
            /* Copy the sequence number to the ACK frame we'll send. */
            _ack[2] = psdu[2];

            /* To send the ACK we need to stop receiving. */
            _rx_abort();

            /* The ACK goes out AIFS after the frame. */
            timer_set(CC26X2_CC13X2_RF_TIMER, MAC_TIMER_CHAN_ACK,
                      cc26x2_cc13x2_rf_ack_delay_us());
            timer_start(CC26X2_CC13X2_RF_TIMER);
        }
        else {
            /* The frame doesn't request an ACK or automatic ACKs are
             * disabled. */
            dev->cb(dev, IEEE802154_RADIO_INDICATION_RX_DONE);
        }
    }
    else {
        entry->status = RFC_DATA_ENTRY_PENDING;
    }
}

static void _set_ifs_timer(void)
{
    _cfg.ifs = true;

    timer_set(CC26X2_CC13X2_RF_TIMER, MAC_TIMER_CHAN_IFS, IFS_US);
    timer_start(CC26X2_CC13X2_RF_TIMER);
}

/* Restart RX if the upper layer asked for it and it is not running */
static void _restore_rx(void)
{
    if (_rx_enabled && (cc26x2_cc13x2_rf_rx_start() == 0)) {
        _state = STATE_RX;
    }
}

/* Whether the end of a transmission or of a CCA is awaited */
static bool _op_pending(void)
{
    return (_tx_state != TX_STATE_IDLE) || _cca_pending;
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

/* CMD_PROP_TX_ADV has ended: PROP_DONE_OK (0x3400) if the frame was sent,
 * any other status is an error (SWCU185G, Table 25-184) */
static void _tx_done(ieee802154_dev_t *dev)
{
    int res = cc26x2_cc13x2_rf_tx_chain_state(_tx_cca);
    bool sent = (res == 0);
    uint8_t tx_state = _tx_state;

    _tx_state = TX_STATE_IDLE;

    if (tx_state == TX_STATE_TX_ACK) {
        /* The received frame is indicated whether or not its ACK went out:
         * the sender retransmits if it misses the ACK. */
        _restore_rx();
        _set_ifs_timer();
        dev->cb(dev, IEEE802154_RADIO_INDICATION_RX_DONE);
    }
    else {
        /* -EBUSY: the channel was busy and nothing was sent. The radio stays
         * idle, as the HAL asks: the SubMAC tries again after a backoff. */
        _tx_result = res;
        if (sent) {
            _set_ifs_timer();
        }
        dev->cb(dev, IEEE802154_RADIO_CONFIRM_TX_DONE);
    }
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
        _handle_rx(dev);
    }

    if ((RFC_DBELL->RFCPEIFG & CPE_IRQ_LAST_COMMAND_DONE) &&
        (RFC_DBELL->RFCPEIEN & CPE_IRQ_LAST_COMMAND_DONE)) {
        /* Clear the flag before reading the status fields: if the awaited
         * command ends after the read, the flag is raised again. */
        RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;

        if ((_tx_state != TX_STATE_IDLE) &&
            (cc26x2_cc13x2_rf_tx_chain_state(_tx_cca) != -EAGAIN)) {
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

    return cc26x2_cc13x2_rf_write(iolist);
}

static int _read(ieee802154_dev_t *dev, void *buf, size_t max_size,
                 ieee802154_rx_info_t *info)
{
    (void)dev;
    rfc_data_entry_general_t *entry;
    uint8_t *psdu;
    uint16_t pktlen;

    /* shouldn't be null as we informed about a packet */
    entry = cc26x2_cc13x2_rf_recv();
    if (entry == NULL) {
        return -ENOENT;
    }

    /* first two bytes are PSDU length */
    pktlen = _entry_len(entry);
    if (pktlen < CC26X2_CC13X2_RF_RX_STATUS_LEN ||
        pktlen > IEEE802154G_FRAME_LEN_MAX + CC26X2_CC13X2_RF_RX_STATUS_LEN) {
        entry->status = RFC_DATA_ENTRY_PENDING;
        return -ENOENT;
    }
    pktlen -= CC26X2_CC13X2_RF_RX_STATUS_LEN;
    DEBUG("[cc26x2_cc13x2_rf]: reading packet of length %i\n", pktlen);

    psdu = (&entry->data) + sizeof(uint16_t);

    if (buf == NULL) {
        entry->status = RFC_DATA_ENTRY_PENDING;
        return 0;
    }

    if (max_size < pktlen) {
        DEBUG("[cc26x2_cc13x2_rf]: buffer is too small\n");
        return -ENOBUFS;
    }
    else {
        if (info != NULL) {
            /* RSSI resides at the end of the PSDU */
            int8_t rssi = (int8_t)psdu[pktlen];

            DEBUG("RSSI: %i\n", (int)rssi);
            info->rssi = ieee802154_dbm_to_rssi(rssi);
            info->lqi = cc26x2_cc13x2_rf_rssi_to_lqi(rssi);
            info->timestamp = 0;
        }
        cc26x2_cc13x2_rf_copy_psdu(buf, psdu, pktlen);
        entry->status = RFC_DATA_ENTRY_PENDING;
    }

    return pktlen;
}

static int _len(ieee802154_dev_t *dev)
{
    (void)dev;

    /* shouldn't be null as we informed about a packet */
    rfc_data_entry_general_t *entry = cc26x2_cc13x2_rf_recv();
    assert(entry != NULL);

    uint16_t pktlen = _entry_len(entry);
    if (pktlen < CC26X2_CC13X2_RF_RX_STATUS_LEN ||
        pktlen > IEEE802154G_FRAME_LEN_MAX + CC26X2_CC13X2_RF_RX_STATUS_LEN) {
        entry->status = RFC_DATA_ENTRY_PENDING;
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
    _tx_state = TX_STATE_IDLE;
    _state = STATE_IDLE;

    cc26x2_cc13x2_rf_off();

    return 0;
}

static int _request_op(ieee802154_dev_t *dev, ieee802154_hal_op_t op, void *ctx)
{
    int res;

    switch (op) {
    case IEEE802154_HAL_OP_TRANSMIT:
        if (_cfg.ifs) {
            DEBUG("[cc26x2_cc13x2_rf]: waiting IFS period\n");
            return -EBUSY;
        }
        /* RX must have ended before the TX command is submitted, only one
         * radio operation command runs at a time. With CCA a frame that is
         * being received counts as a busy channel and is left alone. */
        if (!_cfg.cca_send) {
            _rx_abort();
        }
        else if (_rx_stop_unless_receiving() != 0) {
            _tx_result = -EBUSY;
            _state = STATE_TX;
            dev->cb(dev, IEEE802154_RADIO_CONFIRM_TX_DONE);
            return 0;
        }

        _tx_result = -EAGAIN;
        _tx_cca = _cfg.cca_send;
        _tx_state = TX_STATE_TX;
        RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;
        RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_LAST_COMMAND_DONE;

        if ((res = cc26x2_cc13x2_rf_request_transmit(_tx_cca)) != 0) {
            DEBUG("[cc26x2_cc13x2_rf]: failed to transmit\n");
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            _tx_state = TX_STATE_IDLE;
            _restore_rx();
            return res;
        }
        _state = STATE_TX;
        break;
    case IEEE802154_HAL_OP_SET_RX:
        if (_state != STATE_IDLE && _state != STATE_RX) {
            return -EBUSY;
        }
        _tx_state = TX_STATE_IDLE;
        _rx_enabled = true;
        if ((res = cc26x2_cc13x2_rf_rx_start()) != 0) {
            _rx_enabled = false;
            _state = STATE_IDLE;
            return res;
        }
        _state = STATE_RX;
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
            _rx_abort();
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
        /* the carrier sense is a radio operation command too, RX must have
         * ended before. During the reception of a frame the channel is
         * busy without looking. */
        if (_rx_stop_unless_receiving() != 0) {
            _cca_result = CCA_RESULT_BUSY;
            dev->cb(dev, IEEE802154_RADIO_CONFIRM_CCA);
            return 0;
        }
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
        else if (res == 0 && info != NULL) {
            info->status = TX_STATUS_SUCCESS;
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

    /* turn on the RX IRQs */
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
    (void)bd;

    /* The radio has no CSMA-CA with backoffs and retries of its own, the
     * SubMAC does that. It does the CCA of every attempt: with 0 retries a
     * transmission starts with a CCA and only goes on if the channel is
     * clear, with a negative number it is sent right away. */
    if (retries > 0) {
        return -EINVAL;
    }

    _cfg.cca_send = (retries == 0);

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
            _cfg.ack_only = false;
            _cfg.auto_ack = true;
            break;
        case IEEE802154_FILTER_PROMISC:
            _cfg.promisc = true;
            _cfg.ack_only = false;
            _cfg.auto_ack = false;
            break;
        case IEEE802154_FILTER_ACK_ONLY:
            _cfg.promisc = false;
            _cfg.ack_only = true;
            _cfg.auto_ack = false;
            break;
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
    else if (_cfg.ack_only) {
        *mode = IEEE802154_FILTER_ACK_ONLY;
    }
    else {
        *mode = IEEE802154_FILTER_ACCEPT;
    }

    return 0;
}

void cc26x2_cc13x2_rf_hal_setup(ieee802154_dev_t *hal)
{
    /* We don't set hal->priv because the context of this device is global */
    /* We need to store a reference to the HAL descriptor though for the ISR */
    hal->driver = &cc26x2_cc13x2_rf_ops;
    cc26x2_cc13x2_rf_hal = hal;
}

static const ieee802154_radio_ops_t cc26x2_cc13x2_rf_ops = {
    .caps = IEEE802154_CAP_SUB_GHZ
          | IEEE802154_CAP_AUTO_ACK
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

    if (chan == MAC_TIMER_CHAN_ACK) {
        iolist_t iolist = {
            .iol_next = NULL,
            .iol_base = _ack,
            .iol_len = ACK_PSDU_LEN,
        };

        /* send the ACK frame */
        cc26x2_cc13x2_rf_write(&iolist);

        /* an ACK is sent without CCA */
        _tx_cca = false;
        _tx_state = TX_STATE_TX_ACK;
        RFC_DBELL_NONBUF->RFCPEIFG = ~CPE_IRQ_LAST_COMMAND_DONE;
        RFC_DBELL_NONBUF->RFCPEIEN |= CPE_IRQ_LAST_COMMAND_DONE;

        if (cc26x2_cc13x2_rf_request_transmit(false) != 0) {
            /* The ACK is lost. Go back to RX and indicate the frame, as
             * after an ACK that was sent. */
            DEBUG("[cc26x2_cc13x2_rf]: failed to transmit the ACK\n");
            RFC_DBELL_NONBUF->RFCPEIEN &= ~CPE_IRQ_LAST_COMMAND_DONE;
            _tx_state = TX_STATE_IDLE;
            _restore_rx();
            cc26x2_cc13x2_rf_hal->cb(cc26x2_cc13x2_rf_hal,
                                     IEEE802154_RADIO_INDICATION_RX_DONE);
        }
    }
    else if (chan == MAC_TIMER_CHAN_IFS) {
        _cfg.ifs = false;
        _restore_rx();
    }

    timer_stop(CC26X2_CC13X2_RF_TIMER);
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
