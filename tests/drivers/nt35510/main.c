/*
 * SPDX-FileCopyrightText: 2026 Luis A. Ruiz
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Test application for NT35510 DSI display panels
 *
 * @author      Luis A. Ruiz <luisan00@hotmail.com>
 *
 * @}
 */

#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "periph_cpu.h"
#include "timex.h"
#include "ztimer.h"

#include "nt35510.h"
#include "nt35510_params.h"

#include "riot_logo.h"

int main(void)
{
    nt35510_t dev;

    puts("NT35510 DSI display panel test application");

    printf("Initializing display...");
    int res = nt35510_init(&dev, &nt35510_params[0]);

    if (res < 0) {
        printf("[Failed] (%d)\n", res);
        return 1;
    }
    puts("[OK]");

    /* Enable backlight if macro is defined */
#ifdef BACKLIGHT_ON
    BACKLIGHT_ON;
#endif

    puts("Drawing red, green and blue bands");
    ltdc_fill(0, LCD_SCREEN_WIDTH / 3 - 1, 0, LCD_SCREEN_HEIGHT - 1, 0xf800);
    ltdc_fill(LCD_SCREEN_WIDTH / 3, 2 * (LCD_SCREEN_WIDTH / 3) - 1,
              0, LCD_SCREEN_HEIGHT - 1, 0x07e0);
    ltdc_fill(2 * (LCD_SCREEN_WIDTH / 3), LCD_SCREEN_WIDTH - 1,
              0, LCD_SCREEN_HEIGHT - 1, 0x001f);
    ztimer_sleep(ZTIMER_MSEC, 2 * MS_PER_SEC);

    puts("Sweeping the brightness");
    for (unsigned level = 0; level <= UINT8_MAX; level += 15) {
        res = nt35510_set_brightness(&dev, level);
        if (res < 0) {
            printf("[Failed] (%d)\n", res);
            return 1;
        }
        ztimer_sleep(ZTIMER_MSEC, 100);
    }
    nt35510_set_brightness(&dev, nt35510_params[0].brightness);

    puts("Drawing the RIOT logo");
    ltdc_clear();
    ltdc_map((LCD_SCREEN_WIDTH - RIOT_LOGO_WIDTH) / 2,
             (LCD_SCREEN_WIDTH + RIOT_LOGO_WIDTH) / 2 - 1,
             (LCD_SCREEN_HEIGHT - RIOT_LOGO_HEIGHT) / 2,
             (LCD_SCREEN_HEIGHT + RIOT_LOGO_HEIGHT) / 2 - 1,
             (const uint16_t *)picture);

    puts("SUCCESS");

    return 0;
}
