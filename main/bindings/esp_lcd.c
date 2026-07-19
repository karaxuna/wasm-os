#include <stdlib.h>

#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

/*
 * MIPI-DSI LCD support (LDO power, DSI bus, DBI panel IO, DPI panel). Only
 * the ESP32-P4 has the DSI peripheral; on other targets every function is a
 * stub so modules importing "esp_lcd" still instantiate, with constructors
 * returning an invalid handle and operations returning WOS_ERR_UNSUPPORTED.
 */

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"

static void ldo_channel_destroy(void* ptr) {
  esp_ldo_release_channel((esp_ldo_channel_handle_t)ptr);
}

static void dsi_bus_destroy(void* ptr) {
  esp_lcd_del_dsi_bus((esp_lcd_dsi_bus_handle_t)ptr);
}

static void panel_io_destroy(void* ptr) {
  esp_lcd_panel_io_del((esp_lcd_panel_io_handle_t)ptr);
}

static void panel_destroy(void* ptr) {
  esp_lcd_panel_del((esp_lcd_panel_handle_t)ptr);
}

static const wos_handle_type_t LDO_CONFIG_TYPE = {.name = "ldo_channel_config", .destroy = free};
static const wos_handle_type_t LDO_CHANNEL_TYPE = {.name = "ldo_channel", .destroy = ldo_channel_destroy};
static const wos_handle_type_t DSI_BUS_CONFIG_TYPE = {.name = "dsi_bus_config", .destroy = free};
static const wos_handle_type_t DSI_BUS_TYPE = {.name = "dsi_bus", .destroy = dsi_bus_destroy};
static const wos_handle_type_t DBI_IO_CONFIG_TYPE = {.name = "dbi_io_config", .destroy = free};
static const wos_handle_type_t PANEL_IO_TYPE = {.name = "panel_io", .destroy = panel_io_destroy};
static const wos_handle_type_t DPI_PANEL_CONFIG_TYPE = {.name = "dpi_panel_config", .destroy = free};
static const wos_handle_type_t PANEL_TYPE = {.name = "panel", .destroy = panel_destroy};

/* Allocate a zeroed config struct and register it under `type`. */
static uint32_t create_config(const wos_handle_type_t* type, size_t size) {
  void* config = calloc(1, size);
  if (!config) {
    return WOS_HANDLE_INVALID;
  }
  wos_handle_t handle = wos_handle_create(type, config);
  if (handle == WOS_HANDLE_INVALID) {
    free(config);
  }
  return handle;
}

/* Register a freshly created driver object and write its handle out. */
static int32_t export_new_handle(wasm_exec_env_t exec_env, const wos_handle_type_t* type, void* object,
                                 uint32_t out_aptr) {
  wos_handle_t handle = wos_handle_create(type, object);
  if (handle == WOS_HANDLE_INVALID) {
    type->destroy(object);
    return WOS_ERR_NO_MEM;
  }
  if (!wos_guest_write_u32(exec_env, out_aptr, handle)) {
    wos_handle_destroy(handle, type);
    return WOS_ERR_BAD_MEMORY;
  }
  return WOS_OK;
}

/* ── LDO channel ─────────────────────────────────────────────────────────── */

static uint32_t wasm_ldo_channel_config_create(wasm_exec_env_t exec_env) {
  return create_config(&LDO_CONFIG_TYPE, sizeof(esp_ldo_channel_config_t));
}

static int32_t wasm_ldo_channel_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &LDO_CONFIG_TYPE);
}

