# RF Core patches

Patches for the firmware in the ROM of the RF Core of the CC13x2 and CC26x2,
from Texas Instruments. The radio driver applies them every time it powers
the RF Core up.

The files are unmodified copies from the SimpleLink CC13xx/CC26xx SDK
8.33.00.16 (`lpf2-8.33.00.16` of
https://github.com/TexasInstruments/simplelink-lowpower-f2-sdk), directory
`source/ti/devices/cc13x2_cc26x2/rf_patches`:

| File                            | Here                                |
|:------------------------------- |:----------------------------------- |
| `rf_patch_cpe_prop.c`           | this directory                      |
| `rf_patch_cpe_prop.h`           | `cpu/cc26x2_cc13x2/include/vendor`  |
| `rf_patch_cpe_ieee_802_15_4.c`  | this directory                      |
| `rf_patch_cpe_ieee_802_15_4.h`  | `cpu/cc26x2_cc13x2/include/vendor`  |

They are patches of the command and packet engine (CPE):

- `rf_patch_cpe_prop` for the proprietary mode. It is the patch that TI's
  radio settings name for every PHY the sub-GHz driver uses.
- `rf_patch_cpe_ieee_802_15_4` for the IEEE 802.15.4 mode. It is the patch of
  TI's setting for the O-QPSK PHY in the 2.4 GHz band.

Only the patch of the driver variant in use ends up in the firmware. The
license of the files, BSD 3-Clause, is in the header of each one.
