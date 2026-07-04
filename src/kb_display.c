/*
 * Keyball44 dual nice!view dashboard.
 *   central  (RIGHT) -> primary: battery, endpoint (USB or BT profile+host), Orbit
 *   peripheral (LEFT) -> companion: battery, split link, big Orbit
 *
 * The nice!view is physically 160x68 landscape but mounted portrait, so we
 * rotate the display 90 deg and lay everything out in a 68 (w) x 160 (h) space.
 * v1 reads state once at boot (no live listeners yet) to confirm rotation +
 * layout; live updates come next.
 */
#include <zmk/display/status_screen.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>

#include <zmk/battery.h>

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#if defined(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#endif
#else
#include <zmk/split/bluetooth/peripheral.h>
#endif

#define INK   lv_color_black()
#define GND   lv_color_white()

/* ---- Orbit: lit, dithered sphere on a canvas ---- */
#define ORB 58
static lv_color_t orbit_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(ORB, ORB)];
static const uint8_t BAY[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};

static void draw_orbit(lv_obj_t *parent, lv_coord_t y_off) {
    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, orbit_buf, ORB, ORB, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(cv, GND, LV_OPA_COVER);

    const float cx = ORB / 2.0f, cy = ORB / 2.0f, ballr = 20.0f;
    const float lx = -0.5f, ly = -0.62f, lz = 0.6f;
    const float llen = sqrtf(lx*lx + ly*ly + lz*lz);
    for (int y = 0; y < ORB; y++) {
        for (int x = 0; x < ORB; x++) {
            float dx = (x - cx) / ballr, dy = (y - cy) / ballr, d2 = dx*dx + dy*dy;
            if (d2 > 1.0f) continue;
            float dz = sqrtf(1.0f - d2);
            float b = (dx*lx + dy*ly + dz*lz) / llen;
            b = (b < 0 ? 0 : b) * 0.92f + 0.06f;
            float th = (BAY[y & 3][x & 3] + 0.5f) / 16.0f;
            if (b < th) lv_canvas_set_px_color(cv, x, y, INK);
        }
    }
    /* rim + a cursor dot with a bright center, on an orbit ring */
    for (int a = 0; a < 360; a += 3) {
        float ang = a * (float)M_PI / 180.0f;
        int rx = (int)lroundf(cx + cosf(ang) * ballr);
        int ry = (int)lroundf(cy + sinf(ang) * ballr);
        if (rx >= 0 && rx < ORB && ry >= 0 && ry < ORB) lv_canvas_set_px_color(cv, rx, ry, INK);
    }
    float ca = -0.7f, orbr = 27.0f;
    int ox = (int)lroundf(cx + cosf(ca) * orbr), oy = (int)lroundf(cy + sinf(ca) * orbr * 0.5f);
    for (int yy = -2; yy <= 2; yy++) for (int xx = -2; xx <= 2; xx++)
        if (xx*xx + yy*yy <= 6 && ox+xx>=0 && ox+xx<ORB && oy+yy>=0 && oy+yy<ORB)
            lv_canvas_set_px_color(cv, ox+xx, oy+yy, INK);
    if (ox>=0 && ox<ORB && oy>=0 && oy<ORB) lv_canvas_set_px_color(cv, ox, oy, GND);

    lv_obj_align(cv, LV_ALIGN_TOP_MID, 0, y_off);
}

/* ---- battery: outline bar + nub + % label ---- */
static void draw_battery(lv_obj_t *p, lv_coord_t y, uint8_t pct) {
    lv_obj_t *body = lv_obj_create(p);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 46, 15);
    lv_obj_align(body, LV_ALIGN_TOP_MID, -1, y);
    lv_obj_set_style_border_color(body, INK, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_radius(body, 2, 0);

    lv_obj_t *fill = lv_obj_create(body);
    lv_obj_remove_style_all(fill);
    int fw = (42 * pct) / 100; if (fw < 0) fw = 0;
    lv_obj_set_size(fill, fw, 11);
    lv_obj_align(fill, LV_ALIGN_LEFT_MID, 1, 0);
    lv_obj_set_style_bg_color(fill, INK, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);

    lv_obj_t *nub = lv_obj_create(p);
    lv_obj_remove_style_all(nub);
    lv_obj_set_size(nub, 2, 6);
    lv_obj_align_to(nub, body, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(nub, INK, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, INK, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y + 20);
}

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, lv_coord_t y) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, INK, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static void hline(lv_obj_t *p, lv_coord_t y) {
    lv_obj_t *ln = lv_obj_create(p);
    lv_obj_remove_style_all(ln);
    lv_obj_set_size(ln, 52, 1);
    lv_obj_align(ln, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(ln, INK, 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
}

static lv_obj_t *base_screen(void) {
    lv_disp_set_rotation(lv_disp_get_default(), LV_DISP_ROT_90);
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, 68, 160);
    lv_obj_set_style_bg_color(s, GND, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *s = base_screen();

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    /* PRIMARY (right/central) */
    draw_battery(s, 6, zmk_battery_state_of_charge());
    hline(s, 44);

    bool on_usb = false;
#if defined(CONFIG_USB_DEVICE_STACK)
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    on_usb = (ep.transport == ZMK_TRANSPORT_USB);
#endif
    if (on_usb) {
        label(s, "USB", &lv_font_montserrat_16, 66);
    } else {
        char pn[4];
        snprintf(pn, sizeof(pn), "%d", zmk_ble_active_profile_index() + 1);
        label(s, "BT", &lv_font_montserrat_14, 52);
        label(s, pn, &lv_font_montserrat_26, 62);
        label(s, zmk_ble_active_profile_is_connected() ? "CONNECTED" : "PAIRING",
              &lv_font_montserrat_14, 92);
    }
    hline(s, 108);
    draw_orbit(s, 112);
#else
    /* COMPANION (left/peripheral) */
    draw_battery(s, 6, zmk_battery_state_of_charge());
    label(s, zmk_split_bt_peripheral_is_connected() ? "LINKED" : "NO LINK",
          &lv_font_montserrat_14, 48);
    hline(s, 66);
    draw_orbit(s, 78);
#endif
    return s;
}
