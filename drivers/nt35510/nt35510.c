/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     drivers_nt35510
 * @{
 *
 * @file
 * @brief       Driver for NT35510-based DSI display panels
 *
 * The initialization sequence follows ST's BSD-3-Clause stm32-nt35510
 * component driver. The tearing effect output is left off, as it is not
 * used in video mode.
 *
 * @note The values of the sequence match the ones Linux uses for the same
 *       panel (panel-novatek-nt35510, "frida,frd400b25025"). The comments
 *       follow Linux's decoding of the values, which corrects some of the
 *       voltages given in ST's driver.
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "container.h"
#include "nt35510.h"
#include "nt35510_internal.h"
#include "periph/gpio.h"
#include "periph_cpu.h"
#include "ztimer.h"

#define ENABLE_DEBUG        0
#include "debug.h"

/* one entry of a command sequence */
typedef struct {
    const uint8_t *data; /* DCS command, then its parameters */
    uint8_t len;         /* number of parameters */
} _seq_entry_t;

/* builds an entry from the command and its parameters, counted at compile time */
#define SEQ(...) \
        { .data = (const uint8_t[]){ __VA_ARGS__ }, \
          .len = sizeof((const uint8_t[]){ __VA_ARGS__ }) - 1 }

/* power supply setup on page 1 of the manufacturer command set, then display control
 * on page 0, as done by ST's component driver for this panel */
static const _seq_entry_t _setup_seq[] = {
    SEQ(NT35510_CMD_MAUCCTR, 0x55, 0xaa, 0x52, 0x08, NT35510_MAUCCTR_PAGE1),
    SEQ(NT35510_P1_SETAVDD, 0x03, 0x03, 0x03), /* AVDD: 6.2 V */
    SEQ(NT35510_P1_BT1CTR, 0x46, 0x46, 0x46),  /* AVDD: ratio */
    SEQ(NT35510_P1_SETAVEE, 0x03, 0x03, 0x03), /* AVEE: -6.2 V */
    SEQ(NT35510_P1_BT2CTR, 0x36, 0x36, 0x36),  /* AVEE: ratio */
    SEQ(NT35510_P1_SETVCL, 0x00, 0x00, 0x02),  /* VCL: -2.5 V, -2.5 V, -3.5 V */
    SEQ(NT35510_P1_BT3CTR, 0x26, 0x26, 0x26),  /* VCL: ratio */
    SEQ(NT35510_P1_VGHCTR, 0x01),              /* VGH: on */
    SEQ(NT35510_P1_SETVGH, 0x09, 0x09, 0x09),  /* VGH: 16 V */
    SEQ(NT35510_P1_BT4CTR, 0x36, 0x36, 0x36),  /* VGH: ratio */
    SEQ(NT35510_P1_SETVGL, 0x08, 0x08, 0x08),  /* VGL_REG: -10 V */
    SEQ(NT35510_P1_BT5CTR, 0x26, 0x26, 0x26),  /* VGLX: ratio */
    SEQ(NT35510_P1_SETVGP, 0x00, 0x80, 0x00),  /* VGMP/VGSP: 4.6 V / 0 V */
    SEQ(NT35510_P1_SETVGN, 0x00, 0x80, 0x00),  /* VGMN/VGSN: -4.6 V / 0 V */
    SEQ(NT35510_P1_SETVCMOFF, 0x00, 0x50),     /* VCOM: -1 V */
    SEQ(NT35510_CMD_MAUCCTR, 0x55, 0xaa, 0x52, 0x08, NT35510_MAUCCTR_PAGE0),
    /* TE line enabled as ST does, the TE output stays off, TEON is never sent */
    SEQ(NT35510_P0_DOPCTR,
        NT35510_DOPCTR_0_RAMKP | NT35510_DOPCTR_0_DSITE | NT35510_DOPCTR_0_DSIG |
            NT35510_DOPCTR_0_DSIM | NT35510_DOPCTR_0_EOTP | NT35510_DOPCTR_0_N565,
        0x00),
    SEQ(NT35510_P0_SDHDTCTR, 0x03),                     /* source hold time: 1.5 us */
    SEQ(NT35510_P0_DPRSLCTR, NT35510_DPRSLCTR_480X800), /* resolution */
    SEQ(NT35510_P0_GSEQCTR, 0x00, 0x00),                /* gate EQ: 0 us */
    SEQ(NT35510_P0_SDEQCTR, 0x01, 0x02, 0x02, 0x02),    /* source EQ: mode 2, 1 us per step */
    SEQ(NT35510_P0_INVCTR, 0x00, 0x00, 0x00),           /* inversion: 2-dot */
    SEQ(NT35510_P0_DPMCTR12, 0x03, 0x00, 0x00),         /* gate overlap mode, R/L swap */
    SEQ(NT35510_P0_SDVPCTR, 0x01),                      /* off color in the vertical porch */
};

/* 800x480: rows and columns exchanged */
static const _seq_entry_t _landscape_seq[] = {
    SEQ(NT35510_CMD_MADCTL, NT35510_MADCTL_MX | NT35510_MADCTL_MV),
    SEQ(NT35510_CMD_CASET, 0x00, 0x00, 0x03, 0x1f),
    SEQ(NT35510_CMD_RASET, 0x00, 0x00, 0x01, 0xdf),
};

/* 480x800: the native orientation of the panel */
static const _seq_entry_t _portrait_seq[] = {
    SEQ(NT35510_CMD_MADCTL, 0x00),
    SEQ(NT35510_CMD_CASET, 0x00, 0x00, 0x01, 0xdf),
    SEQ(NT35510_CMD_RASET, 0x00, 0x00, 0x03, 0x1f),
};

/* brightness settings, then the display itself */
static const _seq_entry_t _on_seq[] = {
    SEQ(NT35510_CMD_WRCTRLD, NT35510_WRCTRLD_BCTRL | NT35510_WRCTRLD_DD | NT35510_WRCTRLD_BL),
    SEQ(NT35510_CMD_WRCABC, NT35510_WRCABC_STILL_MODE),
    SEQ(NT35510_CMD_WRCABCMB, 0xff), /* CABC never dims below full brightness */
    SEQ(NT35510_CMD_DISPON),
    SEQ(NT35510_CMD_RAMWR), /* start taking the frames from the video stream */
};

static int _write_seq(const _seq_entry_t *seq, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        int res = dsi_dcs_write(seq[i].data[0], &seq[i].data[1], seq[i].len);
        if (res < 0) {
            return res;
        }
    }
    return 0;
}

