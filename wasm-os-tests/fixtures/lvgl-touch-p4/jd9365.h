/** JD9365 800x1280 MIPI-DSI panel on the JC8012P4A1C (ESP32-P4) board. */
#pragma once

#include <stdint.h>

#define JD9365_HOR_RES 800
#define JD9365_VER_RES 1280

/* Returns 0 on success, negative wasm-os error otherwise. */
int jd9365_init(void);

/* x2/y2 are exclusive, data is RGB565 (little-endian), len its byte size. */
int jd9365_draw(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void* data, uint32_t len);
