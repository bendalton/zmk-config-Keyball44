/*
 * RAM-only, record-at-runtime macro ("password macro").
 *
 *   &pwd PWD_REC   start recording. Type normally -- keys still go to the host,
 *                  so you can type straight into a real password prompt. The
 *                  first Enter or Escape stops recording; that terminating key
 *                  is sent to the host but NOT stored.
 *   &pwd PWD_PLAY  replay the stored keystrokes (no trailing Enter).
 *   &pwd PWD_CLR   wipe the stored keystrokes.
 *
 * Security model: the recording lives only in a static RAM buffer on the
 * central (the dongle, or the right half on a non-dongle build). It is never
 * written to flash/settings, never sent over split BLE, and is gone on any
 * reset, re-flash or power loss. It is NOT protected from anyone who has the
 * keyboard while it is powered: they can press PLAY.
 *
 * Recording works on resolved keycodes (zmk_keycode_state_changed), so layers,
 * hold-taps, combos, mod-morphs and shift all record correctly. Each event is
 * stored as press/release with its implicit mods and replayed through &kp via
 * the behavior queue, exactly like a normal ZMK macro.
 *
 * Guards:
 *   - idle-timeout-ms: if you start recording and walk away, recording aborts
 *     (and the buffer is wiped) after this long with no keystrokes.
 *   - max-events: buffer full aborts and wipes rather than keeping a
 *     truncated password.
 *   - pressing PWD_REC again while recording aborts and wipes.
 */

#define DT_DRV_COMPAT zmk_behavior_ram_macro

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define RAM_MACRO_RECORD 0
#define RAM_MACRO_PLAY 1
#define RAM_MACRO_CLEAR 2

/* `encoded` is exactly what &kp takes as its parameter:
 * mods << 24 | page << 16 | id. */
struct ram_macro_event {
    uint32_t encoded;
    bool pressed;
};

struct ram_macro_config {
    uint32_t wait_ms;
    uint32_t idle_timeout_ms;
    size_t max_events;
};

struct ram_macro_data {
    const struct device *dev;
    struct ram_macro_event *events;
    size_t count;
    bool recording;
    struct k_work_delayable idle_work;
};

#define KP_DEV DEVICE_DT_NAME(DT_NODELABEL(kp))

static void wipe(struct ram_macro_data *data, size_t max_events) {
    memset(data->events, 0, sizeof(struct ram_macro_event) * max_events);
    data->count = 0;
}

static void abort_recording(const struct device *dev, const char *why) {
    struct ram_macro_data *data = dev->data;
    const struct ram_macro_config *cfg = dev->config;

    k_work_cancel_delayable(&data->idle_work);
    data->recording = false;
    wipe(data, cfg->max_events);
    LOG_INF("ram macro: recording aborted (%s), buffer wiped", why);
}

static void idle_timeout_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct ram_macro_data *data = CONTAINER_OF(dwork, struct ram_macro_data, idle_work);

    if (data->recording) {
        abort_recording(data->dev, "idle timeout");
    }
}

static inline uint32_t usage_of(uint32_t encoded) { return encoded & 0x00FFFFFF; }

/* Index of the most recent press of `usage` that has no matching release yet,
 * or -1. */
static int open_press_index(const struct ram_macro_data *data, uint32_t usage) {
    int depth = 0;
    for (int i = (int)data->count - 1; i >= 0; i--) {
        if (usage_of(data->events[i].encoded) != usage) {
            continue;
        }
        if (!data->events[i].pressed) {
            depth++;
        } else if (depth == 0) {
            return i;
        } else {
            depth--;
        }
    }
    return -1;
}

/* Close out any keys still held when recording stopped (e.g. Shift held
 * while hitting Enter) so playback never leaves a key stuck down. */
static void close_open_presses(const struct device *dev) {
    struct ram_macro_data *data = dev->data;
    const struct ram_macro_config *cfg = dev->config;
    size_t original = data->count;

    for (size_t i = 0; i < original; i++) {
        if (!data->events[i].pressed) {
            continue;
        }
        uint32_t usage = usage_of(data->events[i].encoded);
        if (open_press_index(data, usage) == (int)i && data->count < cfg->max_events) {
            data->events[data->count++] =
                (struct ram_macro_event){.encoded = data->events[i].encoded, .pressed = false};
        }
    }
}

static bool is_terminator(const struct zmk_keycode_state_changed *ev) {
    return ev->usage_page == HID_USAGE_KEY &&
           (ev->keycode == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER ||
            ev->keycode == HID_USAGE_KEY_KEYPAD_ENTER ||
            ev->keycode == HID_USAGE_KEY_KEYBOARD_ESCAPE);
}

