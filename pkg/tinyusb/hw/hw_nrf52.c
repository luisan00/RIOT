/*
 * SPDX-FileCopyrightText: 2022 Gunar Schorcht
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     pkg_tinyusb
 * @brief
 * @{
 *
 * @brief       tinyUSB hardware driver for nRF52 MCUs
 * @author      Gunar Schorcht <gunar@schorcht.net>
 */

#include <errno.h>

#include "periph_conf.h"
#include "periph/gpio.h"
#include "periph/pm.h"

#include "tusb.h"
#include "device/usbd.h"
#include "host/usbh.h"

/*
 * Definition of events as used by the tinyusb/portable/nordic/nrf5x/dcd_nrf5x.c
 * https://github.com/hathach/tinyusb/blob/0.14.0/src/portable/nordic/nrf5x/dcd_nrf5x.c#L954
 */
#define USB_EVT_DETECTED    0
#define USB_EVT_REMOVED     1
#define USB_EVT_READY       2

extern void tusb_hal_nrf_power_event(uint32_t event);

int tinyusb_hw_init(void)
{
    if (IS_USED(MODULE_TINYUSB_DEVICE)) {
        NVIC_SetPriority(USBD_IRQn, 2);
    }

    /* The USB supply events can come at any time: the cable can be plugged
     * in after boot, and after a soft reset the USB regulator gets ready
     * later than any fixed wait. They are forwarded from the POWER
     * interrupt (isr_power_clock()). A cable that is already connected
     * gave its events before: they are posted here, with the interrupt
     * still disabled, so that both never run at the same time. Events in
     * between stay pending and are forwarded once the interrupt is
     * enabled; tinyusb ignores an event whose work is already done. */
    NRF_POWER->EVENTS_USBDETECTED = 0;
    NRF_POWER->EVENTS_USBREMOVED = 0;
    NRF_POWER->EVENTS_USBPWRRDY = 0;
    NRF_POWER->INTENSET = POWER_INTENSET_USBDETECTED_Msk | POWER_INTENSET_USBREMOVED_Msk |
                          POWER_INTENSET_USBPWRRDY_Msk;

    if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) {
        tusb_hal_nrf_power_event(USB_EVT_DETECTED);
    }
    if (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_OUTPUTRDY_Msk) {
        tusb_hal_nrf_power_event(USB_EVT_READY);
    }

    NVIC_EnableIRQ(POWER_CLOCK_IRQn);

    return 0;
}

void isr_power_clock(void)
{
    if (NRF_POWER->EVENTS_USBDETECTED) {
        NRF_POWER->EVENTS_USBDETECTED = 0;
        tusb_hal_nrf_power_event(USB_EVT_DETECTED);
    }
    if (NRF_POWER->EVENTS_USBREMOVED) {
        NRF_POWER->EVENTS_USBREMOVED = 0;
        tusb_hal_nrf_power_event(USB_EVT_REMOVED);
    }
    if (NRF_POWER->EVENTS_USBPWRRDY) {
        NRF_POWER->EVENTS_USBPWRRDY = 0;
        tusb_hal_nrf_power_event(USB_EVT_READY);
    }
    cortexm_isr_end();
}

void isr_usbd(void)
{
    /* call device interrupt handler with the first device */
    if (IS_USED(MODULE_TINYUSB_DEVICE)) {
        tud_int_handler(0);
    }

    /* call host interrupt handler with the first device */
    if (IS_USED(MODULE_TINYUSB_HOST)) {
        tuh_int_handler(0);
    }

    cortexm_isr_end();
}
