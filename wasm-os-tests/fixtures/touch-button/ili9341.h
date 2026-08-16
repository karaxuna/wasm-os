/** ILI9341 240x320 SPI display driver for the CYD (HW-458) board. */
#pragma once

#include <stdint.h>

#define ILI9341_HOR_RES 240
#define ILI9341_VER_RES 320

/** Initialize the SPI bus, panel, and backlight. */
void ili9341_init(void);

/** Write big-endian RGB565 pixels to the window [x1,y1]..[x2,y2] inclusive. */
void ili9341_flush(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t* pixels, uint32_t byte_len);

/** Fill the window [x1,y1]..[x2,y2] inclusive with one RGB565 color. */
void ili9341_fill(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
