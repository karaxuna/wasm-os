#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

/*
 * Standard-mode I2S. Read/write results are reported through out-parameters
 * in guest memory (pass 0 to ignore); a timeout of -1 waits forever.
 */

static void channel_destroy(void* ptr) {
  i2s_chan_handle_t channel = ptr;
  i2s_channel_disable(channel);
  i2s_del_channel(channel);
}

static const wos_handle_type_t CHAN_CONFIG_TYPE = {.name = "i2s_chan_config", .destroy = free};
static const wos_handle_type_t STD_CONFIG_TYPE = {.name = "i2s_std_config", .destroy = free};
static const wos_handle_type_t CHANNEL_TYPE = {.name = "i2s_channel", .destroy = channel_destroy};

static TickType_t to_ticks(int32_t timeout_ms) {
  return timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
}

static uint32_t wasm_i2s_chan_cfg_create(wasm_exec_env_t exec_env) {
  i2s_chan_config_t* config = calloc(1, sizeof(i2s_chan_config_t));
  if (!config) {
    return WOS_HANDLE_INVALID;
  }
  config->dma_desc_num = 6;
  config->dma_frame_num = 240;

  wos_handle_t handle = wos_handle_create(&CHAN_CONFIG_TYPE, config);
  if (handle == WOS_HANDLE_INVALID) {
    free(config);
  }
  return handle;
}

static int32_t wasm_i2s_chan_cfg_set_id(wasm_exec_env_t exec_env, uint32_t handle, int32_t id) {
  i2s_chan_config_t* config = wos_handle_deref(handle, &CHAN_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->id = (i2s_port_t)id;
  return WOS_OK;
}

static int32_t wasm_i2s_chan_cfg_set_role(wasm_exec_env_t exec_env, uint32_t handle, int32_t role) {
  i2s_chan_config_t* config = wos_handle_deref(handle, &CHAN_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->role = (i2s_role_t)role;
  return WOS_OK;
}

static int32_t wasm_i2s_chan_cfg_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &CHAN_CONFIG_TYPE);
}

static uint32_t wasm_i2s_std_config_create(wasm_exec_env_t exec_env) {
  i2s_std_config_t* config = calloc(1, sizeof(i2s_std_config_t));
  if (!config) {
    return WOS_HANDLE_INVALID;
  }
  i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  i2s_std_slot_config_t slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  config->clk_cfg = clk_cfg;
  config->slot_cfg = slot_cfg;

  wos_handle_t handle = wos_handle_create(&STD_CONFIG_TYPE, config);
  if (handle == WOS_HANDLE_INVALID) {
    free(config);
  }
  return handle;
}

/* One accessor per commonly tuned field keeps the guest ABI flat and stable. */

static int32_t std_config_set(uint32_t handle, void (*apply)(i2s_std_config_t*, int32_t), int32_t value) {
  i2s_std_config_t* config = wos_handle_deref(handle, &STD_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  apply(config, value);
  return WOS_OK;
}

static void apply_sample_rate(i2s_std_config_t* c, int32_t v) {
  c->clk_cfg.sample_rate_hz = (uint32_t)v;
}
static void apply_data_bit_width(i2s_std_config_t* c, int32_t v) {
  c->slot_cfg.data_bit_width = (i2s_data_bit_width_t)v;
}
static void apply_slot_mode(i2s_std_config_t* c, int32_t v) {
  c->slot_cfg.slot_mode = (i2s_slot_mode_t)v;
}
static void apply_ws_width(i2s_std_config_t* c, int32_t v) {
  c->slot_cfg.ws_width = (uint32_t)v;
}
static void apply_mclk(i2s_std_config_t* c, int32_t v) {
  c->gpio_cfg.mclk = (gpio_num_t)v;
}
static void apply_bclk(i2s_std_config_t* c, int32_t v) {
  c->gpio_cfg.bclk = (gpio_num_t)v;
}
static void apply_ws(i2s_std_config_t* c, int32_t v) {
  c->gpio_cfg.ws = (gpio_num_t)v;
}
static void apply_dout(i2s_std_config_t* c, int32_t v) {
  c->gpio_cfg.dout = (gpio_num_t)v;
}
static void apply_din(i2s_std_config_t* c, int32_t v) {
  c->gpio_cfg.din = (gpio_num_t)v;
}

static int32_t wasm_i2s_std_config_set_sample_rate_hz(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_sample_rate, v);
}
static int32_t wasm_i2s_std_config_set_data_bit_width(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_data_bit_width, v);
}
static int32_t wasm_i2s_std_config_set_slot_mode(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_slot_mode, v);
}
static int32_t wasm_i2s_std_config_set_ws_width(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_ws_width, v);
}
static int32_t wasm_i2s_std_config_set_mclk(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_mclk, v);
}
static int32_t wasm_i2s_std_config_set_bclk(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_bclk, v);
}
static int32_t wasm_i2s_std_config_set_ws(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_ws, v);
}
static int32_t wasm_i2s_std_config_set_dout(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_dout, v);
}
static int32_t wasm_i2s_std_config_set_din(wasm_exec_env_t exec_env, uint32_t handle, int32_t v) {
  return std_config_set(handle, apply_din, v);
}

