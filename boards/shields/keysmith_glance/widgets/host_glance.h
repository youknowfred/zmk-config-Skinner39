/*
 * Keysmith Glance — host-pushed scalars over raw HID (P0-6 / keysmith doc 03 §5).
 *
 * Wire schema mirrors keysmith-core/src/glance.rs EXACTLY (locked + tested there):
 * 32-byte report, data[0] = tag (no Report ID item in the descriptor, so no ID
 * byte on the wire), strings length-prefixed ASCII hard-capped at 14. Tag values
 * dodge the zzeneg per-OS range (0xAA–0xAF) and the relay range (0xCC–0xCD).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

#define GLANCE_STR_CAP 14

enum glance_tag {
    GLANCE_WPM = 0xB0,       /* [0xB0][wpm] */
    GLANCE_COMFORT = 0xB1,   /* [0xB1][score 0..100] */
    GLANCE_APP = 0xB2,       /* [0xB2][len][ascii ≤14] — len 0 clears */
    GLANCE_TIP = 0xB3,       /* [0xB3][len][ascii ≤14] — len 0 clears */
    GLANCE_PAGE = 0xB4,      /* [0xB4][page_idx] */
    GLANCE_HEARTBEAT = 0xB5, /* [0xB5] — liveness only, every 30 s when idle */
};

/* Snapshot handed to the display widget; copied out under the store's lock. */
struct host_glance_view {
    bool live; /* some frame (heartbeats count) arrived within the last 65 s */
    uint8_t wpm;
    uint8_t comfort;
    uint8_t page;
    char app[GLANCE_STR_CAP + 1];
    char tip[GLANCE_STR_CAP + 1];
};

struct host_glance_view keysmith_host_glance_snapshot(void);

/* Raised after every accepted frame and on the 65 s staleness timeout. */
struct keysmith_glance_state_changed {
    bool live;
};
ZMK_EVENT_DECLARE(keysmith_glance_state_changed);
