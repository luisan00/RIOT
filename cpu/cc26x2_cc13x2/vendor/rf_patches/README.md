# RF Core patches

Patches for the firmware in the ROM of the RF Core of the CC13x2 and CC26x2,
from Texas Instruments. The radio driver applies them every time it powers
the RF Core up.

The files are unmodified copies from the SimpleLink CC13xx/CC26xx SDK
8.33.00.16 (`lpf2-8.33.00.16` of
https://github.com/TexasInstruments/simplelink-lowpower-f2-sdk), directory
`source/ti/devices/cc13x2_cc26x2/rf_patches`:

| File                    | Here                                |
|:----------------------- |:----------------------------------- |
| `rf_patch_cpe_prop.c`   | this directory                      |
| `rf_patch_cpe_prop.h`   | `cpu/cc26x2_cc13x2/include/vendor`  |

`rf_patch_cpe_prop` is the patch of the command and packet engine (CPE) for
the proprietary mode, the one TI's radio settings name for every PHY the
driver uses. Its license, BSD 3-Clause, is in the header of each file.
