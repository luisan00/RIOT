/*
 * SPDX-FileCopyrightText: 2019 Freie Universität Berlin
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Regression test to test subscribing to IPv6 packets while
 *              forwarding
 *
 * @author      Martine S. Lenders <m.lenders@fu-berlin.de>
 *
 * @}
 */

#include <errno.h>
#include <stdio.h>

#include "msg.h"
#include "net/ethernet/hdr.h"
#include "net/icmpv6.h"
#include "net/ipv6/addr.h"
#include "net/ipv6/hdr.h"
#include "net/udp.h"
#include "net/gnrc.h"
#include "net/gnrc.h"
#include "net/gnrc/ipv6/nib.h"
#include "net/netdev_test.h"
#include "net/protnum.h"
#include "od.h"
#include "sched.h"
#include "shell.h"
#include "test_utils/expect.h"
#include "thread.h"
#include "xtimer.h"

#include "common.h"

#define DUMPER_QUEUE_SIZE   (16)
#define NBR_MAC             { 0x57, 0x44, 0x33, 0x22, 0x11, 0x00, }
#define NBR_LINK_LOCAL      { 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                              0x55, 0x44, 0x33, 0xff, 0xfe, 0x22, 0x11, 0x00, }
#define DST                 { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0xab, 0xcd, \
                              0x55, 0x44, 0x33, 0xff, 0xfe, 0x22, 0x11, 0x00, }
#define DST_PFX_LEN         (64U)
/* IPv6 header + payload:     version+TC  FL: 0       plen: 16    NH:17 HL:64 */
#define L2_PAYLOAD          { 0x60, 0x00, 0x00, 0x00, 0x00, 0x10, 0x11, 0x40, \
                              /* source: random address */                    \
                              0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0xef, 0x01, \
                              0x02, 0xca, 0x4b, 0xef, 0xf4, 0xc2, 0xde, 0x01, \
                              /* destination: DST */                          \
                              0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0xab, 0xcd, \
                              0x55, 0x44, 0x33, 0xff, 0xfe, 0x22, 0x11, 0x00, \
                              /* random payload of length 16 */               \
                              0x54, 0xb8, 0x59, 0xaf, 0x3a, 0xb4, 0x5c, 0x85, \
                              0x1e, 0xce, 0xe2, 0xeb, 0x05, 0x4e, 0xa3, 0x85, }
/* a destination without a route */
#define NO_ROUTE_DST        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0xef, 0x02, \
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, }
/* prefix of the source of L2_PAYLOAD */
#define SRC_PFX             { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0xef, 0x01, }
#define SRC_PFX_LEN         (64U)
/* an address of the node, for the packet that encapsulates L2_PAYLOAD */
#define DECAP_DST           { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0xef, 0x03, \
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, }

static const uint8_t _nbr_mac[] = NBR_MAC;
static const ipv6_addr_t _nbr_link_local = { .u8 = NBR_LINK_LOCAL };
static const ipv6_addr_t _dst = { .u8 = DST };
static const ipv6_addr_t _no_route_dst = { .u8 = NO_ROUTE_DST };
static const ipv6_addr_t _src_pfx = { .u8 = SRC_PFX };
static const ipv6_addr_t _decap_dst = { .u8 = DECAP_DST };
static const uint8_t _l2_payload[] = L2_PAYLOAD;
static gnrc_netreg_entry_t _dumper;
static msg_t _dumper_queue[DUMPER_QUEUE_SIZE];
static char _dumper_stack[THREAD_STACKSIZE_MAIN];
/* destination unreachable messages handed to IPv6 for sending */
static volatile unsigned _dst_unr_sent;
static volatile unsigned _frames_forwarded;

static int _run_test(int argc, char **argv);
static int _run_rrq_test(int argc, char **argv);
static int _run_decap_test(int argc, char **argv);

static const shell_command_t shell_commands[] = {
    { "run_test", "runs the regression test", _run_test },
    { "run_rrq_test", "runs the reactive routing query test", _run_rrq_test },
    { "run_decap_test", "runs the decapsulation test", _run_decap_test },
    { NULL, NULL, NULL }
};

static void *_dumper_thread(void *arg)
{
    (void)arg;
    msg_init_queue(_dumper_queue, DUMPER_QUEUE_SIZE);

    while (1) {
        msg_t msg;

        msg_receive(&msg);
        if (msg.type == GNRC_NETAPI_MSG_TYPE_RCV) {
            gnrc_pktsnip_t *pkt = msg.content.ptr;

            /* wait a bit to give IPv6 time to handle the packet */
            xtimer_usleep(500);
            /* dump pkt. Should be equal to _l2_payloa*/
            puts("I got a subscription!");
            od_hex_dump(pkt->data, pkt->size, OD_WIDTH_DEFAULT);
            gnrc_pktbuf_release(pkt);
        }
        else if (msg.type == GNRC_NETAPI_MSG_TYPE_SND) {
            /* we are not interested in sent packets from the node itself;
             * just release it */
            gnrc_pktbuf_release(msg.content.ptr);
        }
    }

    return NULL;
}

