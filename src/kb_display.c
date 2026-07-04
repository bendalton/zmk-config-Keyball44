/*
 * Keyball44 dual nice!view dashboard.
 *   central  (RIGHT) -> primary: battery, endpoint (USB or BT profile+host), Orbit
 *   peripheral (LEFT) -> companion: battery, split link, big Orbit
 *
 * The nice!view is 160x68 landscape but mounted portrait. LVGL display rotation
 * is broken for 1-bit panels (zmk#1749), so we draw content UPRIGHT into a
 * square canvas (portrait 68w x 160h occupies the left strip) and rotate the
 * whole canvas 90 deg with lv_canvas_transform -- the technique ZMK's own
 * nice_view / zmk-nice-oled widgets use. v1: state read once at boot.
 */
#include <zephyr/kernel.h>
#include <zmk/display/status_screen.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

#define CW  68     /* portrait width  (physical short side) */
#define SQ  160    /* square canvas side = portrait height  */
#define BG  lv_color_white()
#define FG  lv_color_black()

static lv_color_t cbuf[SQ * SQ];

static void rotate_canvas(lv_obj_t *canvas) {
    static lv_color_t tmp[SQ * SQ];
    memcpy(tmp, cbuf, sizeof(tmp));
    lv_img_dsc_t img;
    img.data = (void *)tmp;
    img.header.cf = LV_IMG_CF_TRUE_COLOR;
    img.header.always_zero = 0;
    img.header.w = SQ;
    img.header.h = SQ;
    lv_canvas_fill_bg(canvas, BG, LV_OPA_COVER);
    lv_canvas_transform(canvas, &img, 900, LV_IMG_ZOOM_NONE, -1, 0, SQ / 2, SQ / 2, false);
}

/* ---- draw helpers: all in upright portrait coords (x:0..68, y:0..160) ---- */
static void d_text(lv_obj_t *cv, const char *t, const lv_font_t *f, lv_coord_t y) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = FG; d.font = f; d.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(cv, 0, y, CW, &d, t);
}

static void d_fill(lv_obj_t *cv, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = FG;
    lv_canvas_draw_rect(cv, x, y, w, h, &d);
}

static void d_battery(lv_obj_t *cv, lv_coord_t y, uint8_t pct) {
    const int bw = 46, bx = (CW - bw) / 2, bh = 15;
    lv_draw_rect_dsc_t o;
    lv_draw_rect_dsc_init(&o);
    o.bg_opa = LV_OPA_TRANSP; o.border_color = FG; o.border_width = 1; o.radius = 2;
    lv_canvas_draw_rect(cv, bx, y, bw, bh, &o);
    int fw = (bw - 4) * pct / 100; if (fw < 0) fw = 0;
    if (fw > 0) d_fill(cv, bx + 2, y + 2, fw, bh - 4);
    d_fill(cv, bx + bw, y + 5, 2, 6);           /* nub */
    char b[8];
    snprintf(b, sizeof(b), "%d%%", pct);
    d_text(cv, b, &lv_font_montserrat_16, y + 19);
}

static void d_orbit(lv_obj_t *cv, lv_coord_t cy, float ballr) {
    const float cx = CW / 2.0f;
    const float lx = -0.5f, ly = -0.62f, lz = 0.6f;
    const float llen = sqrtf(lx*lx + ly*ly + lz*lz);
    static const uint8_t BAY[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    int y0 = (int)(cy - ballr - 4), y1 = (int)(cy + ballr + 4);
    for (int y = y0; y <= y1; y++) {
        for (int x = 0; x < CW; x++) {
            float dx = (x - cx) / ballr, dy = (y - cy) / ballr, d2 = dx*dx + dy*dy;
            if (d2 > 1.0f) continue;
            float dz = sqrtf(1.0f - d2);
            float b = (dx*lx + dy*ly + dz*lz) / llen;
            b = (b < 0 ? 0 : b) * 0.92f + 0.06f;
            float th = (BAY[((y%4)+4)%4][((x%4)+4)%4] + 0.5f) / 16.0f;
            if (b < th) lv_canvas_set_px_color(cv, x, y, FG);
        }
    }
    /* orbit ring + a cursor dot with a bright center */
    for (int a = 0; a < 360; a += 3) {
        float r = a * 3.14159265f / 180.0f;
        int rx = (int)lroundf(cx + cosf(r) * ballr);
        int ry = (int)lroundf(cy + sinf(r) * ballr);
        if (rx >= 0 && rx < CW && ry >= 0 && ry < SQ) lv_canvas_set_px_color(cv, rx, ry, FG);
    }
    float ca = -0.7f, orbr = ballr + 6.0f;
    int ox = (int)lroundf(cx + cosf(ca) * orbr);
    int oy = (int)lroundf(cy + sinf(ca) * orbr * 0.55f);
    for (int yy = -2; yy <= 2; yy++) for (int xx = -2; xx <= 2; xx++)
        if (xx*xx + yy*yy <= 6 && ox+xx>=0 && ox+xx<CW && oy+yy>=0 && oy+yy<SQ)
            lv_canvas_set_px_color(cv, ox+xx, oy+yy, FG);
    if (ox>=0 && ox<CW && oy>=0 && oy<SQ) lv_canvas_set_px_color(cv, ox, oy, BG);
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cv = lv_canvas_create(screen);
    lv_canvas_set_buffer(cv, cbuf, SQ, SQ, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(cv, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(cv, BG, LV_OPA_COVER);

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    d_battery(cv, 8, zmk_battery_state_of_charge());
    d_fill(cv, 8, 46, CW - 16, 1);
    bool on_usb = false;
#if defined(CONFIG_USB_DEVICE_STACK)
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    on_usb = (ep.transport == ZMK_TRANSPORT_USB);
#endif
    if (on_usb) {
        d_text(cv, "USB", &lv_font_montserrat_26, 62);
    } else {
        char pn[4];
        snprintf(pn, sizeof(pn), "%d", zmk_ble_active_profile_index() + 1);
        d_text(cv, "BT", &lv_font_montserrat_14, 54);
        d_text(cv, pn, &lv_font_montserrat_26, 66);
        d_text(cv, zmk_ble_active_profile_is_connected() ? "CONNECTED" : "PAIRING",
               &lv_font_montserrat_14, 96);
    }
    d_fill(cv, 8, 112, CW - 16, 1);
    d_orbit(cv, 138, 18.0f);
#else
    d_battery(cv, 8, zmk_battery_state_of_charge());
    d_text(cv, zmk_split_bt_peripheral_is_connected() ? "LINKED" : "NO LINK",
           &lv_font_montserrat_14, 50);
    d_fill(cv, 8, 68, CW - 16, 1);
    d_orbit(cv, 118, 24.0f);
#endif

    rotate_canvas(cv);
    return screen;
}
