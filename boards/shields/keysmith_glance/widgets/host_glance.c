/*
 * Keysmith Glance host store — the raw-HID receive side (P0-6, keysmith doc 03 §5.3).
 *
 * The listener COPIES the payload out of e->data under a mutex before returning:
 * e->data points into a transient buffer (the USB SET_REPORT setup buffer or the
 * BLE GATT write buffer) that is only valid during the synchronous event
 * dispatch. The display work queue later reads the same store through
 * keysmith_host_glance_snapshot() under the same lock.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <raw_hid/events.h>

#include "host_glance.h"

ZMK_EVENT_IMPL(keysmith_glance_state_changed);

static struct host_glance_view g_state;
static K_MUTEX_DEFINE(keysmith_glance_lock);

/* 65 s without any frame ⇒ host gone (the nvhid.c disconnect-timer pattern).
 * The host keeps it alive with a 30 s heartbeat whenever otherwise idle. */
#define GLANCE_STALE_TIMEOUT K_SECONDS(65)

static void glance_stale_cb(struct k_work *work) {
    ARG_UNUSED(work);
    k_mutex_lock(&keysmith_glance_lock, K_FOREVER);
    g_state.live = false;
    k_mutex_unlock(&keysmith_glance_lock);
    raise_keysmith_glance_state_changed((struct keysmith_glance_state_changed){.live = false});
}
static K_WORK_DELAYABLE_DEFINE(keysmith_glance_stale_work, glance_stale_cb);

struct host_glance_view keysmith_host_glance_snapshot(void) {
    k_mutex_lock(&keysmith_glance_lock, K_FOREVER);
    struct host_glance_view copy = g_state;
    k_mutex_unlock(&keysmith_glance_lock);
    return copy;
}

/* [tag][len][bytes…] — clamp to the cap AND to what actually arrived. */
static void copy_str(char *dst, const struct raw_hid_received_event *e) {
    uint8_t n = e->data[1];
    if (n > GLANCE_STR_CAP) {
        n = GLANCE_STR_CAP;
    }
    if (n > e->length - 2) {
        n = e->length - 2;
    }
    memcpy(dst, &e->data[2], n);
    dst[n] = '\0';
}

static int on_raw_hid(const zmk_event_t *eh) {
    const struct raw_hid_received_event *e = as_raw_hid_received_event(eh);
    if (!e || e->length < 1) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    k_mutex_lock(&keysmith_glance_lock, K_FOREVER);
    switch (e->data[0]) {
    case GLANCE_WPM:
        if (e->length >= 2) {
            g_state.wpm = e->data[1];
        }
        break;
    case GLANCE_COMFORT:
        if (e->length >= 2) {
            g_state.comfort = MIN(e->data[1], 100);
        }
        break;
    case GLANCE_APP:
        if (e->length >= 2) {
            copy_str(g_state.app, e);
        }
        break;
    case GLANCE_TIP:
        if (e->length >= 2) {
            copy_str(g_state.tip, e);
        }
        break;
    case GLANCE_PAGE:
        if (e->length >= 2) {
            g_state.page = e->data[1];
        }
        break;
    case GLANCE_HEARTBEAT:
        break; /* liveness only */
    default:
        /* Not a glance tag (zzeneg 0xAA–0xAF, relay 0xCC–0xCD, future) — not ours. */
        k_mutex_unlock(&keysmith_glance_lock);
        return ZMK_EV_EVENT_BUBBLE;
    }
    g_state.live = true;
    k_mutex_unlock(&keysmith_glance_lock);

    k_work_reschedule(&keysmith_glance_stale_work, GLANCE_STALE_TIMEOUT);
    raise_keysmith_glance_state_changed((struct keysmith_glance_state_changed){.live = true});
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(keysmith_glance_host, on_raw_hid);
ZMK_SUBSCRIPTION(keysmith_glance_host, raw_hid_received_event);
