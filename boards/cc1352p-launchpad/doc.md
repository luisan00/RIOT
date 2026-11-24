@defgroup        boards_cc1352p_launchpad TI CC1352P LaunchPad
@ingroup         boards
@brief           Texas Instruments SimpleLink(TM) CC1352P Wireless MCU LaunchPad(TM) Kit

## Overview

The [LAUNCHXL-CC1352P](http://www.ti.com/tool/LAUNCHXL-CC1352P) is a Texas
Instrument's development kit for the CC1352P SoC which combines dual-band wireless MCU
with integrated power amplifier.

## Hardware

![LAUNCHPAD-CC1352P](http://www.ti.com/diagrams/launchxl-cc1352p_launchxl-cc1352p_mcu041a_cc1352p1.jpg)

| MCU               | CC1352R1              |
|:----------------- |:--------------------- |
| Family            | ARM Cortex-M4F        |
| Vendor            | Texas Instruments     |
| RAM               | 80KiB                 |
| Flash             | 352KiB                |
| Frequency         | 48MHz                 |
| FPU               | yes                   |
| Timers            | 4                     |
| ADCs              | 1x 12-bit (channels)  |
| UARTs             | 2                     |
| SPIs              | 2                     |
| I2Cs              | 1                     |
| Vcc               | 1.8V - 3.8V           |
| Datasheet         | [Datasheet](http://www.ti.com/lit/ds/symlink/cc1352p.pdf) (pdf file) |
| Reference Manual  | [Reference Manual](http://www.ti.com/lit/ug/swcu185d/swcu185d.pdf) |

The board comes in two variants with different RF matching network on the 20 dBm PA output port:

- LAUNCHXL-CC1352P1: 868/915 MHz up to 20 dBm, 2.4 GHz up to 5 dBm
- LAUNCHXL-CC1352P-2: 868/915 MHz up to 14 dBm, 2.4 GHz up to 20 dBm.

## Radio

The sub-GHz radio is supported by the @ref cpu_cc26x2_cc13x2_rf driver, which
implements the MR-FSK PHY of IEEE 802.15.4g. Applications that use
`netdev_default` get a network interface for it.

The default frequency band is 863 MHz (Europe), with the channel plan and the
operating mode that RIOT's AT86RF215 driver uses, for compatibility with it.
The plan of the standard, the other bands, the channel spacing and the
operating modes are described in the documentation of the driver.

The driver uses the sub-GHz path of the on-board RF switch with the standard
PA, which gives up to 13 dBm, or 14 dBm in a build with
`CONFIG_CC26XX_CC13XX_FORCE_VDDR_HH`. The 2.4 GHz band is not supported yet.

The two variants of the board differ in their 20 dBm PA, so the build has to
know which one it is for. `BOARD_VERSION` selects it, `cc1352p1` (the default)
or `cc1352p-2`:

    BOARD=cc1352p-launchpad BOARD_VERSION=cc1352p-2 make ...

On the LAUNCHXL-CC1352P1 the 20 dBm PA is for the 868/915 MHz band. The
driver uses it for TX powers above those of the standard PA when it is built
with `CFLAGS=-DCONFIG_CC26X2_CC13X2_RF_PA20=1`; the RF Core then moves the RF
switch itself between that PA in TX and the sub-GHz path in RX. This is off
by default, as only some channels of some bands allow such powers.

The radio has been tested on the LAUNCHXL-CC1352P1. Nobody has tried it on
the LAUNCHXL-CC1352P-2 yet.

The 48 MHz crystal of this LaunchPad needs less load than the factory trim of
the MCU gives. TI's board support sets a cap-array delta in the CCFG for it,
and RIOT uses the same one. RIOT only writes the CCFG when it is built with
`CFLAGS=-DCONFIG_CC26XX_CC13XX_UPDATE_CCFG=1`, so flash such a build once.
Without the delta the carrier of a LAUNCHXL-CC1352P1 is about 55 ppm low,
outside the tolerance of IEEE Std 802.15.4-2024 (20.6.3), and at 50 ksym/s it
can neither hear nor be heard by radios that are on frequency.

## Board pinout

The [LAUNCHXL-CC1352P1 Quick Start Guide](https://www.ti.com/lit/ug/swau108a/swau108a.pdf)
provides the default pinout for the board.

## Flashing the Device

Flashing RIOT is quite straight forward. The board comes with an XDS110 on-board
debug probe that provides programming, flashing and debugging capabilities
through the USB Micro-USB connector. Once either TI Uniflash or OpenOCD are
installed just connect the board using the Micro-USB port to your computer and
type:

```shell
BOARD=cc1352p-launchpad make flash
```

To use OpenOCD instead of uniflash we need to set the `PROGRAMMER` environment
variable, this is to enable OpenOCD instead of Uniflash.

```shell
PROGRAMMER=openocd BOARD=cc1352p-launchpad make flash
```

For detailed information about CC1352P MCUs as well as configuring, compiling
RIOT and installation of flashing tools for CC1352P boards,
see \ref cpu_cc26xx_cc13xx.
