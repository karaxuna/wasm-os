/**
 * Interactive touch test app for the CYD (HW-458) board, written in plain C
 * against the wasm-os bindings (no GUI library: LVGL-sized modules exceed
 * what a PSRAM-less ESP32 can hold in RAM alongside its linear memory).
 *
 * Draws one large green button; serial markers:
 *   UI_READY       - the button is on screen, waiting for a human
 *   BUTTON_PRESSED - the human pressed it; the button turns blue
 */
#include <stdbool.h>
#include <stdint.h>

#include "ili9341.h"
#include "wasm_os.h"
#include "xpt2046.h"

/* RGB565 */
#define COLOR_BACKGROUND 0x10A3 /* near-black */
#define COLOR_BUTTON 0x2CEB     /* green */
#define COLOR_PRESSED 0x2B77    /* blue */

#define BUTTON_X1 15
#define BUTTON_Y1 30
#define BUTTON_X2 (ILI9341_HOR_RES - 1 - 15)
#define BUTTON_Y2 (ILI9341_VER_RES - 1 - 30)

static bool inside_button(int16_t x, int16_t y) {
  return x >= BUTTON_X1 && x <= BUTTON_X2 && y >= BUTTON_Y1 && y <= BUTTON_Y2;
}

int main(void) {
  wos_println("touch-button app starting");

  ili9341_init();
  xpt2046_init();

  ili9341_fill(0, 0, ILI9341_HOR_RES - 1, ILI9341_VER_RES - 1, COLOR_BACKGROUND);
  ili9341_fill(BUTTON_X1, BUTTON_Y1, BUTTON_X2, BUTTON_Y2, COLOR_BUTTON);
  wos_println("UI_READY");

  while (true) {
    int16_t x, y;
    if (xpt2046_read(&x, &y) && inside_button(x, y)) {
      break;
    }
    wos_delay(20);
  }

  ili9341_fill(BUTTON_X1, BUTTON_Y1, BUTTON_X2, BUTTON_Y2, COLOR_PRESSED);
  wos_println("BUTTON_PRESSED");
  wos_println("touch-button app done");
  return 0;
}
