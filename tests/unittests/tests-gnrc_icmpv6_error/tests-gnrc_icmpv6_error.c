/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @{
 *
 * @file
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "embUnit.h"
#include "net/gnrc/icmpv6/error.h"
#include "net/gnrc/netreg.h"
#include "net/gnrc/pktbuf.h"
#include "net/icmpv6.h"
#include "net/ipv6/hdr.h"
#include "net/protnum.h"

#include "tests-gnrc_icmpv6_error.h"

/* an ICMPv6 header of the given type, the rest of it does not matter here */
#define ICMPV6_HDR(type)    { (type), 0, 0, 0, 0, 0, 0, 0 }

static const ipv6_addr_t _src = { {
    0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
} };
static const ipv6_addr_t _dst = { {
    0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
} };

/* number of error messages handed to IPv6 for sending */
static unsigned _sent;

static void _ipv6_cb(uint16_t cmd, gnrc_pktsnip_t *pkt, void *ctx)
{
    (void)ctx;
    if (cmd == GNRC_NETAPI_MSG_TYPE_SND) {
        _sent++;
    }
    gnrc_pktbuf_release(pkt);
}

static gnrc_netreg_entry_cbd_t _ipv6_cbd = { .cb = _ipv6_cb };
static gnrc_netreg_entry_t _ipv6_entry =
    GNRC_NETREG_ENTRY_INIT_CB(GNRC_NETREG_DEMUX_CTX_ALL, &_ipv6_cbd);

/* the invoking packet: an IPv6 header followed by one or two parts */
static ipv6_hdr_t _ipv6_hdr;
static gnrc_pktsnip_t _ipv6_snip;
static gnrc_pktsnip_t _part_snips[2];

static void set_up(void)
{
    gnrc_pktbuf_init();
    gnrc_netreg_init();
    gnrc_netreg_register(GNRC_NETTYPE_IPV6, &_ipv6_entry);
}

static void tear_down(void)
{
    gnrc_netreg_unregister(GNRC_NETTYPE_IPV6, &_ipv6_entry);
}

static gnrc_pktsnip_t *_orig_pkt(bool rcv_order, uint8_t nh,
                                 uint8_t *part1, size_t len1,
                                 uint8_t *part2, size_t len2)
{
    memset(&_ipv6_hdr, 0, sizeof(_ipv6_hdr));
    ipv6_hdr_set_version(&_ipv6_hdr);
    _ipv6_hdr.len = byteorder_htons(len1 + len2);
    _ipv6_hdr.nh = nh;
    _ipv6_hdr.hl = 64;
    _ipv6_hdr.src = _src;
    _ipv6_hdr.dst = _dst;

    _ipv6_snip = (gnrc_pktsnip_t){
        .data = &_ipv6_hdr, .size = sizeof(_ipv6_hdr),
        .users = 1, .type = GNRC_NETTYPE_IPV6,
    };
    _part_snips[0] = (gnrc_pktsnip_t){
        .data = part1, .size = len1, .users = 1, .type = GNRC_NETTYPE_UNDEF,
    };
    _part_snips[1] = (gnrc_pktsnip_t){
        .data = part2, .size = len2, .users = 1, .type = GNRC_NETTYPE_UNDEF,
    };

    if (rcv_order) {
        /* innermost part first, IPv6 header last */
        _part_snips[0].next = &_ipv6_snip;
        if (part2 != NULL) {
            _part_snips[1].next = &_part_snips[0];
            return &_part_snips[1];
        }
        return &_part_snips[0];
    }
    /* IPv6 header first, innermost part last */
    _ipv6_snip.next = &_part_snips[0];
    if (part2 != NULL) {
        _part_snips[0].next = &_part_snips[1];
    }
    return &_ipv6_snip;
}

/* asks for every kind of error message and returns how many were sent */
static unsigned _send_errors(const gnrc_pktsnip_t *orig_pkt, bool rcv_order)
{
    _sent = 0;
    gnrc_icmpv6_error_dst_unr_send(ICMPV6_ERROR_DST_UNR_NO_ROUTE, orig_pkt);
    gnrc_icmpv6_error_pkt_too_big_send(1280U, orig_pkt);
    gnrc_icmpv6_error_time_exc_send(ICMPV6_ERROR_TIME_EXC_HL, orig_pkt);
    if (rcv_order) {
        /* only received packets can have a parameter problem */
        gnrc_icmpv6_error_param_prob_send(ICMPV6_ERROR_PARAM_PROB_HDR_FIELD,
                                          &_ipv6_hdr.nh, orig_pkt);
    }
    return _sent;
}

/* checks for both orders of the invoking packet whether it is answered */
static void _check(uint8_t nh, uint8_t *part1, size_t len1,
                   uint8_t *part2, size_t len2, bool answered)
{
    for (unsigned order = 0; order < 2; order++) {
        bool rcv_order = (order == 1);
        unsigned expected = answered ? (rcv_order ? 4U : 3U) : 0U;
        gnrc_pktsnip_t *pkt = _orig_pkt(rcv_order, nh, part1, len1,
                                        part2, len2);

        TEST_ASSERT_EQUAL_INT(expected, _send_errors(pkt, rcv_order));
        TEST_ASSERT(gnrc_pktbuf_is_empty());
    }
}

static void test_gnrc_icmpv6_error__udp(void)
{
    uint8_t udp[] = { 0x12, 0x34, 0x00, 0x09, 0x00, 0x08, 0x00, 0x00 };

    _check(PROTNUM_UDP, udp, sizeof(udp), NULL, 0, true);
}

static void test_gnrc_icmpv6_error__icmpv6_informational(void)
{
    const uint8_t types[] = { ICMPV6_ECHO_REQ, ICMPV6_ECHO_REP, 255 };

    for (unsigned i = 0; i < sizeof(types); i++) {
        uint8_t icmpv6[] = ICMPV6_HDR(types[i]);

        _check(PROTNUM_ICMPV6, icmpv6, sizeof(icmpv6), NULL, 0, true);
    }
}

/* see https://tools.ietf.org/html/rfc4443#section-2.4 (e.1) */
static void test_gnrc_icmpv6_error__icmpv6_error(void)
{
    const uint8_t types[] = { 0, ICMPV6_DST_UNR, ICMPV6_PKT_TOO_BIG,
                              ICMPV6_TIME_EXC, ICMPV6_PARAM_PROB, 127 };

    for (unsigned i = 0; i < sizeof(types); i++) {
        uint8_t icmpv6[] = ICMPV6_HDR(types[i]);

        _check(PROTNUM_ICMPV6, icmpv6, sizeof(icmpv6), NULL, 0, false);
    }
}

/* see https://tools.ietf.org/html/rfc4443#section-2.4 (e.2) */
static void test_gnrc_icmpv6_error__icmpv6_redirect(void)
{
    uint8_t icmpv6[] = ICMPV6_HDR(ICMPV6_REDIRECT);

    _check(PROTNUM_ICMPV6, icmpv6, sizeof(icmpv6), NULL, 0, false);
}

static void test_gnrc_icmpv6_error__icmpv6_error_after_ext_hdrs(void)
{
    uint8_t ext_hdrs[] = {
        /* hop-by-hop options header with a PadN option */
        PROTNUM_IPV6_EXT_DST, 0, 1, 4, 0, 0, 0, 0,
        /* destination options header with a PadN option */
        PROTNUM_ICMPV6, 1, 1, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    uint8_t icmpv6[] = ICMPV6_HDR(ICMPV6_DST_UNR);
    uint8_t payload[sizeof(ext_hdrs) + sizeof(icmpv6)];

    memcpy(payload, ext_hdrs, sizeof(ext_hdrs));
    memcpy(payload + sizeof(ext_hdrs), icmpv6, sizeof(icmpv6));
    /* all in one snip */
    _check(PROTNUM_IPV6_EXT_HOPOPT, payload, sizeof(payload), NULL, 0, false);
    /* extension headers and ICMPv6 header in snips of their own */
    _check(PROTNUM_IPV6_EXT_HOPOPT, ext_hdrs, sizeof(ext_hdrs),
           icmpv6, sizeof(icmpv6), false);
}

static void test_gnrc_icmpv6_error__icmpv6_error_first_fragment(void)
{
    /* fragment offset 0, more fragments follow */
    uint8_t frag[] = { PROTNUM_ICMPV6, 0, 0x00, 0x01, 0x12, 0x34, 0x56, 0x78 };
    uint8_t icmpv6[] = ICMPV6_HDR(ICMPV6_DST_UNR);

    _check(PROTNUM_IPV6_EXT_FRAG, frag, sizeof(frag),
           icmpv6, sizeof(icmpv6), false);
}

/* the type of the ICMPv6 message is unknown, so it is answered as before */
static void test_gnrc_icmpv6_error__icmpv6_type_unknown(void)
{
    /* fragment offset 8: the ICMPv6 header is in another fragment */
    uint8_t frag[] = { PROTNUM_ICMPV6, 0, 0x00, 0x08, 0x12, 0x34, 0x56, 0x78 };
    uint8_t data[] = ICMPV6_HDR(ICMPV6_DST_UNR);
    /* encapsulating security payload: the rest is encrypted */
    uint8_t esp[] = { 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01 };
    uint8_t none[1];

    _check(PROTNUM_IPV6_EXT_FRAG, frag, sizeof(frag), data, sizeof(data), true);
    _check(PROTNUM_IPV6_EXT_ESP, esp, sizeof(esp), NULL, 0, true);
    /* the ICMPv6 header is missing */
    _check(PROTNUM_ICMPV6, none, 0, NULL, 0, true);
}

static Test *tests_gnrc_icmpv6_error_tests(void)
{
    EMB_UNIT_TESTFIXTURES(fixtures) {
        new_TestFixture(test_gnrc_icmpv6_error__udp),
        new_TestFixture(test_gnrc_icmpv6_error__icmpv6_informational),
        new_TestFixture(test_gnrc_icmpv6_error__icmpv6_error),
        new_TestFixture(test_gnrc_icmpv6_error__icmpv6_redirect),
        new_TestFixture(test_gnrc_icmpv6_error__icmpv6_error_after_ext_hdrs),
        new_TestFixture(test_gnrc_icmpv6_error__icmpv6_error_first_fragment),
        new_TestFixture(test_gnrc_icmpv6_error__icmpv6_type_unknown),
    };

    EMB_UNIT_TESTCALLER(gnrc_icmpv6_error_tests, set_up, tear_down, fixtures);

    return (Test *)&gnrc_icmpv6_error_tests;
}

void tests_gnrc_icmpv6_error(void)
{
    TESTS_RUN(tests_gnrc_icmpv6_error_tests());
}
/** @} */
