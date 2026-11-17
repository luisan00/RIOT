/*
 * SPDX-FileCopyrightText: 2015-2018 Freie Universität Berlin
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Tests extension header handling of gnrc stack.
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Takuo Yonezawa <Yonezawa-T2@mail.dnp.co.jp>
 * @author      Martine S. Lenders <m.lenders@fu-berlin.de>
 *
 * @}
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "iolist.h"
#include "msg.h"
#include "od.h"
#include "net/ieee802154.h"
#include "net/ipv6/addr.h"
#include "net/ipv6/hdr.h"
#include "net/gnrc/pkt.h"
#include "net/gnrc/pktbuf.h"
#include "net/gnrc/netreg.h"
#include "net/gnrc/netapi.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netif/conf.h"
#include "net/gnrc/netif/ieee802154.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc/ipv6/hdr.h"
#include "net/gnrc/pktdump.h"
#include "net/gnrc/sixlowpan/ctx.h"
#include "net/netdev_test.h"
#include "net/protnum.h"
#include "shell.h"
#include "test_utils/expect.h"
#include "ztimer.h"

#define IPV6_COUNT_RESET           (0x4480)
#define IPV6_COUNT_COUNT_UP        (0x4481)
#define SENT_6LO_FRAME             (0x4482)

#define IEEE802154_MAX_FRAG_SIZE    (102)
#define IEEE802154_LOCAL_EUI64     { \
        0x02, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x01 \
    }
#define IEEE802154_REMOTE_EUI64     { \
        0x02, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x02 \
    }

static gnrc_netif_t _netif;
static char _netif_stack[THREAD_STACKSIZE_DEFAULT];
static char _ipv6_count_stack[THREAD_STACKSIZE_DEFAULT];
static netdev_test_t _ieee802154_dev;
static const uint8_t _ieee802154_local_eui64[] = IEEE802154_LOCAL_EUI64;
static const uint8_t _ieee802154_remote_eui64[] = IEEE802154_REMOTE_EUI64;
static kernel_pid_t main_pid, ipv6_count_pid;

static void *_ipv6_count_thread(void *arg)
{
    static msg_t msg_queue[8];
    msg_t msg = { 0 };
    uint32_t received = 0;

    (void)arg;
    msg_init_queue(msg_queue, ARRAY_SIZE(msg_queue));
    while (true) {
        msg_receive(&msg);
        switch (msg.type) {
            case GNRC_NETAPI_MSG_TYPE_RCV:
                puts("Received 6LoWPAN frame in app.");
                gnrc_pktbuf_release(msg.content.ptr);
                msg.type = IPV6_COUNT_COUNT_UP;
                msg.content.value = ++received;
                msg_send(&msg, main_pid);
                break;
            case IPV6_COUNT_RESET: {
                msg_t reply = { .type = IPV6_COUNT_RESET };
                received = 0;
                msg_reply(&msg, &reply);
                break;
            }
        }
    }

    return NULL;
}

static int _get_netdev_device_type(netdev_t *netdev, void *value, size_t max_len)
{
    expect(max_len == sizeof(uint16_t));
    (void)netdev;

    *((uint16_t *)value) = NETDEV_TYPE_IEEE802154;
    return sizeof(uint16_t);
}

static int _get_netdev_proto(netdev_t *netdev, void *value, size_t max_len)
{
    expect(max_len == sizeof(gnrc_nettype_t));
    (void)netdev;

    *((gnrc_nettype_t *)value) = GNRC_NETTYPE_SIXLOWPAN;
    return sizeof(gnrc_nettype_t);
}

static int _get_netdev_max_packet_size(netdev_t *netdev, void *value,
                                       size_t max_len)
{
    expect(max_len == sizeof(uint16_t));
    (void)netdev;

    *((uint16_t *)value) = IEEE802154_MAX_FRAG_SIZE;
    return sizeof(uint16_t);
}

static int _get_netdev_src_len(netdev_t *netdev, void *value, size_t max_len)
{
    (void)netdev;
    expect(max_len == sizeof(uint16_t));
    *((uint16_t *)value) = sizeof(_ieee802154_local_eui64);
    return sizeof(uint16_t);
}

static int _get_netdev_addr_long(netdev_t *netdev, void *value, size_t max_len)
{
    (void)netdev;
    expect(max_len >= sizeof(_ieee802154_local_eui64));
    memcpy(value, _ieee802154_local_eui64, sizeof(_ieee802154_local_eui64));
    return sizeof(_ieee802154_local_eui64);
}

static int _netdev_send(netdev_t *netdev, const iolist_t *iolist);

static void _init_interface(void)
{
    netdev_test_setup(&_ieee802154_dev, NULL);
    netdev_test_set_send_cb(&_ieee802154_dev, _netdev_send);
    netdev_test_set_get_cb(&_ieee802154_dev, NETOPT_DEVICE_TYPE,
                           _get_netdev_device_type);
    netdev_test_set_get_cb(&_ieee802154_dev, NETOPT_PROTO,
                           _get_netdev_proto);
    netdev_test_set_get_cb(&_ieee802154_dev, NETOPT_MAX_PDU_SIZE,
                           _get_netdev_max_packet_size);
    netdev_test_set_get_cb(&_ieee802154_dev, NETOPT_SRC_LEN,
                           _get_netdev_src_len);
    netdev_test_set_get_cb(&_ieee802154_dev, NETOPT_ADDRESS_LONG,
                           _get_netdev_addr_long);
    gnrc_netif_ieee802154_create(&_netif,
            _netif_stack, THREAD_STACKSIZE_DEFAULT, GNRC_NETIF_PRIO,
            "dummy_netif", &_ieee802154_dev.netdev.netdev);
    ipv6_addr_t addr = IPV6_ADDR_UNSPECIFIED;

    /* fe80::ff:fe00:1 */
    addr.u8[0] = 0xfe;
    addr.u8[1] = 0x80;
    addr.u8[11] = 0xff;
    addr.u8[12] = 0xfe;
    addr.u8[15] = 0x01;

    ztimer_sleep(ZTIMER_MSEC, 1); /* wait for thread to start */
    if (gnrc_netapi_set(_netif.pid, NETOPT_IPV6_ADDR, 64U << 8U, &addr,
                        sizeof(addr)) < 0) {
        printf("error: unable to add IPv6 address fe80::ff:fe00:1/64 to interface %u\n",
               _netif.pid);
        return;
    }

    memset(&addr, 0, sizeof(addr));
    /* fd01::ff:fe00:1 */
    addr.u8[0] = 0xfd;
    addr.u8[1] = 0x01;
    addr.u8[11] = 0xff;
    addr.u8[12] = 0xfe;
    addr.u8[15] = 0x01;

    if (gnrc_netapi_set(_netif.pid, NETOPT_IPV6_ADDR, 64U << 8U, &addr,
                        sizeof(addr)) < 0) {
        printf("error: unable to add IPv6 address fd01::ff:fe00:1/64 to interface %u\n",
               _netif.pid);
        return;
    }

    /* set fd01::/64 as context 1 for maximum ltime
     * (should be long enough for this test ;-) ) */
    addr.u8[15] = 0;
    if (gnrc_sixlowpan_ctx_update(1, &addr, 64, 0xffff, true) == NULL) {
        puts("error: unable to set compression context for fd01::/64");
        return;
    }
}