static int _dump_etherframe(netdev_t *dev, const iolist_t *iolist)
{
    static uint8_t outbuf[sizeof(ethernet_hdr_t) + sizeof(_l2_payload)];
    size_t outbuf_len = 0U;

    (void)dev;
    while (iolist) {
        if ((outbuf_len + iolist->iol_len) > sizeof(outbuf)) {
            printf("Ignoring packet: %" PRIuSIZE " > %" PRIuSIZE "\n",
                  (outbuf_len + iolist->iol_len),
                  sizeof(outbuf));
            /* ignore larger packets */
            return outbuf_len;
        }
        memcpy(&outbuf[outbuf_len], iolist->iol_base, iolist->iol_len);
        outbuf_len += iolist->iol_len;
        iolist = iolist->iol_next;
    }

    puts("Forwarded Ethernet frame:");
    od_hex_dump(outbuf, outbuf_len, OD_WIDTH_DEFAULT);
    /* frames to the neighbor; not the router solicitations of the node */
    if (memcmp(outbuf, _nbr_mac, sizeof(_nbr_mac)) == 0) {
        _frames_forwarded++;
    }
    return outbuf_len;
}

static gnrc_pktsnip_t *_build_recvd_pkt(const ipv6_addr_t *dst)
{
    gnrc_pktsnip_t *netif;
    gnrc_pktsnip_t *pkt;

    netif = gnrc_netif_hdr_build(NULL, 0, NULL, 0);
    expect(netif);
    gnrc_netif_hdr_set_netif(netif->data, _mock_netif);
    pkt = gnrc_pktbuf_add(netif, _l2_payload, sizeof(_l2_payload),
                          GNRC_NETTYPE_IPV6);
    expect(pkt);
    ((ipv6_hdr_t *)pkt->data)->dst = *dst;
    return pkt;
}

static void _count_dst_unr(uint16_t cmd, gnrc_pktsnip_t *pkt, void *ctx)
{
    gnrc_pktsnip_t *icmpv6 = gnrc_pktsnip_search_type(pkt,
                                                      GNRC_NETTYPE_ICMPV6);

    (void)ctx;
    if ((cmd == GNRC_NETAPI_MSG_TYPE_SND) && (icmpv6 != NULL) &&
        (((icmpv6_hdr_t *)icmpv6->data)->type == ICMPV6_DST_UNR)) {
        _dst_unr_sent++;
    }
    gnrc_pktbuf_release(pkt);
}

static gnrc_netreg_entry_cbd_t _dst_unr_cbd = { .cb = _count_dst_unr };
static gnrc_netreg_entry_t _dst_unr_counter =
    GNRC_NETREG_ENTRY_INIT_CB(GNRC_NETREG_DEMUX_CTX_ALL, &_dst_unr_cbd);

static void _route_info_cb(unsigned type, const ipv6_addr_t *ctx_addr,
                           const void *ctx)
{
    (void)ctx;
    if (type == GNRC_IPV6_NIB_ROUTE_INFO_TYPE_RRQ) {
        char addr_str[IPV6_ADDR_MAX_STR_LEN];

        printf("Route request for %s\n",
               ipv6_addr_to_str(addr_str, ctx_addr, sizeof(addr_str)));
    }
}

/* forwards a packet to a destination without a route and returns how many
 * destination unreachable messages were sent for it */
static unsigned _forward_without_route(void)
{
    _dst_unr_sent = 0;
    gnrc_netapi_dispatch_receive(GNRC_NETTYPE_IPV6, GNRC_NETREG_DEMUX_CTX_ALL,
                                 _build_recvd_pkt(&_no_route_dst));
    /* give IPv6 time to handle the packet */
    xtimer_usleep(100 * US_PER_MS);
    return _dst_unr_sent;
}

static int _run_rrq_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* route back to the source, so the error messages can be sent */
    expect(gnrc_ipv6_nib_ft_add(&_src_pfx, SRC_PFX_LEN, &_nbr_link_local,
                                _mock_netif->pid, 0) == 0);
    gnrc_netreg_register(GNRC_NETTYPE_IPV6, &_dst_unr_counter);

    printf("Destination unreachable messages without routing protocol: %u\n",
           _forward_without_route());
    /* a reactive routing protocol may still find a route, so the NIB leaves
     * reporting a failure to it */
    _mock_netif->ipv6.route_info_cb = _route_info_cb;
    printf("Destination unreachable messages with routing protocol: %u\n",
           _forward_without_route());
    _mock_netif->ipv6.route_info_cb = NULL;

    gnrc_netreg_unregister(GNRC_NETTYPE_IPV6, &_dst_unr_counter);
    gnrc_ipv6_nib_ft_del(&_src_pfx, SRC_PFX_LEN);
    return 0;
}

