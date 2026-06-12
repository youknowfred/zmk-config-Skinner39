/*
 * Keysmith status-report channel (TB-7) — device→host telemetry over the
 * raw-HID Glance link (vendor/zmk-raw-hid, HID-over-GATT).
 *
 * Wire schema (32-byte input reports; tags clear of the LOCKED host→device
 * Glance schema 0xB0–0xB5, the zzeneg samples 0xAA–0xAF and the relay range
 * 0xCC–0xCD — full spec in docs/2026-06-12-status-report-channel.md):
 *
 *   0xB8 LAYER    [0]=tag [1]=ver=1 [2]=seq [3]=highest active layer index
 *                 [4..7]=layer state bitmask, LE u32 [8]=reason (1=change,
 *                 0=heartbeat) [9..31]=0
 *   0xB9 BATTERY  [0]=tag [1]=ver=1 [2]=seq [3]=central SoC % (0xFF unknown)
 *                 [4]=central on USB power (0/1) [5]=peripheral count (1)
 *                 [6]=peripheral SoC % (0xFF = never reported; ZMK relays 0
 *                 on peripheral disconnect) [7]=reason [8..31]=0
 *
 * Emission: on change, coalesced through one work item per frame type (event
 * bursts collapse to the latest state — a momentary-layer flap costs at most
 * a few frames, never a queue), plus heartbeats (layer 15 s, battery 60 s) so
 * a freshly attached host gets state without a GET round-trip. Nothing sends
 * unless the active profile is connected AND the host has enabled
 * notifications on the raw-HID input report — silence is the idle state.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#endif

#include <raw_hid/events.h>
#include <raw_hid/raw_hid.h>

#define KS_TAG_LAYER 0xB8
#define KS_TAG_BATTERY 0xB9
#define KS_SCHEMA_VER 0x01

#define KS_REASON_HEARTBEAT 0x00
#define KS_REASON_CHANGE 0x01

#define KS_LAYER_HEARTBEAT_S 15
#define KS_BATTERY_HEARTBEAT_S 60

#define KS_SOC_UNKNOWN 0xFF

/* Last peripheral level relayed by the central (BATTERY_LEVEL_FETCHING).
 * Single u8, written from listener context, read from the system workqueue —
 * torn access is impossible and a stale-by-one heartbeat is harmless. */
static uint8_t periph_soc = KS_SOC_UNKNOWN;

static bool channel_up(void) {
    return zmk_ble_active_profile_is_connected() && raw_hid_ble_subscribed();
}

/* Frames live in static storage: hog.c copies them into its own 32-byte
 * buffer synchronously inside the raise, and every sender below runs on the
 * system workqueue, so reuse is single-threaded. */

static void send_layer_frame(uint8_t reason) {
    static uint8_t seq;
    static uint8_t frame[CONFIG_RAW_HID_REPORT_SIZE];

    if (!channel_up()) {
        return;
    }

    memset(frame, 0, sizeof(frame));
    frame[0] = KS_TAG_LAYER;
    frame[1] = KS_SCHEMA_VER;
    frame[2] = seq++;
    frame[3] = (uint8_t)zmk_keymap_highest_layer_active();
    sys_put_le32((uint32_t)zmk_keymap_layer_state(), &frame[4]);
    frame[8] = reason;

    raise_raw_hid_sent_event(
        (struct raw_hid_sent_event){.data = frame, .length = sizeof(frame)});
}

static void send_battery_frame(uint8_t reason) {
    static uint8_t seq;
    static uint8_t frame[CONFIG_RAW_HID_REPORT_SIZE];

    if (!channel_up()) {
        return;
    }

    memset(frame, 0, sizeof(frame));
    frame[0] = KS_TAG_BATTERY;
    frame[1] = KS_SCHEMA_VER;
    frame[2] = seq++;
    frame[3] = zmk_battery_state_of_charge();
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    frame[4] = zmk_usb_is_powered() ? 1 : 0;
#endif
    frame[5] = 1;
    frame[6] = periph_soc;
    frame[7] = reason;

    raise_raw_hid_sent_event(
        (struct raw_hid_sent_event){.data = frame, .length = sizeof(frame)});
}

static void layer_change_work_cb(struct k_work *work) { send_layer_frame(KS_REASON_CHANGE); }
K_WORK_DEFINE(ks_layer_change_work, layer_change_work_cb);

static void battery_change_work_cb(struct k_work *work) { send_battery_frame(KS_REASON_CHANGE); }
K_WORK_DEFINE(ks_battery_change_work, battery_change_work_cb);

static void layer_heartbeat_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(ks_layer_heartbeat, layer_heartbeat_cb);
static void layer_heartbeat_cb(struct k_work *work) {
    send_layer_frame(KS_REASON_HEARTBEAT);
    k_work_schedule(&ks_layer_heartbeat, K_SECONDS(KS_LAYER_HEARTBEAT_S));
}

static void battery_heartbeat_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(ks_battery_heartbeat, battery_heartbeat_cb);
static void battery_heartbeat_cb(struct k_work *work) {
    send_battery_frame(KS_REASON_HEARTBEAT);
    k_work_schedule(&ks_battery_heartbeat, K_SECONDS(KS_BATTERY_HEARTBEAT_S));
}

static int layer_listener_cb(const zmk_event_t *eh) {
    k_work_submit(&ks_layer_change_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(keysmith_status_layer, layer_listener_cb);
ZMK_SUBSCRIPTION(keysmith_status_layer, zmk_layer_state_changed);

static int battery_listener_cb(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    const struct zmk_peripheral_battery_state_changed *pev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (pev != NULL) {
        periph_soc = pev->state_of_charge;
    }
#endif
    k_work_submit(&ks_battery_change_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(keysmith_status_battery, battery_listener_cb);
ZMK_SUBSCRIPTION(keysmith_status_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(keysmith_status_battery, zmk_peripheral_battery_state_changed);
#endif

/* Artifact strings-probe marker. LOG_* strings vanish on the Studio-snippet
 * builds (CONFIG_LOG=n), so the probe gate needs a plain .rodata tag — the
 * asm reference below anchors it against --gc-sections via the (kept)
 * SYS_INIT text. */
static const char ks_status_fw_tag[] =
    "keysmith-status-reports v1: layer 0xB8 / battery 0xB9";

static int keysmith_status_init(void) {
    __asm__ volatile("" ::"r"(ks_status_fw_tag));
    /* Boot confirmation on logging-enabled builds only. */
    LOG_INF("keysmith status reports armed: layer 0xB8 @ %ds, battery 0xB9 @ %ds",
            KS_LAYER_HEARTBEAT_S, KS_BATTERY_HEARTBEAT_S);
    k_work_schedule(&ks_layer_heartbeat, K_SECONDS(KS_LAYER_HEARTBEAT_S));
    k_work_schedule(&ks_battery_heartbeat, K_SECONDS(KS_BATTERY_HEARTBEAT_S));
    return 0;
}

SYS_INIT(keysmith_status_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
