/**
 * LVGL LV_DRAW_SW_ASM_CUSTOM hooks backed by the wasm-os "gfx" bindings:
 * the hottest RGB565 fill/blend loops run natively in the firmware instead
 * of the WAMR interpreter. Included by LVGL's blend .c files; see
 * lv_draw_wos.c for the implementations.
 */
#ifndef LV_DRAW_WOS_H
#define LV_DRAW_WOS_H

/* No includes needed: LVGL's blend .c files pull in
 * lv_draw_sw_blend_private.h before this header. */

lv_result_t wos_blend_fill_rgb565(lv_draw_sw_blend_fill_dsc_t* dsc);
lv_result_t wos_blend_fill_rgb565_opa(lv_draw_sw_blend_fill_dsc_t* dsc);
lv_result_t wos_blend_fill_rgb565_mask(lv_draw_sw_blend_fill_dsc_t* dsc);

#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565(dsc) wos_blend_fill_rgb565(dsc)
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565_WITH_OPA(dsc) wos_blend_fill_rgb565_opa(dsc)
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565_WITH_MASK(dsc) wos_blend_fill_rgb565_mask(dsc)
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB565_MIX_MASK_OPA(dsc) wos_blend_fill_rgb565_mask(dsc)

#endif /* LV_DRAW_WOS_H */
