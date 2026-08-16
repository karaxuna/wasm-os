/**
 * Minimal LVGL v9 configuration for the wasm32-wasi build. Everything not
 * set here falls back to the lv_conf_internal.h defaults; heap and libc
 * calls route to wasi-libc so the module needs no LVGL-private pools.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE

/* Offload the hot RGB565 fill/blend loops to native wasm-os "gfx" bindings
 * (see lv_draw_wos.h); interpreted rendering is ~25x slower. */
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_CUSTOM
#define LV_DRAW_SW_ASM_CUSTOM_INCLUDE "lv_draw_wos.h"

#define LV_USE_LOG 0

/* 10.1" 800x1280 panel */
#define LV_DPI_DEF 150

#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_28

#endif /* LV_CONF_H */