static int32_t wasm_i2s_std_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &STD_CONFIG_TYPE);
}

/*
 * Create channels; writes the tx/rx channel handles into guest memory.
 * Pass 0 for an out-pointer to skip creating that direction.
 */
static int32_t wasm_i2s_new_channel(wasm_exec_env_t exec_env, uint32_t config_handle, uint32_t tx_out_aptr,
                                    uint32_t rx_out_aptr) {
  i2s_chan_config_t* config = wos_handle_deref(config_handle, &CHAN_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }

  i2s_chan_handle_t tx = NULL;
  i2s_chan_handle_t rx = NULL;
  esp_err_t err = i2s_new_channel(config, tx_out_aptr ? &tx : NULL, rx_out_aptr ? &rx : NULL);
  if (err != ESP_OK) {
    return wos_err(err);
  }

  wos_handle_t tx_handle = tx ? wos_handle_create(&CHANNEL_TYPE, tx) : WOS_HANDLE_INVALID;
  wos_handle_t rx_handle = rx ? wos_handle_create(&CHANNEL_TYPE, rx) : WOS_HANDLE_INVALID;

  bool ok = (!tx || tx_handle != WOS_HANDLE_INVALID) && (!rx || rx_handle != WOS_HANDLE_INVALID);
  if (ok && tx_out_aptr) {
    ok = wos_guest_write_u32(exec_env, tx_out_aptr, tx_handle);
  }
  if (ok && rx_out_aptr) {
    ok = wos_guest_write_u32(exec_env, rx_out_aptr, rx_handle);
  }

  if (!ok) {
    if (tx_handle != WOS_HANDLE_INVALID) {
      wos_handle_destroy(tx_handle, &CHANNEL_TYPE);
    } else if (tx) {
      i2s_del_channel(tx);
    }
    if (rx_handle != WOS_HANDLE_INVALID) {
      wos_handle_destroy(rx_handle, &CHANNEL_TYPE);
    } else if (rx) {
      i2s_del_channel(rx);
    }
    return WOS_ERR_INTERNAL;
  }
  return WOS_OK;
}

