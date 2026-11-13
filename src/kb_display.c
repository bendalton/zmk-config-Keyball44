/*
 * Keyball44 nice!view dashboard (portrait, live-updating).
 *
 * Two independent axes, deliberately kept separate:
 *   ROLE  decides the CONTENT (which ZMK state is even readable here)
 *     central    -> primary: battery, endpoint (USB or BT profile+host), Orbit
 *     peripheral -> companion: battery, split link, big Orbit
 *   SIDE  decides the GEOMETRY (where the case window is)
 *     left / right each have their own content box, tuned below.
 *
 * In the DONGLE setup both halves are peripherals, so both render the
 * companion content -- but the right half still needs the RIGHT box to clear
 * its case window. Conflating the two axes is what made the right screen draw
 * full-width after the dongle switch. The central branch is retained (unused
 * while a dongle is central) so reverting to right-as-central just works.
 *
 * Portrait via canvas rotation (lv_canvas_transform); LVGL display rotation is
 * a no-op on 1-bit panels (zmk#1749). Content drawn upright into a 160x160
 * canvas (portrait 68x160 left strip) then rotated 90 deg. Redraws on ZMK
 * state events via the display work queue.
 *
 * Content is laid out inside a box [BXo, BXo+BWo] x [BYo, ...]; per-side fit
 * constants below let us shift/shrink to clear each case window.
 */
#include <zephyr/kernel.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#if defined(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif
#else
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#endif

#define CW  68
#define SQ  160
#define BG  lv_color_white()
#define FG  lv_color_black()

/* ---- per-side fit: content box in portrait coords. Tune these. ---- */
/* RIGHT: shifted fully left + ~4mm smaller each way (case window). */
#define R_X   0      /* box left edge  (lower = further left)   */
#define R_W   45     /* box width      (68 - ~4mm)              */
#define R_TOP 6      /* top margin                              */
/* LEFT: full width, centered. */
#define L_X   0
#define L_W   68
#define L_TOP 8

/* Pick the box by SIDE (shield), never by role -- see the header note. */
#if defined(CONFIG_SHIELD_KEYBALL44_RIGHT)
#define BOX_X   R_X
#define BOX_W   R_W
#define BOX_TOP R_TOP
#else
#define BOX_X   L_X
#define BOX_W   L_W
#define BOX_TOP L_TOP
#endif

/* The Orbit must fit the box, not just the canvas: d_orbit() clips to the
 * 68px canvas, so a radius tuned for the wide LEFT box would spill under the
 * RIGHT half's narrower case bezel. The orbiting dot sits at r+6 (+2 for the
 * dot itself), so the usable radius is BOX_W/2 - 8. Clamp, never upscale --
 * the left keeps its hand-tuned sizes exactly. */
#define ORBIT_FIT     (BOX_W / 2.0f - 8.0f)
#define ORBIT_R(pref) ((pref) < ORBIT_FIT ? (float)(pref) : ORBIT_FIT)

/* montserrat_14 fits ~4 chars in the narrow RIGHT box (same budget as the
 * central branch's CONN/PAIR), so the split-link label shortens with it. */
#if BOX_W < 56
#define LINK_YES "LINK"
#define LINK_NO  "LOST"
#else
#define LINK_YES "LINKED"
#define LINK_NO  "NO LINK"
#endif

static lv_color_t cbuf[SQ * SQ];
static lv_obj_t *g_cv;
static int BXo = 0, BWo = CW;      /* active content box (x, width) */

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

static void d_text(lv_obj_t *cv, const char *t, const lv_font_t *f, lv_coord_t y) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = FG; d.font = f; d.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(cv, BXo, y, BWo, &d, t);
}

static void d_fill(lv_obj_t *cv, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = FG;
    lv_canvas_draw_rect(cv, x, y, w, h, &d);
}

static void d_hline(lv_obj_t *cv, lv_coord_t y) {
    d_fill(cv, BXo + 4, y, BWo - 8, 1);
}

static void d_battery(lv_obj_t *cv, lv_coord_t y, uint8_t pct) {
    int bw = BWo - 4; if (bw > 46) bw = 46;
    const int bx = BXo + (BWo - bw) / 2, bh = 15;
    lv_draw_rect_dsc_t o;
    lv_draw_rect_dsc_init(&o);
    o.bg_opa = LV_OPA_TRANSP; o.border_color = FG; o.border_width = 1; o.radius = 2;
    lv_canvas_draw_rect(cv, bx, y, bw, bh, &o);
    int fw = (bw - 4) * pct / 100; if (fw < 0) fw = 0;
    if (fw > 0) d_fill(cv, bx + 2, y + 2, fw, bh - 4);
    d_fill(cv, bx + bw, y + 5, 2, 6);
    char b[8];
    snprintf(b, sizeof(b), "%d%%", pct);
    d_text(cv, b, &lv_font_montserrat_16, y + 19);
}

static void d_orbit(lv_obj_t *cv, lv_coord_t cyc, float ballr) {
    const float cx = BXo + BWo / 2.0f, cy = cyc;
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

static void redraw(void) {
    if (!g_cv) return;
    lv_canvas_fill_bg(g_cv, BG, LV_OPA_COVER);

    /* Geometry: side. Content below: role. */
    BXo = BOX_X; BWo = BOX_W;
    int y = BOX_TOP;

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    d_battery(g_cv, y, zmk_battery_state_of_charge());          /* ~y..y+34 */
    d_hline(g_cv, y + 37);
    bool on_usb = false;
#if defined(CONFIG_USB_DEVICE_STACK)
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
    on_usb = (ep.transport == ZMK_TRANSPORT_USB);
#endif
    if (on_usb) {
        d_text(g_cv, "USB", &lv_font_montserrat_16, y + 64);
    } else {
        char pn[4];
        snprintf(pn, sizeof(pn), "%d", zmk_ble_active_profile_index() + 1);
        d_text(g_cv, "BT", &lv_font_montserrat_14, y + 44);
        d_text(g_cv, pn, &lv_font_montserrat_26, y + 56);
        d_text(g_cv, zmk_ble_active_profile_is_connected() ? "CONN" : "PAIR",
               &lv_font_montserrat_14, y + 86);
    }
    d_hline(g_cv, y + 102);
    d_orbit(g_cv, y + 126, ORBIT_R(15.0f));
#else
    d_battery(g_cv, y, zmk_battery_state_of_charge());
    d_text(g_cv, zmk_split_bt_peripheral_is_connected() ? LINK_YES : LINK_NO,
           &lv_font_montserrat_14, y + 42);
    d_hline(g_cv, y + 60);
    d_orbit(g_cv, y + 110, ORBIT_R(24.0f));
#endif

    rotate_canvas(g_cv);
}

static void redraw_cb(struct k_work *work) { redraw(); }
static K_WORK_DEFINE(redraw_work, redraw_cb);

static int kb_display_listener(const zmk_event_t *eh) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &redraw_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(kb_display, kb_display_listener);
ZMK_SUBSCRIPTION(kb_display, zmk_battery_state_changed);
#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
ZMK_SUBSCRIPTION(kb_display, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(kb_display, zmk_endpoint_changed);
#if defined(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(kb_display, zmk_usb_conn_state_changed);
#endif
#else
ZMK_SUBSCRIPTION(kb_display, zmk_split_peripheral_status_changed);
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    g_cv = lv_canvas_create(screen);
    lv_canvas_set_buffer(g_cv, cbuf, SQ, SQ, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(g_cv, LV_ALIGN_TOP_LEFT, 0, 0);

    redraw();
    return screen;
}
