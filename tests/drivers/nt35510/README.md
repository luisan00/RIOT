## About

This is a manual test application for the NT35510 DSI display panel driver.
It runs on the STM32F769I-DISCO with the MB1166-A09 display board.

## Usage

```sh
make -C tests/drivers/nt35510 flash term
```

## Expected output

The application initializes the panel, draws three vertical bands (red,
green and blue) for two seconds, sweeps the brightness from the lowest to
the highest level in about two seconds, restores the initial brightness
and finally draws the RIOT logo in the middle of the screen:

```
NT35510 DSI display panel test application
Initializing display...[OK]
Drawing red, green and blue bands
Sweeping the brightness
Drawing the RIOT logo
SUCCESS
```

On older MB1166 revisions, which carry an OTM8009A controller instead of
an NT35510, the initialization fails with `-ENODEV`:

```
Initializing display...[Failed] (-19)
```
