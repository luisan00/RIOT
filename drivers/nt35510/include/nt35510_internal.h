/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     drivers_nt35510
 * @{
 *
 * @file
 * @brief       Commands, registers and register values of the NT35510
 *
 * Register names follow Linux's panel-novatek-nt35510 driver, except
 * DPRSLCTR and INVCTR, which Linux does not name.
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name DCS standard commands
 * @{
 */
#define NT35510_CMD_SLPOUT      (0x11)  /**< Sleep out */
#define NT35510_CMD_DISPON      (0x29)  /**< Display on */
#define NT35510_CMD_CASET       (0x2a)  /**< Column address set */
#define NT35510_CMD_RASET       (0x2b)  /**< Row address set */
#define NT35510_CMD_RAMWR       (0x2c)  /**< Memory write */
#define NT35510_CMD_MADCTL      (0x36)  /**< Memory data access control */
#define NT35510_CMD_COLMOD      (0x3a)  /**< Interface pixel format */
#define NT35510_CMD_WRDISBV     (0x51)  /**< Write display brightness */
#define NT35510_CMD_WRCTRLD     (0x53)  /**< Write control display */
#define NT35510_CMD_WRCABC      (0x55)  /**< Write content adaptive brightness control */
#define NT35510_CMD_WRCABCMB    (0x5e)  /**< Write CABC minimum brightness */
#define NT35510_CMD_RDID2       (0xdb)  /**< Read ID2 */
/** @} */

/**
 * @name Manufacturer command set
 * @{
 */
#define NT35510_CMD_MAUCCTR     (0xf0)  /**< Manufacturer command set, page select */
#define NT35510_MAUCCTR_PAGE0   (0x00)  /**< Page 0: display control */
#define NT35510_MAUCCTR_PAGE1   (0x01)  /**< Page 1: power settings */
/** @} */

/**
 * @name Page 1 registers: power settings
 * @{
 */
#define NT35510_P1_SETAVDD      (0xb0)  /**< AVDD voltage */
#define NT35510_P1_SETAVEE      (0xb1)  /**< AVEE voltage */
#define NT35510_P1_SETVCL       (0xb2)  /**< VCL voltage */
#define NT35510_P1_SETVGH       (0xb3)  /**< VGH voltage */
#define NT35510_P1_SETVGL       (0xb5)  /**< VGL_REG voltage */
#define NT35510_P1_BT1CTR       (0xb6)  /**< AVDD booster control */
#define NT35510_P1_BT2CTR       (0xb7)  /**< AVEE booster control */
#define NT35510_P1_BT3CTR       (0xb8)  /**< VCL booster control */
#define NT35510_P1_BT4CTR       (0xb9)  /**< VGH booster control */
#define NT35510_P1_BT5CTR       (0xba)  /**< VGLX booster control */
#define NT35510_P1_SETVGP       (0xbc)  /**< VGMP/VGSP voltage */
#define NT35510_P1_SETVGN       (0xbd)  /**< VGMN/VGSN voltage */
#define NT35510_P1_SETVCMOFF    (0xbe)  /**< VCOM offset voltage */
#define NT35510_P1_VGHCTR       (0xbf)  /**< VGH output control */
/** @} */

/**
 * @name Page 0 registers: display control
 * @{
 */
#define NT35510_P0_DOPCTR       (0xb1)  /**< Display option control */
#define NT35510_P0_DPRSLCTR     (0xb5)  /**< Display resolution control */
#define NT35510_P0_SDHDTCTR     (0xb6)  /**< Source output data hold time */
#define NT35510_P0_GSEQCTR      (0xb7)  /**< Gate signal EQ control */
#define NT35510_P0_SDEQCTR      (0xb8)  /**< Source driver EQ control */
#define NT35510_P0_SDVPCTR      (0xba)  /**< Source driver behaviour in the vertical porch */
#define NT35510_P0_INVCTR       (0xbc)  /**< Inversion driving control */
#define NT35510_P0_DPMCTR12     (0xcc)  /**< Display timing control 12 */
/** @} */

/**
 * @name Register values
 * @{
 */
#define NT35510_COLMOD_RGB888       (0x77)  /**< 24 bits per pixel */
#define NT35510_ID2                 (0x80)  /**< ID2 of the NT35510 */
#define NT35510_MADCTL_MX           (0x40)  /**< Column address order */
#define NT35510_MADCTL_MV           (0x20)  /**< Row/column exchange */
#define NT35510_WRCTRLD_BCTRL       (0x20)  /**< Brightness control block on */
#define NT35510_WRCTRLD_DD          (0x08)  /**< Display dimming on */
#define NT35510_WRCTRLD_BL          (0x04)  /**< Backlight on */
#define NT35510_WRCABC_STILL_MODE   (0x02)  /**< CABC mode for still pictures */
#define NT35510_DPRSLCTR_480X800    (0x50)  /**< 480x800 resolution (CGM[7:0]) */
/** @} */

/**
 * @name DOPCTR bits, first parameter
 * @{
 */
#define NT35510_DOPCTR_0_RAMKP      (0x80)  /**< Frame memory kept in sleep in */
#define NT35510_DOPCTR_0_DSITE      (0x40)  /**< Tearing effect line enable */
#define NT35510_DOPCTR_0_DSIG       (0x20)  /**< Generic read/write enable */
#define NT35510_DOPCTR_0_DSIM       (0x10)  /**< Video mode data type enable */
#define NT35510_DOPCTR_0_EOTP       (0x08)  /**< No report of EoTP violations */
#define NT35510_DOPCTR_0_N565       (0x04)  /**< Alternative RGB565 byte order */
/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