/* L2_PAYLOAD encapsulated in an IPv6 packet for this node (RFC 2473): the
 * node must forward the encapsulated packet, once, and not the packet around
 * it */
static int _run_decap_test(int argc, char **argv)
{
    gnrc_pktsnip_t *netif, *pkt;
    ipv6_hdr_t *outer;

    (void)argc;
    (void)argv;
    /* the dumper thread would get a copy of each received packet */
    gnrc_netreg_unregister(GNRC_NETTYPE_IPV6, &_dumper);
    expect(gnrc_netif_ipv6_addr_add(_mock_netif, &_decap_dst, 128,
                                    GNRC_NETIF_IPV6_ADDRS_FLAGS_STATE_VALID) >= 0);
    netif = gnrc_netif_hdr_build(NULL, 0, NULL, 0);
    expect(netif);
    gnrc_netif_hdr_set_netif(netif->data, _mock_netif);
    pkt = gnrc_pktbuf_add(netif, NULL, sizeof(ipv6_hdr_t) + sizeof(_l2_payload),
                          GNRC_NETTYPE_IPV6);
    expect(pkt);
    outer = pkt->data;
    memset(outer, 0, sizeof(*outer));
    ipv6_hdr_set_version(outer);
    outer->len = byteorder_htons(sizeof(_l2_payload));
    outer->nh = PROTNUM_IPV6;
    outer->hl = 64;
    outer->src = _nbr_link_local;
    outer->dst = _decap_dst;
    memcpy(outer + 1, _l2_payload, sizeof(_l2_payload));

    _frames_forwarded = 0;
    gnrc_netapi_dispatch_receive(GNRC_NETTYPE_IPV6, GNRC_NETREG_DEMUX_CTX_ALL,
                                 pkt);
    /* give IPv6 time to handle the packet */
    xtimer_usleep(100 * US_PER_MS);
    printf("Frames forwarded after decapsulation: %u\n", _frames_forwarded);
    gnrc_netif_ipv6_addr_remove(_mock_netif, &_decap_dst);
    return 0;
}

static int _run_test(int argc, char **argv)
{
    int subscribers;
    (void)argc;
    (void)argv;
    if (_dumper.target.pid <= KERNEL_PID_UNDEF) {
        gnrc_netreg_entry_init_pid(&_dumper, GNRC_NETREG_DEMUX_CTX_ALL,
                                   thread_create(_dumper_stack,
                                                 sizeof(_dumper_stack),
                                                 THREAD_PRIORITY_MAIN - 1, 0,
                                                 _dumper_thread, NULL,
                                                 "dumper"));
        expect(_dumper.target.pid > KERNEL_PID_UNDEF);
        /* give dumper thread time to run */
        xtimer_usleep(200);
    }
    /* activate dumping of sent ethernet frames */
    netdev_ieee802154_t *netdev_ieee802154 = container_of(_mock_netif->dev,
                                                          netdev_ieee802154_t,
                                                          netdev);
    netdev_test_t *netdev_test = container_of(netdev_ieee802154,
                                              netdev_test_t,
                                              netdev);
    netdev_test_set_send_cb(netdev_test,
                            _dump_etherframe);
    /* first, test forwarding without subscription */
    subscribers = gnrc_netapi_dispatch_receive(GNRC_NETTYPE_IPV6,
                                               GNRC_NETREG_DEMUX_CTX_ALL,
                                               _build_recvd_pkt(&_dst));
    /* only IPv6 should be subscribed at the moment */
    expect(subscribers == 1);
    /* subscribe dumper thread for any IPv6 packets */
    gnrc_netreg_register(GNRC_NETTYPE_IPV6, &_dumper);
    /* now test forwarding with subscription */
    subscribers = gnrc_netapi_dispatch_receive(GNRC_NETTYPE_IPV6,
                                               GNRC_NETREG_DEMUX_CTX_ALL,
                                               _build_recvd_pkt(&_dst));
    /* expect 2 subscribers: IPv6 and gnrc_pktdump as registered above */
    expect(subscribers == 2);
    return 0;
}

int main(void)
{
    int res;

    /* initialize mock interface */
    _tests_init();
    /* define neighbor to forward to */
    res = gnrc_ipv6_nib_nc_set(&_nbr_link_local, _mock_netif->pid,
                               _nbr_mac, sizeof(_nbr_mac));
    expect(res == 0);
    /* set route to neighbor */
    res = gnrc_ipv6_nib_ft_add(&_dst, DST_PFX_LEN, &_nbr_link_local,
                               _mock_netif->pid, 0);
    expect(res == 0);
    /* start shell */
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    /* should be never reached */
    return 0;
}
