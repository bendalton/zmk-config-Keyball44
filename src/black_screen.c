/*
 * Central (RIGHT) half status screen: solid black, drawn once, never redrawn.
 * The right nice!view has a hardware fault that corrupts frames on activity;
 * with no widgets and no event listeners, LVGL invalidates the screen exactly
 * once (one SPI flush at boot) and never writes again, so there is nothing for
 * the glitch to corrupt.
 */
#include <zmk/display/status_screen.h>
#include <lvgl.h>

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}
