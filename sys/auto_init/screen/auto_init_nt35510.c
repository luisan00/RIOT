/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     sys_auto_init
 * @{
 * @file
 * @brief       initializes an NT35510 DSI display panel
 *
 * The panel is driven by the LTDC through the DSI host, so the registered
 * display device is the LTDC one.
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 * @}
 */

#include "log.h"

#include "disp_dev.h"

#include "nt35510.h"
#include "nt35510_params.h"

static nt35510_t _dev;
static disp_dev_reg_t _disp_dev_reg;
static disp_dev_t _disp_dev;

extern const disp_dev_driver_t stm32_ltdc_disp_dev_driver;

void auto_init_nt35510(void)
{
    LOG_DEBUG("[auto_init_screen] initializing NT35510 panel\n");
    int res = nt35510_init(&_dev, &nt35510_params[0]);

    if (res < 0) {
        LOG_ERROR("[auto_init_screen] error initializing NT35510 panel (%d)\n",
                  res);
        return;
    }

    _disp_dev_reg.dev = &_disp_dev;
    _disp_dev_reg.screen_id = 0;
    _disp_dev_reg.dev->driver = &stm32_ltdc_disp_dev_driver;

    /* add to disp_dev registry */
    disp_dev_reg_add(&_disp_dev_reg);
}
