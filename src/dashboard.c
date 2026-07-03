/*
 * Peripheral (LEFT) half portrait dashboard.
 *
 * STAGE 1 (de-risk): verify the module links, that CONFIG_ZMK_SPLIT_ROLE_CENTRAL
 * branching gives black-on-right/this-on-left, and — the big unknown — that
 * portrait rotation actually renders correctly on the nice!view (ls0xx is
 * physically 160x68 landscape; the panel is mounted rotated, so we want a
 * 68-wide x 160-tall logical space). We rotate the whole display 90 deg and
 * drop a few probe elements to eyeball orientation before building the real
 * layout in Stage 2.
 */
#include <zmk/display/status_screen.h>
#include <zmk/battery.h>
#include <lvgl.h>
#include <stdio.h>

lv_obj_t *zmk_display_status_screen(void) {
    /* Rotate to portrait: logical canvas becomes 68 (w) x 160 (h). */
    lv_disp_set_rotation(lv_disp_get_default(), LV_DISP_ROT_90);

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* "L" at the top so we can confirm top-of-portrait is where we expect. */
    lv_obj_t *top = lv_label_create(screen);
    lv_label_set_text(top, "L");
    lv_obj_set_style_text_color(top, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 4);

    /* Battery % (static read at boot for now; listeners come in Stage 2). */
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", zmk_battery_state_of_charge());
    lv_obj_t *batt = lv_label_create(screen);
    lv_label_set_text(batt, buf);
    lv_obj_set_style_text_color(batt, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(batt, LV_ALIGN_TOP_MID, 0, 28);

    /* A framed box in the middle: confirms the 68x160 aspect looks portrait. */
    lv_obj_t *box = lv_obj_create(screen);
    lv_obj_set_size(box, 44, 60);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 2, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 10);

    /* "BOT" marker near the bottom edge. */
    lv_obj_t *bot = lv_label_create(screen);
    lv_label_set_text(bot, "BOT");
    lv_obj_set_style_text_color(bot, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, -4);

    return screen;
}
