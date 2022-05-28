/**
 * From pico-sdk stdio_usb
 *
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 * Modifications (c) 2022 Brian Starkey <stark3y@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tusb.h"

#include "pico/time.h"
#include "pico/stdio/driver.h"
#include "pico/binary_info.h"
#include "pico/mutex.h"
#include "hardware/irq.h"

#define UB_STDIO_USB_LOW_PRIORITY_IRQ 31
#define UB_STDIO_USB_TASK_INTERVAL_US 1000
#define UB_STDIO_USB_STDOUT_TIMEOUT_US 500000

alarm_pool_t *alarm_pool;

static_assert(UB_STDIO_USB_LOW_PRIORITY_IRQ > RTC_IRQ, ""); // note RTC_IRQ is currently the last one
static mutex_t ub_stdio_usb_mutex;

static void low_priority_worker_irq(void) {
    // if the mutex is already owned, then we are in user code
    // in this file which will do a tud_task itself, so we'll just do nothing
    // until the next tick; we won't starve
    if (mutex_try_enter(&ub_stdio_usb_mutex, NULL)) {
        tud_task();
        mutex_exit(&ub_stdio_usb_mutex);
    }
}

static int64_t timer_task(__unused alarm_id_t id, __unused void *user_data) {
    irq_set_pending(UB_STDIO_USB_LOW_PRIORITY_IRQ);
    return UB_STDIO_USB_TASK_INTERVAL_US;
}

static void ub_stdio_usb_out_chars(const char *buf, int length) {
    static uint64_t last_avail_time;
    uint32_t owner;
    if (!mutex_try_enter(&ub_stdio_usb_mutex, &owner)) {
        if (owner == get_core_num()) return; // would deadlock otherwise
        mutex_enter_blocking(&ub_stdio_usb_mutex);
    }
    if (tud_cdc_connected()) {
        for (int i = 0; i < length;) {
            int n = length - i;
            int avail = (int) tud_cdc_write_available();
            if (n > avail) n = avail;
            if (n) {
                int n2 = (int) tud_cdc_write(buf + i, (uint32_t)n);
                tud_task();
                tud_cdc_write_flush();
                i += n2;
                last_avail_time = time_us_64();
            } else {
                tud_task();
                tud_cdc_write_flush();
                if (!tud_cdc_connected() ||
                    (!tud_cdc_write_available() && time_us_64() > last_avail_time + UB_STDIO_USB_STDOUT_TIMEOUT_US)) {
                    break;
                }
            }
        }
    } else {
        // reset our timeout
        last_avail_time = 0;
    }
    mutex_exit(&ub_stdio_usb_mutex);
}

int ub_stdio_usb_in_chars(char *buf, int length) {
    uint32_t owner;
    if (!mutex_try_enter(&ub_stdio_usb_mutex, &owner)) {
        if (owner == get_core_num()) return PICO_ERROR_NO_DATA; // would deadlock otherwise
        mutex_enter_blocking(&ub_stdio_usb_mutex);
    }
    int rc = PICO_ERROR_NO_DATA;
    if (tud_cdc_connected() && tud_cdc_available()) {
        int count = (int) tud_cdc_read(buf, (uint32_t) length);
        rc =  count ? count : PICO_ERROR_NO_DATA;
    }
    mutex_exit(&ub_stdio_usb_mutex);
    return rc;
}

stdio_driver_t ub_stdio_usb = {
    .out_chars = ub_stdio_usb_out_chars,
    .in_chars = ub_stdio_usb_in_chars,
    .crlf_enabled = true,
};

bool ub_stdio_usb_connected(void) {
    return tud_cdc_connected();
}

bool ub_stdio_usb_init(void) {
    irq_set_exclusive_handler(UB_STDIO_USB_LOW_PRIORITY_IRQ, low_priority_worker_irq);
    irq_set_enabled(UB_STDIO_USB_LOW_PRIORITY_IRQ, true);

    alarm_pool = alarm_pool_create(2, 4);

    mutex_init(&ub_stdio_usb_mutex);
    bool rc = alarm_pool_add_alarm_in_us(alarm_pool, UB_STDIO_USB_TASK_INTERVAL_US, timer_task, NULL, true);
    // TODO: Do we want to wait here for a connection? Probably not
    if (rc) {
        stdio_set_driver_enabled(&ub_stdio_usb, true);
	/*
        absolute_time_t until = at_the_end_of_time;
        do {
            if (ub_stdio_usb_connected()) {
                sleep_ms(50);
                break;
            }
            sleep_ms(10);
        } while (!time_reached(until));
	*/
    }
    return rc;
}