static int32_t wasm_ldo_channel_config_set_chan_id(wasm_exec_env_t exec_env, uint32_t handle, int32_t chan_id) {
  esp_ldo_channel_config_t* config = wos_handle_deref(handle, &LDO_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->chan_id = chan_id;
  return WOS_OK;
}

static int32_t wasm_ldo_channel_config_set_voltage_mv(wasm_exec_env_t exec_env, uint32_t handle, int32_t voltage_mv) {
  esp_ldo_channel_config_t* config = wos_handle_deref(handle, &LDO_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->voltage_mv = voltage_mv;
  return WOS_OK;
}

static int32_t wasm_ldo_acquire_channel(wasm_exec_env_t exec_env, uint32_t config_handle, uint32_t out_aptr) {
  esp_ldo_channel_config_t* config = wos_handle_deref(config_handle, &LDO_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }

  esp_ldo_channel_handle_t channel = NULL;
  esp_err_t err = esp_ldo_acquire_channel(config, &channel);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  return export_new_handle(exec_env, &LDO_CHANNEL_TYPE, channel, out_aptr);
}

static int32_t wasm_ldo_release_channel(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &LDO_CHANNEL_TYPE);
}

/* ── DSI bus ─────────────────────────────────────────────────────────────── */

static uint32_t wasm_dsi_bus_config_create(wasm_exec_env_t exec_env) {
  uint32_t handle = create_config(&DSI_BUS_CONFIG_TYPE, sizeof(esp_lcd_dsi_bus_config_t));
  if (handle != WOS_HANDLE_INVALID) {
    esp_lcd_dsi_bus_config_t* config = wos_handle_deref(handle, &DSI_BUS_CONFIG_TYPE);
    config->phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  }
  return handle;
}

static int32_t wasm_dsi_bus_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DSI_BUS_CONFIG_TYPE);
}

static int32_t wasm_dsi_bus_config_set_bus_id(wasm_exec_env_t exec_env, uint32_t handle, int32_t bus_id) {
  esp_lcd_dsi_bus_config_t* config = wos_handle_deref(handle, &DSI_BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->bus_id = bus_id;
  return WOS_OK;
}

static int32_t wasm_dsi_bus_config_set_num_data_lanes(wasm_exec_env_t exec_env, uint32_t handle, int32_t lanes) {
  esp_lcd_dsi_bus_config_t* config = wos_handle_deref(handle, &DSI_BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->num_data_lanes = (uint8_t)lanes;
  return WOS_OK;
}

static int32_t wasm_dsi_bus_config_set_lane_bit_rate_mbps(wasm_exec_env_t exec_env, uint32_t handle, int32_t mbps) {
  esp_lcd_dsi_bus_config_t* config = wos_handle_deref(handle, &DSI_BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->lane_bit_rate_mbps = (uint32_t)mbps;
  return WOS_OK;
}

static int32_t wasm_lcd_new_dsi_bus(wasm_exec_env_t exec_env, uint32_t config_handle, uint32_t out_aptr) {
  esp_lcd_dsi_bus_config_t* config = wos_handle_deref(config_handle, &DSI_BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }

  esp_lcd_dsi_bus_handle_t bus = NULL;
  esp_err_t err = esp_lcd_new_dsi_bus(config, &bus);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  return export_new_handle(exec_env, &DSI_BUS_TYPE, bus, out_aptr);
}

static int32_t wasm_lcd_del_dsi_bus(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DSI_BUS_TYPE);
}

/* ── DBI panel IO ────────────────────────────────────────────────────────── */

static uint32_t wasm_dbi_io_config_create(wasm_exec_env_t exec_env) {
  return create_config(&DBI_IO_CONFIG_TYPE, sizeof(esp_lcd_dbi_io_config_t));
}

static int32_t wasm_dbi_io_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DBI_IO_CONFIG_TYPE);
}

