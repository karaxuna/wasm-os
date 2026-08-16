#include <stdint.h>

#include "wasm_export.h"

#include "common.h"
#include "modules.h"

/*
 * Native software-blit helpers for guest GUI rendering (RGB565). Guest
 * toolkits (e.g. LVGL via its ASM-hook mechanism) offload their hottest
 * pixel loops here; running them natively instead of in the interpreter
 * speeds a full-screen fill up by more than an order of magnitude.
 *
 * All buffers are guest addresses validated for their full extent; strides
 * are in bytes, matching LVGL draw-buffer conventions.
 */

/* LVGL's RGB565 mix: fg weighted by opa (255 = fg), bg by the rest. */
static inline uint16_t mix16(uint16_t fg, uint16_t bg, uint8_t opa) {
  uint8_t mix = (opa + 4) >> 3;
  uint32_t bg32 = ((uint32_t)bg | ((uint32_t)bg << 16)) & 0x7E0F81F;
  uint32_t fg32 = ((uint32_t)fg | ((uint32_t)fg << 16)) & 0x7E0F81F;
  uint32_t res = ((((fg32 - bg32) * mix) >> 5) + bg32) & 0x7E0F81F;
  return (uint16_t)((res >> 16) | res);
}

/* Resolve a w x h pixel region with byte stride; NULL when out of bounds. */
static uint8_t* region_ptr(wasm_exec_env_t exec_env, uint32_t addr, int32_t stride, int32_t w, int32_t h,
                           int32_t bytes_per_px) {
  if (w <= 0 || h <= 0 || stride < w * bytes_per_px) {
    return NULL;
  }
  uint64_t span = (uint64_t)stride * (h - 1) + (uint64_t)w * bytes_per_px;
  return wos_guest_ptr(exec_env, addr, span);
}

static int32_t wasm_gfx_fill_rgb565(wasm_exec_env_t exec_env, uint32_t dst_aptr, int32_t dst_stride, int32_t w,
                                    int32_t h, uint32_t color) {
  uint8_t* dst = region_ptr(exec_env, dst_aptr, dst_stride, w, h, 2);
  if (!dst) {
    return WOS_ERR_BAD_MEMORY;
  }

  uint16_t c16 = (uint16_t)color;
  for (int32_t y = 0; y < h; y++) {
    uint16_t* row = (uint16_t*)(dst + (size_t)y * dst_stride);
    for (int32_t x = 0; x < w; x++) {
      row[x] = c16;
    }
  }
  return WOS_OK;
}

static int32_t wasm_gfx_fill_rgb565_opa(wasm_exec_env_t exec_env, uint32_t dst_aptr, int32_t dst_stride, int32_t w,
                                        int32_t h, uint32_t color, uint32_t opa) {
  uint8_t* dst = region_ptr(exec_env, dst_aptr, dst_stride, w, h, 2);
  if (!dst) {
    return WOS_ERR_BAD_MEMORY;
  }

  uint16_t c16 = (uint16_t)color;
  for (int32_t y = 0; y < h; y++) {
    uint16_t* row = (uint16_t*)(dst + (size_t)y * dst_stride);
    for (int32_t x = 0; x < w; x++) {
      row[x] = mix16(c16, row[x], (uint8_t)opa);
    }
  }
  return WOS_OK;
}

static int32_t wasm_gfx_fill_rgb565_mask(wasm_exec_env_t exec_env, uint32_t dst_aptr, int32_t dst_stride,
                                         uint32_t mask_aptr, int32_t mask_stride, int32_t w, int32_t h, uint32_t color,
                                         uint32_t opa) {
  uint8_t* dst = region_ptr(exec_env, dst_aptr, dst_stride, w, h, 2);
  const uint8_t* mask = region_ptr(exec_env, mask_aptr, mask_stride, w, h, 1);
  if (!dst || !mask) {
    return WOS_ERR_BAD_MEMORY;
  }

  uint16_t c16 = (uint16_t)color;
  for (int32_t y = 0; y < h; y++) {
    uint16_t* row = (uint16_t*)(dst + (size_t)y * dst_stride);
    const uint8_t* mrow = mask + (size_t)y * mask_stride;
    for (int32_t x = 0; x < w; x++) {
      uint32_t a = mrow[x];
      if (opa < 255) {
        a = (a * opa) >> 8;
      }
      if (a >= 253) {
        row[x] = c16;
      } else if (a > 2) {
        row[x] = mix16(c16, row[x], (uint8_t)a);
      }
    }
  }
  return WOS_OK;
}

static NativeSymbol k_symbols[] = {
    {"gfx_fill_rgb565", wasm_gfx_fill_rgb565, "(iiiii)i", NULL},
    {"gfx_fill_rgb565_opa", wasm_gfx_fill_rgb565_opa, "(iiiiii)i", NULL},
    {"gfx_fill_rgb565_mask", wasm_gfx_fill_rgb565_mask, "(iiiiiiii)i", NULL},
};

bool wos_register_gfx(void) {
  return wasm_runtime_register_natives("gfx", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