static int32_t wasm_i2s_channel_init_std_mode(wasm_exec_env_t exec_env, uint32_t chan_handle, uint32_t config_handle) {
  i2s_chan_handle_t channel = wos_handle_deref(chan_handle, &CHANNEL_TYPE);
  i2s_std_config_t* config = wos_handle_deref(config_handle, &STD_CONFIG_TYPE);
  if (!channel || !config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(i2s_channel_init_std_mode(channel, config));
}

static int32_t wasm_i2s_channel_enable(wasm_exec_env_t exec_env, uint32_t handle) {
  i2s_chan_handle_t channel = wos_handle_deref(handle, &CHANNEL_TYPE);
  if (!channel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(i2s_channel_enable(channel));
}

static int32_t wasm_i2s_channel_disable(wasm_exec_env_t exec_env, uint32_t handle) {
  i2s_chan_handle_t channel = wos_handle_deref(handle, &CHANNEL_TYPE);
  if (!channel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(i2s_channel_disable(channel));
}

static int32_t wasm_i2s_channel_read(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr, uint32_t size,
                                     int32_t timeout_ms, uint32_t bytes_read_out_aptr) {
  i2s_chan_handle_t channel = wos_handle_deref(handle, &CHANNEL_TYPE);
  if (!channel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  void* buf = wos_guest_ptr(exec_env, buf_aptr, size);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }

  size_t bytes_read = 0;
  esp_err_t err = i2s_channel_read(channel, buf, size, &bytes_read, to_ticks(timeout_ms));
  if (bytes_read_out_aptr && !wos_guest_write_u32(exec_env, bytes_read_out_aptr, (uint32_t)bytes_read)) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(err);
}

static int32_t wasm_i2s_channel_write(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr, uint32_t size,
                                      int32_t timeout_ms, uint32_t bytes_written_out_aptr) {
  i2s_chan_handle_t channel = wos_handle_deref(handle, &CHANNEL_TYPE);
  if (!channel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* buf = wos_guest_ptr(exec_env, buf_aptr, size);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }

  size_t bytes_written = 0;
  esp_err_t err = i2s_channel_write(channel, buf, size, &bytes_written, to_ticks(timeout_ms));
  if (bytes_written_out_aptr && !wos_guest_write_u32(exec_env, bytes_written_out_aptr, (uint32_t)bytes_written)) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(err);
}

static int32_t wasm_i2s_channel_preload_data(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr,
                                             uint32_t size, uint32_t bytes_loaded_out_aptr) {
  i2s_chan_handle_t channel = wos_handle_deref(handle, &CHANNEL_TYPE);
  if (!channel) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* buf = wos_guest_ptr(exec_env, buf_aptr, size);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }

  size_t bytes_loaded = 0;
  esp_err_t err = i2s_channel_preload_data(channel, buf, size, &bytes_loaded);
  if (bytes_loaded_out_aptr && !wos_guest_write_u32(exec_env, bytes_loaded_out_aptr, (uint32_t)bytes_loaded)) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(err);
}

static int32_t wasm_i2s_del_channel(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &CHANNEL_TYPE);
}

static NativeSymbol k_symbols[] = {
    {"i2s_chan_cfg_create", wasm_i2s_chan_cfg_create, "()i", NULL},
    {"i2s_chan_cfg_set_id", wasm_i2s_chan_cfg_set_id, "(ii)i", NULL},
    {"i2s_chan_cfg_set_role", wasm_i2s_chan_cfg_set_role, "(ii)i", NULL},
    {"i2s_chan_cfg_destroy", wasm_i2s_chan_cfg_destroy, "(i)i", NULL},
    {"i2s_std_config_create", wasm_i2s_std_config_create, "()i", NULL},
    {"i2s_std_config_set_sample_rate_hz", wasm_i2s_std_config_set_sample_rate_hz, "(ii)i", NULL},
    {"i2s_std_config_set_data_bit_width", wasm_i2s_std_config_set_data_bit_width, "(ii)i", NULL},
    {"i2s_std_config_set_slot_mode", wasm_i2s_std_config_set_slot_mode, "(ii)i", NULL},
    {"i2s_std_config_set_ws_width", wasm_i2s_std_config_set_ws_width, "(ii)i", NULL},
    {"i2s_std_config_set_mclk", wasm_i2s_std_config_set_mclk, "(ii)i", NULL},
    {"i2s_std_config_set_bclk", wasm_i2s_std_config_set_bclk, "(ii)i", NULL},
    {"i2s_std_config_set_ws", wasm_i2s_std_config_set_ws, "(ii)i", NULL},
    {"i2s_std_config_set_dout", wasm_i2s_std_config_set_dout, "(ii)i", NULL},
    {"i2s_std_config_set_din", wasm_i2s_std_config_set_din, "(ii)i", NULL},
    {"i2s_std_config_destroy", wasm_i2s_std_config_destroy, "(i)i", NULL},
    {"i2s_new_channel", wasm_i2s_new_channel, "(iii)i", NULL},
    {"i2s_channel_init_std_mode", wasm_i2s_channel_init_std_mode, "(ii)i", NULL},
    {"i2s_channel_enable", wasm_i2s_channel_enable, "(i)i", NULL},
    {"i2s_channel_disable", wasm_i2s_channel_disable, "(i)i", NULL},
    {"i2s_channel_read", wasm_i2s_channel_read, "(iiiii)i", NULL},
    {"i2s_channel_write", wasm_i2s_channel_write, "(iiiii)i", NULL},
    {"i2s_channel_preload_data", wasm_i2s_channel_preload_data, "(iiii)i", NULL},
    {"i2s_del_channel", wasm_i2s_del_channel, "(i)i", NULL},
};

bool wos_register_i2s_std(void) {
  return wasm_runtime_register_natives("i2s_std", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