static struct {
    gnrc_netif_hdr_t netif_hdr;
    uint8_t src[8];
    uint8_t dst[8];
} _netif_hdr = {
    .src = IEEE802154_REMOTE_EUI64,
    .dst = IEEE802154_LOCAL_EUI64,
};

static int _send_packet(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t data1[] = {
        /* 6LoWPAN Header */
        /* Fragmentation Header (first) */
        0xc0, 0x94, /* 0b11000: frag1, 0b00010010100: datagram_size (148) */
        0x00, 0x01, /* datagram_tag */
        /* 0b011: LOWPAN_IPHC */
        /* 0b11: Traffic Class and Flow Label are elided */
        /* 0b1: Next Header is compressed */
        /* 0b11: The Hop Limit field is compressed and the hop limit is 255 */
        0x7f,
        /* 0b0: No additional 8-bit Context Identifier Extension is used */
        /* 0b0: Source address compression uses stateless compression */
        /* 0b11: source address mode is 0 bits */
        /* 0b0: Destination address is not a multicast address */
        /* 0x0: Destination address compression uses stateless compression */
        /* 0x00: destination address mode is 128 bits */
        0x30,

        /* destination address: fd01::ff:fe00:1 */
        0xfd, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xff,
        0xfe, 0x00, 0x00, 0x01,

        /* 0b11110: UDP LOWPAN_NHC */
        /* 0b0: Checksum is carried in-line */
        /* 0b11: First 12 bits of both Source Port and Destination Port are 0xf0b and elided */
        0xf3,
        0x00, /* Source Port and Destination Port (4 bits each) */
        0x24, 0x2f, /* Checksum */

        /* payload */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    uint8_t data2[] = {
        /* 6LoWPAN Header */
        /* Fragmentation Header (rest) */
        0xe0, 0x94, /* 0b11100: frag1, 0b00010010100: datagram_size (148) */
        0x00, 0x01, /* datagram_tag */
        0x0c,       /* datagram_offset (12 * 8 = 96) */

        /* payload */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    msg_t msg = { 0 };

    gnrc_pktsnip_t *netif1 = gnrc_pktbuf_add(NULL,
                                            &_netif_hdr,
                                            sizeof(_netif_hdr),
                                            GNRC_NETTYPE_NETIF);
    gnrc_pktsnip_t *pkt1 = gnrc_pktbuf_add(netif1,
                                           data1,
                                           sizeof(data1),
                                           GNRC_NETTYPE_SIXLOWPAN);

    gnrc_netapi_dispatch_receive(GNRC_NETTYPE_SIXLOWPAN, GNRC_NETREG_DEMUX_CTX_ALL, pkt1);

    gnrc_pktsnip_t *netif2 = gnrc_pktbuf_add(NULL,
                                             &_netif_hdr,
                                             sizeof(_netif_hdr),
                                             GNRC_NETTYPE_NETIF);
    gnrc_pktsnip_t *pkt2 = gnrc_pktbuf_add(netif2,
                                           data2,
                                           sizeof(data2),
                                           GNRC_NETTYPE_SIXLOWPAN);
    gnrc_netapi_dispatch_receive(GNRC_NETTYPE_SIXLOWPAN, GNRC_NETREG_DEMUX_CTX_ALL, pkt2);
    msg_receive(&msg);

    if (msg.type == IPV6_COUNT_COUNT_UP) {
        printf("IPv6 packets received in app: %lu (queue capacity: %u)\n",
               (long unsigned)msg.content.value, msg_queue_capacity(main_pid));
    }

    return 0;
}

static uint8_t _data_buf[259];
static ssize_t _data_len = 0;

static int _preload_6lo(int argc, char **argv)
{
    ssize_t data_len;

    if (argc < 2) {
        printf("usage: %s <data hex>\n", argv[0]);
        return 1;
    }

    if ((data_len = scn_buf_hex(
        &_data_buf[_data_len], sizeof(_data_buf) - _data_len,
        argv[1], strlen(argv[1])
    )) < 0) {
        printf("Can not parse \"%s\" as hex string.", argv[1]);
        return 1;
    }
    _data_len += data_len;

    od_hex_dump(_data_buf, _data_len, OD_WIDTH_DEFAULT);

    return 0;
}

static int _inject_6lo(int argc, char **argv)
{
    gnrc_pktsnip_t *netif, *pkt;
    msg_t msg = { 0 }, reply = { 0 };
    ssize_t data_len;

    if (argc < 2) {
        if (_data_len == 0) {
            printf("usage: %s [<data hex>]\n", argv[0]);
            return 1;
        }
    }

    if (argc > 1) {
        if ((data_len = scn_buf_hex(
            &_data_buf[_data_len], sizeof(_data_buf) - _data_len,
            argv[1], strlen(argv[1])
        )) < 0) {
            printf("Can not parse \"%s\" as hex string.", argv[1]);
            return 1;
        }
        _data_len += data_len;
    }

    puts("================================================");
    msg.type = IPV6_COUNT_RESET;
    msg_send_receive(&msg, &reply, ipv6_count_pid);
    netif = gnrc_pktbuf_add(NULL, &_netif_hdr, sizeof(_netif_hdr),
                            GNRC_NETTYPE_NETIF);
    pkt = gnrc_pktbuf_add(netif, _data_buf, _data_len, GNRC_NETTYPE_SIXLOWPAN);
    _data_len = 0;

    gnrc_netapi_dispatch_receive(
        GNRC_NETTYPE_SIXLOWPAN,
        GNRC_NETREG_DEMUX_CTX_ALL,
        pkt
    );

    uint8_t count = 0;
    while (true) {
        /* receive all messages from IPv6 packet counting thread */
        msg.type = 0;

        if (ztimer_msg_receive_timeout(ZTIMER_MSEC, &msg, 100U) == -ETIME) {
            if (count == 0) {
                printf("No IPv6 packets received (queue capacity: %u)\n",
                       msg_queue_capacity(main_pid));
            }
            break;
        }
        else if (msg.type == IPV6_COUNT_COUNT_UP) {
            printf("IPv6 packets received in app: %lu (queue capacity: %u)\n",
                   (long unsigned)msg.content.value, msg_queue_capacity(main_pid));
            count++;
        }
    }

    return 0;
}

static volatile bool _capture;

/* Keeps the next unicast frame the interface sends, without its MAC header,
 * so that the inject command can feed it back in. Frames to the broadcast
 * address are left out: the host also sends router solicitations */
static int _netdev_send(netdev_t *netdev, const iolist_t *iolist)
{
    int size = 0;

    (void)netdev;
    for (const iolist_t *ptr = iolist; ptr != NULL; ptr = ptr->iol_next) {
        size += ptr->iol_len;
    }
    if (_capture &&
        (ieee802154_get_dst_len(iolist->iol_base) == IEEE802154_LONG_ADDRESS_LEN)) {
        msg_t msg = { .type = SENT_6LO_FRAME };

        _capture = false;
        _data_len = 0;
        for (const iolist_t *ptr = iolist->iol_next; ptr != NULL;
             ptr = ptr->iol_next) {
            if (((size_t)_data_len + ptr->iol_len) > sizeof(_data_buf)) {
                _data_len = 0;
                return -ENOBUFS;
            }
            memcpy(&_data_buf[_data_len], ptr->iol_base, ptr->iol_len);
            _data_len += ptr->iol_len;
        }
        msg_send(&msg, main_pid);
    }
    return size;
}

/* Sends a UDP packet encapsulated in IPv6 (RFC 2473) through 6LoWPAN, with
 * the inner IPv6 header in its own snip, or with "merged", in one snip with
 * its payload, and prints the 6LoWPAN frame. No address can be elided: there
 * is no context for 2001:db8::/64 */
static int _send_ipv6_in_ipv6(int argc, char **argv)
{
    static const ipv6_addr_t outer_src = {
        .u8 = { 0x20, 0x01, 0x0d, 0xb8, [15] = 0x0a }
    };
    static const ipv6_addr_t outer_dst = {
        .u8 = { 0x20, 0x01, 0x0d, 0xb8, [15] = 0x0b }
    };
    static const ipv6_addr_t inner_src = {
        .u8 = { 0x20, 0x01, 0x0d, 0xb8, [15] = 0x01 }
    };
    static const ipv6_addr_t inner_dst = {
        .u8 = { 0x20, 0x01, 0x0d, 0xb8, [15] = 0x02 }
    };
    static const uint8_t udp[] = {
        0xf0, 0xb0,             /* source port: 61616 */
        0xf0, 0xb1,             /* destination port: 61617 */
        0x00, 0x0c,             /* length: 12 */
        0x12, 0x34,             /* checksum, carried in-line as it is */
        0xde, 0xad, 0xbe, 0xef, /* payload */
    };
    bool merged = (argc > 1) && (strcmp(argv[1], "merged") == 0);
    gnrc_pktsnip_t *pkt, *netif;
    ipv6_hdr_t *hdr;
    msg_t msg = { 0 };

    if ((argc > 2) || ((argc == 2) && !merged)) {
        printf("usage: %s [merged]\n", argv[0]);
        return 1;
    }
    pkt = gnrc_pktbuf_add(NULL, udp, sizeof(udp), GNRC_NETTYPE_UDP);
    if (pkt == NULL) {
        puts("error: packet buffer full");
        return 1;
    }
    pkt = gnrc_ipv6_hdr_build(pkt, &inner_src, &inner_dst);
    if (pkt == NULL) {
        puts("error: packet buffer full");
        return 1;
    }
    hdr = pkt->data;
    hdr->len = byteorder_htons(sizeof(udp));
    hdr->nh = PROTNUM_UDP;
    hdr->hl = 64;
    if (merged && (gnrc_pktbuf_merge(pkt) != 0)) {
        puts("error: unable to merge the encapsulated packet");
        gnrc_pktbuf_release(pkt);
        return 1;
    }
    pkt = gnrc_ipv6_hdr_build(pkt, &outer_src, &outer_dst);
    if (pkt == NULL) {
        puts("error: packet buffer full");
        return 1;
    }
    hdr = pkt->data;
    hdr->len = byteorder_htons(gnrc_pkt_len(pkt->next));
    hdr->nh = PROTNUM_IPV6;
    hdr->hl = 64;
    netif = gnrc_netif_hdr_build(NULL, 0, _ieee802154_remote_eui64,
                                 sizeof(_ieee802154_remote_eui64));
    if (netif == NULL) {
        puts("error: packet buffer full");
        gnrc_pktbuf_release(pkt);
        return 1;
    }
    gnrc_netif_hdr_set_netif(netif->data, &_netif);
    pkt = gnrc_pkt_prepend(pkt, netif);

    _capture = true;
    if (gnrc_netapi_dispatch_send(GNRC_NETTYPE_SIXLOWPAN,
                                  GNRC_NETREG_DEMUX_CTX_ALL, pkt) == 0) {
        puts("error: no 6LoWPAN thread");
        _capture = false;
        gnrc_pktbuf_release(pkt);
        return 1;
    }
    while (ztimer_msg_receive_timeout(ZTIMER_MSEC, &msg, 100U) != -ETIME) {
        if (msg.type == SENT_6LO_FRAME) {
            puts("Sent 6LoWPAN frame:");
            od_hex_dump(_data_buf, _data_len, OD_WIDTH_DEFAULT);
            return 0;
        }
    }
    _capture = false;
    puts("No 6LoWPAN frame sent");
    return 1;
}

SHELL_COMMAND(preload, "Preload 6LoWPAN packet", _preload_6lo);
SHELL_COMMAND(inject, "Inject 6LoWPAN packet", _inject_6lo);
SHELL_COMMAND(send_fragments, "Test fragmentation with IPHC", _send_packet);
SHELL_COMMAND(send_ipv6_in_ipv6, "Send IPv6-in-IPv6 packet through 6LoWPAN",
              _send_ipv6_in_ipv6);

int main(void)
{
    gnrc_netif_t *netif;
    static msg_t msg_queue[4];
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    msg_init_queue(msg_queue, ARRAY_SIZE(msg_queue));
    main_pid = thread_getpid();

    ipv6_count_pid = thread_create(
        _ipv6_count_stack,
        THREAD_STACKSIZE_DEFAULT,
        GNRC_NETIF_PRIO - 1,
        0,
        _ipv6_count_thread,
        NULL,
        "6lo_count"
    );
    ztimer_sleep(ZTIMER_MSEC, 100); /* wait for thread to start */

    gnrc_netreg_entry_t dump_6lowpan = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, gnrc_pktdump_pid);
    gnrc_netreg_entry_t count_ipv6 = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, ipv6_count_pid);
    gnrc_netreg_entry_t dump_ipv6 = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, gnrc_pktdump_pid);
    gnrc_netreg_entry_t dump_udp = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, gnrc_pktdump_pid);
    gnrc_netreg_entry_t dump_udp_61616 = GNRC_NETREG_ENTRY_INIT_PID(61616, gnrc_pktdump_pid);

    _init_interface();
    netif = gnrc_netif_iter(NULL);
    gnrc_netif_hdr_init(&(_netif_hdr.netif_hdr), 8, 8);
    gnrc_netif_hdr_set_netif(&_netif_hdr.netif_hdr, netif);

    gnrc_netreg_register(GNRC_NETTYPE_SIXLOWPAN, &dump_6lowpan);
    gnrc_netreg_register(GNRC_NETTYPE_IPV6, &count_ipv6);
    gnrc_netreg_register(GNRC_NETTYPE_IPV6, &dump_ipv6);
    gnrc_netreg_register(GNRC_NETTYPE_UDP, &dump_udp);
    gnrc_netreg_register(GNRC_NETTYPE_UDP, &dump_udp_61616);

    shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
