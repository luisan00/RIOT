# Regression test for forwarding IPv6 packets with a sniffing subscriber

This only tests if a router can sniff its own forwarded IPv6 traffic. It is
**not** a full IPv6 test suite.

It also tests that the router answers a packet it has no route for with an
ICMPv6 destination unreachable message, unless a reactive routing protocol was
asked for a route.

## Usage

```
BOARD='<your choice>' make flash test
```