static int32_t wasm_dbi_io_config_set_virtual_channel(wasm_exec_env_t exec_env, uint32_t handle, int32_t channel) {
  esp_lcd_dbi_io_config_t* config = wos_handle_deref(handle, &DBI_IO_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->virtual_channel = channel;
  return WOS_OK;
}

static int32_t wasm_dbi_io_config_set_lcd_cmd_bits(wasm_exec_env_t exec_env, uint32_t handle, int32_t bits) {
  esp_lcd_dbi_io_config_t* config = wos_handle_deref(handle, &DBI_IO_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->lcd_cmd_bits = bits;
  return WOS_OK;
}

static int32_t wasm_dbi_io_config_set_lcd_param_bits(wasm_exec_env_t exec_env, uint32_t handle, int32_t bits) {
  esp_lcd_dbi_io_config_t* config = wos_handle_deref(handle, &DBI_IO_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->lcd_param_bits = bits;
  return WOS_OK;
}

static int32_t wasm_lcd_new_panel_io_dbi(wasm_exec_env_t exec_env, uint32_t bus_handle, uint32_t config_handle,
                                         uint32_t out_aptr) {
  esp_lcd_dsi_bus_handle_t bus = wos_handle_deref(bus_handle, &DSI_BUS_TYPE);
  esp_lcd_dbi_io_config_t* config = wos_handle_deref(config_handle, &DBI_IO_CONFIG_TYPE);
  if (!bus || !config) {
    return WOS_ERR_INVALID_HANDLE;
  }

  esp_lcd_panel_io_handle_t io = NULL;
  esp_err_t err = esp_lcd_new_panel_io_dbi(bus, config, &io);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  return export_new_handle(exec_env, &PANEL_IO_TYPE, io, out_aptr);
}

static int32_t wasm_lcd_panel_io_del(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &PANEL_IO_TYPE);
}

static int32_t wasm_lcd_panel_io_tx_param(wasm_exec_env_t exec_env, uint32_t io_handle, int32_t cmd,
                                          uint32_t params_aptr, uint32_t params_len) {
  esp_lcd_panel_io_handle_t io = wos_handle_deref(io_handle, &PANEL_IO_TYPE);
  if (!io) {
    return WOS_ERR_INVALID_HANDLE;
  }

  const void* params = NULL;
  if (params_aptr != 0 && params_len > 0) {
    params = wos_guest_ptr(exec_env, params_aptr, params_len);
    if (!params) {
      return WOS_ERR_BAD_MEMORY;
    }
  }
  return wos_err(esp_lcd_panel_io_tx_param(io, cmd, params, params_len));
}

static int32_t wasm_lcd_panel_io_rx_param(wasm_exec_env_t exec_env, uint32_t io_handle, int32_t cmd,
                                          uint32_t params_aptr, uint32_t params_len) {
  esp_lcd_panel_io_handle_t io = wos_handle_deref(io_handle, &PANEL_IO_TYPE);
  if (!io) {
    return WOS_ERR_INVALID_HANDLE;
  }

  void* params = wos_guest_ptr(exec_env, params_aptr, params_len);
  if (!params) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(esp_lcd_panel_io_rx_param(io, cmd, params, params_len));
}

/* ── DPI panel ───────────────────────────────────────────────────────────── */

static uint32_t wasm_dpi_panel_config_create(wasm_exec_env_t exec_env) {
  uint32_t handle = create_config(&DPI_PANEL_CONFIG_TYPE, sizeof(esp_lcd_dpi_panel_config_t));
  if (handle != WOS_HANDLE_INVALID) {
    esp_lcd_dpi_panel_config_t* config = wos_handle_deref(handle, &DPI_PANEL_CONFIG_TYPE);
    config->dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    config->in_color_format = LCD_COLOR_FMT_RGB565;
    config->num_fbs = 1;
  }
  return handle;
}

static int32_t wasm_dpi_panel_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DPI_PANEL_CONFIG_TYPE);
}

/* Field setters share one shape: resolve config, poke one u32. */
typedef void (*dpi_apply_fn)(esp_lcd_dpi_panel_config_t*, uint32_t);

