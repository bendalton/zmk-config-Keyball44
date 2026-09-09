/*
 * Link-time shims that let the PMW3610 driver build on a split PERIPHERAL.
 *
 * The kumamuk-git/zmk-pmw3610-driver we pin is central-only by construction:
 * get_input_mode_for_current_layer() opens with zmk_keymap_highest_layer_active()
 * unconditionally, and the ball-action path calls zmk_behavior_queue_add() --
 * both of which live in ZMK sources that app/CMakeLists.txt compiles only for
 * `(NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL`. In the dongle
 * setup the sensor sits on the right half, which is now a peripheral, so the
 * right build fails to LINK -- not to compile. Upstream has no fix (the pin is
 * the repo tip) and the only other fork of this driver has the same coupling.
 *
 * These stubs are pure link fodder, never semantically live:
 *   - scroll-layers / snipe-layers / ball-actions were all removed from the
 *     sensor node in keyball44_right.overlay, so every layer list the driver
 *     walks is empty. Reporting "layer 0" makes it take the plain MOVE path,
 *     which is the only path we want here: scroll and snipe are re-created on
 *     the dongle as layer-gated input-processors.
 *   - the ball-action queue call sits behind an empty ball_actions list and is
 *     unreachable; it returns -ENOTSUP so a future misconfiguration fails loud
 *     rather than silently half-working.
 *
 * Declared __weak so that if ZMK ever does compile the real keymap/behaviour
 * queue into a peripheral build, its strong definitions simply win.
 */

#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>
#include <errno.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>

__weak zmk_keymap_layer_index_t zmk_keymap_highest_layer_active(void) {
    return 0;
}

__weak int zmk_behavior_queue_add(const struct zmk_behavior_binding_event *event,
                                  const struct zmk_behavior_binding behavior, bool press,
                                  uint32_t wait) {
    ARG_UNUSED(event);
    ARG_UNUSED(behavior);
    ARG_UNUSED(press);
    ARG_UNUSED(wait);
    return -ENOTSUP;
}