static void _reset(const nt35510_t *dev)
{
    if (!gpio_is_valid(dev->params->rst_pin)) {
        return;
    }
    gpio_init(dev->params->rst_pin, GPIO_OUT);
    gpio_clear(dev->params->rst_pin);
    ztimer_sleep(ZTIMER_MSEC, 20);
    gpio_set(dev->params->rst_pin);
    /* the controller needs a long settle time before it accepts commands */
    ztimer_sleep(ZTIMER_MSEC, 200);
}

static int _check_id(void)
{
    uint8_t id;
    int res = dsi_dcs_read(NT35510_CMD_RDID2, &id, 1);

    if (res < 0) {
        return res;
    }
    if (id != NT35510_ID2) {
        DEBUG("[nt35510] unexpected ID2 0x%02x\n", id);
        return -ENODEV;
    }
    return 0;
}

static int _setup(const nt35510_t *dev)
{
    int res = _check_id();

    if (res < 0) {
        return res;
    }

    res = _write_seq(_setup_seq, ARRAY_SIZE(_setup_seq));
    if (res < 0) {
        return res;
    }

    /* delay needed by the panel, MADCTL is not taken otherwise */
    ztimer_sleep(ZTIMER_MSEC, 200);

    if (dev->params->landscape) {
        res = _write_seq(_landscape_seq, ARRAY_SIZE(_landscape_seq));
    }
    else {
        res = _write_seq(_portrait_seq, ARRAY_SIZE(_portrait_seq));
    }
    if (res < 0) {
        return res;
    }

    res = dsi_dcs_write(NT35510_CMD_SLPOUT, NULL, 0);
    if (res < 0) {
        return res;
    }
    /* the datasheet asks for 5 ms before the next command, ST waits 120 ms */
    ztimer_sleep(ZTIMER_MSEC, 120);

    /* the DSI host streams 24-bit color */
    res = dsi_dcs_write(NT35510_CMD_COLMOD, (const uint8_t[]){ NT35510_COLMOD_RGB888 }, 1);
    if (res < 0) {
        return res;
    }

    res = nt35510_set_brightness(dev, dev->params->brightness);
    if (res < 0) {
        return res;
    }

    return _write_seq(_on_seq, ARRAY_SIZE(_on_seq));
}

int nt35510_init(nt35510_t *dev, const nt35510_params_t *params)
{
    assert(dev && params);

    dev->params = params;

    _reset(dev);

    /* initialize the DSI host and the LTDC, then start streaming; the DCS
     * transfers go out in low-power mode between video packets */
    int res = dsi_init();

    if (res < 0) {
        return res;
    }
    ltdc_init();
    dsi_start();

    res = _setup(dev);
    if (res < 0) {
        dsi_stop();
    }

    return res;
}

int nt35510_set_brightness(const nt35510_t *dev, uint8_t level)
{
    assert(dev);
    (void)dev;

    return dsi_dcs_write(NT35510_CMD_WRDISBV, &level, 1);
}
