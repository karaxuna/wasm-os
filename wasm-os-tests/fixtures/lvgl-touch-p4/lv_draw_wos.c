/**
 * Implementations for the LV_DRAW_SW_ASM_CUSTOM hooks in lv_draw_wos.h.
 * Each unpacks LVGL's blend descriptor and calls the native wasm-os "gfx"
 * fill; returning LV_RESULT_INVALID falls back to LVGL's interpreted C
 * loops for anything the native path doesn't cover.
 */
#include "lvgl.h"

#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"

#include "wasm_os.h"

lv_result_t wos_blend_fill_rgb565(lv_draw_sw_blend_fill_dsc_t* dsc);
lv_result_t wos_blend_fill_rgb565_opa(lv_draw_sw_blend_fill_dsc_t* dsc);
lv_result_t wos_blend_fill_rgb565_mask(lv_draw_sw_blend_fill_dsc_t* dsc);

lv_result_t wos_blend_fill_rgb565(lv_draw_sw_blend_fill_dsc_t* dsc) {
  int32_t err = wos_gfx_fill_rgb565(dsc->dest_buf, dsc->dest_stride, dsc->dest_w, dsc->dest_h,
                                    lv_color_to_u16(dsc->color));
  return err == 0 ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t wos_blend_fill_rgb565_opa(lv_draw_sw_blend_fill_dsc_t* dsc) {
  int32_t err = wos_gfx_fill_rgb565_opa(dsc->dest_buf, dsc->dest_stride, dsc->dest_w, dsc->dest_h,
                                        lv_color_to_u16(dsc->color), dsc->opa);
  return err == 0 ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t wos_blend_fill_rgb565_mask(lv_draw_sw_blend_fill_dsc_t* dsc) {
  int32_t err = wos_gfx_fill_rgb565_mask(dsc->dest_buf, dsc->dest_stride, dsc->mask_buf, dsc->mask_stride, dsc->dest_w,
                                         dsc->dest_h, lv_color_to_u16(dsc->color), dsc->opa);
  return err == 0 ? LV_RESULT_OK : LV_RESULT_INVALID;
}