static void record_event(const struct device *dev, const struct zmk_keycode_state_changed *ev) {
    struct ram_macro_data *data = dev->data;
    const struct ram_macro_config *cfg = dev->config;

    uint8_t mods =
        is_mod(ev->usage_page, ev->keycode) ? ev->explicit_modifiers : ev->implicit_modifiers;
    uint32_t encoded = APPLY_MODS((uint32_t)mods, ZMK_HID_USAGE(ev->usage_page, ev->keycode));

    /* A release for a key that was already down before recording started
     * would replay as a stray release -- drop it. */
    if (!ev->state && open_press_index(data, usage_of(encoded)) < 0) {
        return;
    }

    if (data->count >= cfg->max_events) {
        abort_recording(dev, "buffer full");
        return;
    }

    data->events[data->count++] = (struct ram_macro_event){.encoded = encoded, .pressed = ev->state};
    k_work_reschedule(&data->idle_work, K_MSEC(cfg->idle_timeout_ms));
}

static int start_recording(const struct device *dev) {
    struct ram_macro_data *data = dev->data;
    const struct ram_macro_config *cfg = dev->config;

    if (data->recording) {
        abort_recording(dev, "record pressed again");
        return 0;
    }

    wipe(data, cfg->max_events);
    data->recording = true;
    k_work_reschedule(&data->idle_work, K_MSEC(cfg->idle_timeout_ms));
    LOG_INF("ram macro: recording");
    return 0;
}

static int play(const struct device *dev, struct zmk_behavior_binding_event event) {
    struct ram_macro_data *data = dev->data;
    const struct ram_macro_config *cfg = dev->config;

    if (data->recording || data->count == 0) {
        return 0;
    }

    for (size_t i = 0; i < data->count; i++) {
        struct zmk_behavior_binding binding = {
            .behavior_dev = KP_DEV,
            .param1 = data->events[i].encoded,
        };
        int ret = zmk_behavior_queue_add(&event, binding, data->events[i].pressed, cfg->wait_ms);
        if (ret < 0) {
            LOG_ERR("ram macro: behavior queue full at event %d (%d)", (int)i, ret);
            return ret;
        }
    }
    return 0;
}

static int on_ram_macro_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct ram_macro_data *data = dev->data;
    const struct ram_macro_config *cfg = dev->config;

    switch (binding->param1) {
    case RAM_MACRO_RECORD:
        return start_recording(dev);
    case RAM_MACRO_PLAY:
        return play(dev, event);
    case RAM_MACRO_CLEAR:
        k_work_cancel_delayable(&data->idle_work);
        data->recording = false;
        wipe(data, cfg->max_events);
        LOG_INF("ram macro: cleared");
        return 0;
    default:
        return -ENOTSUP;
    }
}

static int on_ram_macro_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "Record",
     .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
     .value = RAM_MACRO_RECORD},
    {.display_name = "Play", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = RAM_MACRO_PLAY},
    {.display_name = "Clear",
     .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
     .value = RAM_MACRO_CLEAR},
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};
#endif

static const struct behavior_driver_api ram_macro_driver_api = {
    .binding_pressed = on_ram_macro_pressed,
    .binding_released = on_ram_macro_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

/* Observe-only: always bubbles, so keys reach the host whether or not this
 * listener runs before the HID listener. */
static int ram_macro_keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (size_t i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];
        struct ram_macro_data *data = dev->data;
        if (!data->recording) {
            continue;
        }

        if (ev->state && is_terminator(ev)) {
            k_work_cancel_delayable(&data->idle_work);
            close_open_presses(dev);
            data->recording = false;
            LOG_INF("ram macro: saved %d events", (int)data->count);
            continue;
        }

        record_event(dev, ev);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_ram_macro, ram_macro_keycode_listener);
ZMK_SUBSCRIPTION(behavior_ram_macro, zmk_keycode_state_changed);

static int ram_macro_init(const struct device *dev) {
    struct ram_macro_data *data = dev->data;
    data->dev = dev;
    k_work_init_delayable(&data->idle_work, idle_timeout_handler);
    return 0;
}

#define RAM_MACRO_INST(n)                                                                          \
    static struct ram_macro_event ram_macro_events_##n[DT_INST_PROP(n, max_events)];               \
    static struct ram_macro_data ram_macro_data_##n = {.events = ram_macro_events_##n};            \
    static const struct ram_macro_config ram_macro_config_##n = {                                  \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                       \
        .idle_timeout_ms = DT_INST_PROP(n, idle_timeout_ms),                                       \
        .max_events = DT_INST_PROP(n, max_events),                                                 \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, ram_macro_init, NULL, &ram_macro_data_##n, &ram_macro_config_##n,   \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &ram_macro_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RAM_MACRO_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