static int32_t dpi_config_set(uint32_t handle, dpi_apply_fn apply, uint32_t value) {
  esp_lcd_dpi_panel_config_t* config = wos_handle_deref(handle, &DPI_PANEL_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  apply(config, value);
  return WOS_OK;
}

static void apply_virtual_channel(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->virtual_channel = (uint8_t)v;
}
static void apply_dpi_clock_freq_mhz(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->dpi_clock_freq_mhz = v;
}
static void apply_h_size(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.h_size = v;
}
static void apply_v_size(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.v_size = v;
}
static void apply_hsync_pulse_width(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.hsync_pulse_width = v;
}
static void apply_hsync_back_porch(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.hsync_back_porch = v;
}
static void apply_hsync_front_porch(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.hsync_front_porch = v;
}
static void apply_vsync_pulse_width(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.vsync_pulse_width = v;
}
static void apply_vsync_back_porch(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.vsync_back_porch = v;
}
static void apply_vsync_front_porch(esp_lcd_dpi_panel_config_t* c, uint32_t v) {
  c->video_timing.vsync_front_porch = v;
}

#define DPI_SETTER(field)                                                                                              \
  static int32_t wasm_dpi_panel_config_set_##field(wasm_exec_env_t exec_env, uint32_t handle, uint32_t value) {        \
    return dpi_config_set(handle, apply_##field, value);                                                               \
  }

DPI_SETTER(virtual_channel)
DPI_SETTER(dpi_clock_freq_mhz)
DPI_SETTER(h_size)
DPI_SETTER(v_size)
DPI_SETTER(hsync_pulse_width)
DPI_SETTER(hsync_back_porch)
DPI_SETTER(hsync_front_porch)
DPI_SETTER(vsync_pulse_width)
DPI_SETTER(vsync_back_porch)
DPI_SETTER(vsync_front_porch)

static int32_t wasm_lcd_new_panel_dpi(wasm_exec_env_t exec_env, uint32_t bus_handle, uint32_t config_handle,
                                      uint32_t out_aptr) {
  esp_lcd_dsi_bus_handle_t bus = wos_handle_deref(bus_handle, &DSI_BUS_TYPE);
  esp_lcd_dpi_panel_config_t* config = wos_handle_deref(config_handle, &DPI_PANEL_CONFIG_TYPE);
  if (!bus || !config) {
    return WOS_ERR_INVALID_HANDLE;
  }

  esp_lcd_panel_handle_t panel = NULL;
  esp_err_t err = esp_lcd_new_panel_dpi(bus, config, &panel);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  return export_new_handle(exec_env, &PANEL_TYPE, panel, out_aptr);
}

/* ── Panel ops ───────────────────────────────────────────────────────────── */

static int32_t wasm_lcd_panel_reset(wasm_exec_env_t exec_env, uint32_t handle) {
  esp_lcd_panel_handle_t panel = wos_handle_deref(handle, &PANEL_TYPE);
  if (!panel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(esp_lcd_panel_reset(panel));
}

static int32_t wasm_lcd_panel_init(wasm_exec_env_t exec_env, uint32_t handle) {
  esp_lcd_panel_handle_t panel = wos_handle_deref(handle, &PANEL_TYPE);
  if (!panel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(esp_lcd_panel_init(panel));
}

static int32_t wasm_lcd_panel_del(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &PANEL_TYPE);
}

static int32_t wasm_lcd_panel_disp_on_off(wasm_exec_env_t exec_env, uint32_t handle, uint32_t on) {
  esp_lcd_panel_handle_t panel = wos_handle_deref(handle, &PANEL_TYPE);
  if (!panel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(esp_lcd_panel_disp_on_off(panel, on != 0));
}

/* data_len lets the host validate the whole pixel buffer up front. */
static int32_t wasm_lcd_panel_draw_bitmap(wasm_exec_env_t exec_env, uint32_t handle, int32_t x1, int32_t y1, int32_t x2,
                                          int32_t y2, uint32_t data_aptr, uint32_t data_len) {
  esp_lcd_panel_handle_t panel = wos_handle_deref(handle, &PANEL_TYPE);
  if (!panel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* data = wos_guest_ptr(exec_env, data_aptr, data_len);
  if (!data) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, data));
}

#else /* !CONFIG_IDF_TARGET_ESP32P4 */

#define LCD_STUB_CREATE(name)                                                                                          \
  static uint32_t name(wasm_exec_env_t exec_env) {                                                                     \
    return WOS_HANDLE_INVALID;                                                                                         \
  }

#define LCD_STUB_ERR(name, ...)                                                                                        \
  static int32_t name(wasm_exec_env_t exec_env, ##__VA_ARGS__) {                                                       \
    return WOS_ERR_UNSUPPORTED;                                                                                        \
  }

LCD_STUB_CREATE(wasm_ldo_channel_config_create)
LCD_STUB_ERR(wasm_ldo_channel_config_destroy, uint32_t a)
LCD_STUB_ERR(wasm_ldo_channel_config_set_chan_id, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_ldo_channel_config_set_voltage_mv, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_ldo_acquire_channel, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_ldo_release_channel, uint32_t a)

LCD_STUB_CREATE(wasm_dsi_bus_config_create)
LCD_STUB_ERR(wasm_dsi_bus_config_destroy, uint32_t a)
LCD_STUB_ERR(wasm_dsi_bus_config_set_bus_id, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_dsi_bus_config_set_num_data_lanes, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_dsi_bus_config_set_lane_bit_rate_mbps, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_lcd_new_dsi_bus, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_lcd_del_dsi_bus, uint32_t a)

LCD_STUB_CREATE(wasm_dbi_io_config_create)
LCD_STUB_ERR(wasm_dbi_io_config_destroy, uint32_t a)
LCD_STUB_ERR(wasm_dbi_io_config_set_virtual_channel, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_dbi_io_config_set_lcd_cmd_bits, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_dbi_io_config_set_lcd_param_bits, uint32_t a, int32_t b)
LCD_STUB_ERR(wasm_lcd_new_panel_io_dbi, uint32_t a, uint32_t b, uint32_t c)
LCD_STUB_ERR(wasm_lcd_panel_io_del, uint32_t a)
LCD_STUB_ERR(wasm_lcd_panel_io_tx_param, uint32_t a, int32_t b, uint32_t c, uint32_t d)
LCD_STUB_ERR(wasm_lcd_panel_io_rx_param, uint32_t a, int32_t b, uint32_t c, uint32_t d)

LCD_STUB_CREATE(wasm_dpi_panel_config_create)
LCD_STUB_ERR(wasm_dpi_panel_config_destroy, uint32_t a)
LCD_STUB_ERR(wasm_dpi_panel_config_set_virtual_channel, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_dpi_clock_freq_mhz, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_h_size, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_v_size, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_hsync_pulse_width, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_hsync_back_porch, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_hsync_front_porch, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_vsync_pulse_width, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_vsync_back_porch, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_dpi_panel_config_set_vsync_front_porch, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_lcd_new_panel_dpi, uint32_t a, uint32_t b, uint32_t c)

LCD_STUB_ERR(wasm_lcd_panel_reset, uint32_t a)
LCD_STUB_ERR(wasm_lcd_panel_init, uint32_t a)
LCD_STUB_ERR(wasm_lcd_panel_del, uint32_t a)
LCD_STUB_ERR(wasm_lcd_panel_disp_on_off, uint32_t a, uint32_t b)
LCD_STUB_ERR(wasm_lcd_panel_draw_bitmap, uint32_t a, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t d,
             uint32_t l)

#endif /* CONFIG_IDF_TARGET_ESP32P4 */

static NativeSymbol k_symbols[] = {
    {"ldo_channel_config_create", wasm_ldo_channel_config_create, "()i", NULL},
    {"ldo_channel_config_destroy", wasm_ldo_channel_config_destroy, "(i)i", NULL},
    {"ldo_channel_config_set_chan_id", wasm_ldo_channel_config_set_chan_id, "(ii)i", NULL},
    {"ldo_channel_config_set_voltage_mv", wasm_ldo_channel_config_set_voltage_mv, "(ii)i", NULL},
    {"ldo_acquire_channel", wasm_ldo_acquire_channel, "(ii)i", NULL},
    {"ldo_release_channel", wasm_ldo_release_channel, "(i)i", NULL},

    {"dsi_bus_config_create", wasm_dsi_bus_config_create, "()i", NULL},
    {"dsi_bus_config_destroy", wasm_dsi_bus_config_destroy, "(i)i", NULL},
    {"dsi_bus_config_set_bus_id", wasm_dsi_bus_config_set_bus_id, "(ii)i", NULL},
    {"dsi_bus_config_set_num_data_lanes", wasm_dsi_bus_config_set_num_data_lanes, "(ii)i", NULL},
    {"dsi_bus_config_set_lane_bit_rate_mbps", wasm_dsi_bus_config_set_lane_bit_rate_mbps, "(ii)i", NULL},
    {"lcd_new_dsi_bus", wasm_lcd_new_dsi_bus, "(ii)i", NULL},
    {"lcd_del_dsi_bus", wasm_lcd_del_dsi_bus, "(i)i", NULL},

    {"dbi_io_config_create", wasm_dbi_io_config_create, "()i", NULL},
    {"dbi_io_config_destroy", wasm_dbi_io_config_destroy, "(i)i", NULL},
    {"dbi_io_config_set_virtual_channel", wasm_dbi_io_config_set_virtual_channel, "(ii)i", NULL},
    {"dbi_io_config_set_lcd_cmd_bits", wasm_dbi_io_config_set_lcd_cmd_bits, "(ii)i", NULL},
    {"dbi_io_config_set_lcd_param_bits", wasm_dbi_io_config_set_lcd_param_bits, "(ii)i", NULL},
    {"lcd_new_panel_io_dbi", wasm_lcd_new_panel_io_dbi, "(iii)i", NULL},
    {"lcd_panel_io_del", wasm_lcd_panel_io_del, "(i)i", NULL},
    {"lcd_panel_io_tx_param", wasm_lcd_panel_io_tx_param, "(iiii)i", NULL},
    {"lcd_panel_io_rx_param", wasm_lcd_panel_io_rx_param, "(iiii)i", NULL},

    {"dpi_panel_config_create", wasm_dpi_panel_config_create, "()i", NULL},
    {"dpi_panel_config_destroy", wasm_dpi_panel_config_destroy, "(i)i", NULL},
    {"dpi_panel_config_set_virtual_channel", wasm_dpi_panel_config_set_virtual_channel, "(ii)i", NULL},
    {"dpi_panel_config_set_dpi_clock_freq_mhz", wasm_dpi_panel_config_set_dpi_clock_freq_mhz, "(ii)i", NULL},
    {"dpi_panel_config_set_h_size", wasm_dpi_panel_config_set_h_size, "(ii)i", NULL},
    {"dpi_panel_config_set_v_size", wasm_dpi_panel_config_set_v_size, "(ii)i", NULL},
    {"dpi_panel_config_set_hsync_pulse_width", wasm_dpi_panel_config_set_hsync_pulse_width, "(ii)i", NULL},
    {"dpi_panel_config_set_hsync_back_porch", wasm_dpi_panel_config_set_hsync_back_porch, "(ii)i", NULL},
    {"dpi_panel_config_set_hsync_front_porch", wasm_dpi_panel_config_set_hsync_front_porch, "(ii)i", NULL},
    {"dpi_panel_config_set_vsync_pulse_width", wasm_dpi_panel_config_set_vsync_pulse_width, "(ii)i", NULL},
    {"dpi_panel_config_set_vsync_back_porch", wasm_dpi_panel_config_set_vsync_back_porch, "(ii)i", NULL},
    {"dpi_panel_config_set_vsync_front_porch", wasm_dpi_panel_config_set_vsync_front_porch, "(ii)i", NULL},
    {"lcd_new_panel_dpi", wasm_lcd_new_panel_dpi, "(iii)i", NULL},

    {"lcd_panel_reset", wasm_lcd_panel_reset, "(i)i", NULL},
    {"lcd_panel_init", wasm_lcd_panel_init, "(i)i", NULL},
    {"lcd_panel_del", wasm_lcd_panel_del, "(i)i", NULL},
    {"lcd_panel_disp_on_off", wasm_lcd_panel_disp_on_off, "(ii)i", NULL},
    {"lcd_panel_draw_bitmap", wasm_lcd_panel_draw_bitmap, "(iiiiiii)i", NULL},
};

bool wos_register_esp_lcd(void) {
  return wasm_runtime_register_natives("esp_lcd", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
