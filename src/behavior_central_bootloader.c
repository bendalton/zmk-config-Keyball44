/*
 * &dongle_boot: reboot the CENTRAL (the dongle) into the UF2 bootloader.
 *
 * ZMK's built-in &bootloader has BEHAVIOR_LOCALITY_EVENT_SOURCE, so it always
 * reboots the half whose key was pressed. The dongle has no keys, so this is a
 * copy of ZMK's reset behavior with CENTRAL locality instead -- it runs on the
 * dongle no matter which half the key is on.
 */

#define DT_DRV_COMPAT zmk_behavior_central_reset

#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct central_reset_config {
    int type;
};

static int on_central_reset_pressed(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct central_reset_config *cfg = dev->config;

    sys_reboot(cfg->type);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api central_reset_driver_api = {
    .binding_pressed = on_central_reset_pressed,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define CENTRAL_RST_INST(n)                                                                        \
    static const struct central_reset_config central_reset_config_##n = {                          \
        .type = DT_INST_PROP(n, type)};                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &central_reset_config_##n, POST_KERNEL,           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &central_reset_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CENTRAL_RST_INST)

#endif
