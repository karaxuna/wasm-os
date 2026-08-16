/**
 * LVGL demo for the JC8012P4A1C board (ESP32-P4, JD9365 800x1280 MIPI-DSI
 * panel, GSL3680 I2C touch). Fetches a list of buttons as JSON over HTTP
 * (Array<{label, id}>) and renders one on-screen button per entry.
 *
 * Serial markers:
 *   UI_READY       - the buttons are on screen, waiting for a human
 *   BUTTON_PRESSED - a human pressed a button; the app also prints its id
 */
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "buttons.h"
#include "gsl3680.h"
#include "jd9365.h"
#include "wasm_os.h"

#define DRAW_BUF_LINES 128
static uint8_t s_draw_buf[JD9365_HOR_RES * DRAW_BUF_LINES * 2];

static button_data_t s_buttons[BUTTONS_MAX];

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
  jd9365_draw(area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map, w * h * 2);
  lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  int16_t x = 0;
  int16_t y = 0;
  if (gsl3680_read(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void button_pressed_cb(lv_event_t* e) {
  lv_obj_t* button = lv_event_get_target(e);
  const button_data_t* data = lv_event_get_user_data(e);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x2166f3), 0);

  char msg[BUTTON_ID_MAX + 24];
  lv_snprintf(msg, sizeof(msg), "BUTTON_PRESSED %s", data->id);
  wos_println(msg);
}

/* Build one on-screen button per fetched entry, stacked vertically. */
static void render_buttons(lv_obj_t* screen, int count) {
  int32_t margin = 30;
  int32_t gap = 20;
  int32_t avail = JD9365_VER_RES - 2 * margin - (count - 1) * gap;
  int32_t height = count > 0 ? avail / count : 0;

  for (int i = 0; i < count; i++) {
    lv_obj_t* button = lv_button_create(screen);
    lv_obj_set_size(button, JD9365_HOR_RES - 2 * margin, height);
    lv_obj_set_pos(button, margin, margin + i * (height + gap));
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2ba84a), 0);
    /* Sharp corners + no shadow: rounded/shadow masks are generated
     * pixel-by-pixel in the (interpreted) guest and dominate render time. */
    lv_obj_set_style_radius(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, button_pressed_cb, LV_EVENT_PRESSED, &s_buttons[i]);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, s_buttons[i].label);
    lv_obj_center(label);
  }
}

int main(void) {
  wos_println("lvgl-touch-p4 app starting");

  int err = jd9365_init();
  if (err < 0) {
    wos_println("display init failed");
    return 1;
  }
  err = gsl3680_init();
  if (err < 0) {
    wos_println("touch init failed");
    return 1;
  }

  wos_println("connecting to WiFi (stored credentials)...");
  if (wos_wifi_connect("", "") < 0 || wos_wifi_wait(30000) != 0) {
    wos_println("wifi connect failed");
    return 1;
  }

  wos_println("fetching buttons over HTTP...");
  int count = buttons_fetch(s_buttons, BUTTONS_MAX);
  if (count < 0) {
    char msg[48];
    lv_snprintf(msg, sizeof(msg), "button fetch failed: %d", count);
    wos_println(msg);
    return 1;
  }
  {
    char msg[48];
    lv_snprintf(msg, sizeof(msg), "fetched %d buttons", count);
    wos_println(msg);
  }

  lv_init();
  lv_tick_set_cb(wos_millis);

  lv_display_t* disp = lv_display_create(JD9365_HOR_RES, JD9365_VER_RES);
  lv_display_set_buffers(disp, s_draw_buf, NULL, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flush_cb);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);

  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);

  render_buttons(screen, count);

  /* Render the first frame synchronously so UI_READY means "on screen". */
  lv_refr_now(disp);
  wos_println("UI_READY");

  while (true) {
    lv_timer_handler();
    wos_delay(5);
  }
  return 0;
}
