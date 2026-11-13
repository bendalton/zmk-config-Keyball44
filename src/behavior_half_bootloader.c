/*
 * &half_boot POS: reboot the HALF that owns key position POS into its UF2
 * bootloader, even when triggered from a combo.
 *
 * Why this exists: the keymap runs on the dongle. A plain &bootloader on a
 * regular key works (it is EVENT_SOURCE locality, so it runs on the half that
 * sent the key), but combos always invoke their binding with source = LOCAL
 * (the dongle), so &bootloader in a combo would reboot the DONGLE.
 *
 * Fix: a listener remembers, per key position, which split peripheral (slot
 * index) last sent it. On press we forward ZMK's stock &bootloader to that
 * peripheral over the split link. Slot indexes depend on BLE connection order,
 * so they are learned, never hard-coded. If POS hasn't been seen since the
 * dongle booted, nothing happens (type a key on that half first).
 */

#define DT_DRV_COMPAT zmk_behavior_half_bootloader

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/split/central.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define MAX_POSITIONS 64
#define UNKNOWN ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL

static uint8_t last_source[MAX_POSITIONS];

static int half_boot_init(const struct device *dev) {
    memset(last_source, UNKNOWN, sizeof(last_source));
    return 0;
}

static int half_boot_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL &&
        ev->position < MAX_POSITIONS) {
        last_source[ev->position] = ev->source;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(half_boot, half_boot_listener);
ZMK_SUBSCRIPTION(half_boot, zmk_position_state_changed);

static int on_half_boot_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    uint32_t pos = binding->param1;
    if (pos >= MAX_POSITIONS || last_source[pos] == UNKNOWN) {
        LOG_WRN("half_boot: no peripheral seen yet for position %d", pos);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    struct zmk_behavior_binding boot = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(bootloader)),
    };
    event.source = last_source[pos];
    LOG_INF("half_boot: bootloader -> peripheral %d (pos %d)", event.source, pos);
    zmk_split_central_invoke_behavior(event.source, &boot, event, true);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_half_boot_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata pos_values[] = {
    {
        .display_name = "Key position",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range = {.min = 0, .max = MAX_POSITIONS - 1},
    },
};
static const struct behavior_parameter_metadata_set pos_set = {
    .param1_values = pos_values,
    .param1_values_len = ARRAY_SIZE(pos_values),
};
static const struct behavior_parameter_metadata half_boot_metadata = {
    .sets_len = 1,
    .sets = &pos_set,
};
#endif

static const struct behavior_driver_api half_boot_driver_api = {
    .binding_pressed = on_half_boot_pressed,
    .binding_released = on_half_boot_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &half_boot_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, half_boot_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &half_boot_driver_api);

#endif
