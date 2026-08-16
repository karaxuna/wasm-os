#include <stdlib.h>

#include "driver/i2c_master.h"
#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

/*
 * I2C master (driver_ng API). Buses are addressed by port number and kept in
 * a static per-port table rather than the generic handle table: the table
 * tears down in slot order, which would delete a bus before the devices that
 * were added to it. Devices live in the handle table (removed first at
 * teardown); wos_i2c_master_reset() then reclaims any bus the guest leaked.
 */

static i2c_master_bus_handle_t s_buses[I2C_NUM_MAX];
static wos_slot_t s_bus_owner[I2C_NUM_MAX];

static void device_destroy(void* ptr) {
  i2c_master_bus_rm_device((i2c_master_dev_handle_t)ptr);
}

static const wos_handle_type_t BUS_CONFIG_TYPE = {.name = "i2c_bus_config", .destroy = free};
static const wos_handle_type_t DEV_CONFIG_TYPE = {.name = "i2c_device_config", .destroy = free};
static const wos_handle_type_t DEVICE_TYPE = {.name = "i2c_device", .destroy = device_destroy};

static uint32_t wasm_i2c_master_bus_config_create(wasm_exec_env_t exec_env) {
  i2c_master_bus_config_t* config = calloc(1, sizeof(i2c_master_bus_config_t));
  if (!config) {
    return WOS_HANDLE_INVALID;
  }
  config->i2c_port = 0;
  config->clk_source = I2C_CLK_SRC_DEFAULT;
  config->glitch_ignore_cnt = 7;

  wos_handle_t handle = wos_handle_create(&BUS_CONFIG_TYPE, config);
  if (handle == WOS_HANDLE_INVALID) {
    free(config);
  }
  return handle;
}

static int32_t wasm_i2c_master_bus_config_set_i2c_port(wasm_exec_env_t exec_env, uint32_t handle, int32_t port) {
  i2c_master_bus_config_t* config = wos_handle_deref(handle, &BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  if (port < 0 || port >= I2C_NUM_MAX) {
    return WOS_ERR_INVALID_ARG;
  }
  config->i2c_port = port;
  return WOS_OK;
}

static int32_t wasm_i2c_master_bus_config_set_sda_io_num(wasm_exec_env_t exec_env, uint32_t handle, int32_t pin) {
  i2c_master_bus_config_t* config = wos_handle_deref(handle, &BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->sda_io_num = pin;
  return WOS_OK;
}

static int32_t wasm_i2c_master_bus_config_set_scl_io_num(wasm_exec_env_t exec_env, uint32_t handle, int32_t pin) {
  i2c_master_bus_config_t* config = wos_handle_deref(handle, &BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->scl_io_num = pin;
  return WOS_OK;
}

static int32_t wasm_i2c_master_bus_config_set_enable_internal_pullup(wasm_exec_env_t exec_env, uint32_t handle,
                                                                     uint32_t enable) {
  i2c_master_bus_config_t* config = wos_handle_deref(handle, &BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->flags.enable_internal_pullup = enable != 0;
  return WOS_OK;
}

static int32_t wasm_i2c_master_bus_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &BUS_CONFIG_TYPE);
}

static int32_t wasm_i2c_new_master_bus(wasm_exec_env_t exec_env, uint32_t config_handle) {
  i2c_master_bus_config_t* config = wos_handle_deref(config_handle, &BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  int port = config->i2c_port;
  if (s_buses[port]) {
    return WOS_ERR_INVALID_ARG;
  }

  i2c_master_bus_handle_t bus = NULL;
  esp_err_t err = i2c_new_master_bus(config, &bus);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  s_buses[port] = bus;
  s_bus_owner[port] = wos_owner_current();
  return WOS_OK;
}

static int32_t wasm_i2c_del_master_bus(wasm_exec_env_t exec_env, int32_t port) {
  if (port < 0 || port >= I2C_NUM_MAX || !s_buses[port]) {
    return WOS_ERR_INVALID_ARG;
  }
  esp_err_t err = i2c_del_master_bus(s_buses[port]);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  s_buses[port] = NULL;
  return WOS_OK;
}

static uint32_t wasm_i2c_device_config_create(wasm_exec_env_t exec_env) {
  i2c_device_config_t* config = calloc(1, sizeof(i2c_device_config_t));
  if (!config) {
    return WOS_HANDLE_INVALID;
  }
  config->dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config->scl_speed_hz = 100000;

  wos_handle_t handle = wos_handle_create(&DEV_CONFIG_TYPE, config);
  if (handle == WOS_HANDLE_INVALID) {
    free(config);
  }
  return handle;
}

static int32_t wasm_i2c_device_config_set_device_address(wasm_exec_env_t exec_env, uint32_t handle, uint32_t address) {
  i2c_device_config_t* config = wos_handle_deref(handle, &DEV_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->device_address = (uint16_t)address;
  return WOS_OK;
}

static int32_t wasm_i2c_device_config_set_scl_speed_hz(wasm_exec_env_t exec_env, uint32_t handle, uint32_t hz) {
  i2c_device_config_t* config = wos_handle_deref(handle, &DEV_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  if (hz == 0) {
    return WOS_ERR_INVALID_ARG;
  }
  config->scl_speed_hz = hz;
  return WOS_OK;
}

static int32_t wasm_i2c_device_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DEV_CONFIG_TYPE);
}

/* On success writes the new device handle (u32) to device_out_aptr. */
static int32_t wasm_i2c_master_bus_add_device(wasm_exec_env_t exec_env, int32_t port, uint32_t config_handle,
                                              uint32_t device_out_aptr) {
  i2c_device_config_t* config = wos_handle_deref(config_handle, &DEV_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  if (port < 0 || port >= I2C_NUM_MAX || !s_buses[port]) {
    return WOS_ERR_INVALID_ARG;
  }

  i2c_master_dev_handle_t device = NULL;
  esp_err_t err = i2c_master_bus_add_device(s_buses[port], config, &device);
  if (err != ESP_OK) {
    return wos_err(err);
  }

  wos_handle_t handle = wos_handle_create(&DEVICE_TYPE, device);
  if (handle == WOS_HANDLE_INVALID || !wos_guest_write_u32(exec_env, device_out_aptr, handle)) {
    if (handle != WOS_HANDLE_INVALID) {
      wos_handle_destroy(handle, &DEVICE_TYPE);
    } else {
      i2c_master_bus_rm_device(device);
    }
    return handle == WOS_HANDLE_INVALID ? WOS_ERR_NO_MEM : WOS_ERR_BAD_MEMORY;
  }
  return WOS_OK;
}

static int32_t wasm_i2c_master_bus_rm_device(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DEVICE_TYPE);
}

static int32_t wasm_i2c_master_transmit(wasm_exec_env_t exec_env, uint32_t device_handle, uint32_t data_aptr,
                                        uint32_t len, int32_t timeout_ms) {
  i2c_master_dev_handle_t device = wos_handle_deref(device_handle, &DEVICE_TYPE);
  if (!device) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* data = wos_guest_ptr(exec_env, data_aptr, len);
  if (!data || len == 0) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(i2c_master_transmit(device, data, len, timeout_ms));
}

static int32_t wasm_i2c_master_receive(wasm_exec_env_t exec_env, uint32_t device_handle, uint32_t data_aptr,
                                       uint32_t len, int32_t timeout_ms) {
  i2c_master_dev_handle_t device = wos_handle_deref(device_handle, &DEVICE_TYPE);
  if (!device) {
    return WOS_ERR_INVALID_HANDLE;
  }
  void* data = wos_guest_ptr(exec_env, data_aptr, len);
  if (!data || len == 0) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(i2c_master_receive(device, data, len, timeout_ms));
}

static int32_t wasm_i2c_master_transmit_receive(wasm_exec_env_t exec_env, uint32_t device_handle, uint32_t tx_aptr,
                                                uint32_t tx_len, uint32_t rx_aptr, uint32_t rx_len,
                                                int32_t timeout_ms) {
  i2c_master_dev_handle_t device = wos_handle_deref(device_handle, &DEVICE_TYPE);
  if (!device) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* tx = wos_guest_ptr(exec_env, tx_aptr, tx_len);
  void* rx = wos_guest_ptr(exec_env, rx_aptr, rx_len);
  if (!tx || tx_len == 0 || !rx || rx_len == 0) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(i2c_master_transmit_receive(device, tx, tx_len, rx, rx_len, timeout_ms));
}

void wos_i2c_master_reset(wos_slot_t owner) {
  for (int port = 0; port < I2C_NUM_MAX; port++) {
    if (s_buses[port] && s_bus_owner[port] == owner) {
      i2c_del_master_bus(s_buses[port]);
      s_buses[port] = NULL;
    }
  }
}

static NativeSymbol k_symbols[] = {
    {"i2c_master_bus_config_create", wasm_i2c_master_bus_config_create, "()i", NULL},
    {"i2c_master_bus_config_set_i2c_port", wasm_i2c_master_bus_config_set_i2c_port, "(ii)i", NULL},
    {"i2c_master_bus_config_set_sda_io_num", wasm_i2c_master_bus_config_set_sda_io_num, "(ii)i", NULL},
    {"i2c_master_bus_config_set_scl_io_num", wasm_i2c_master_bus_config_set_scl_io_num, "(ii)i", NULL},
    {"i2c_master_bus_config_set_enable_internal_pullup", wasm_i2c_master_bus_config_set_enable_internal_pullup, "(ii)i",
     NULL},
    {"i2c_master_bus_config_destroy", wasm_i2c_master_bus_config_destroy, "(i)i", NULL},
    {"i2c_new_master_bus", wasm_i2c_new_master_bus, "(i)i", NULL},
    {"i2c_del_master_bus", wasm_i2c_del_master_bus, "(i)i", NULL},
    {"i2c_device_config_create", wasm_i2c_device_config_create, "()i", NULL},
    {"i2c_device_config_set_device_address", wasm_i2c_device_config_set_device_address, "(ii)i", NULL},
    {"i2c_device_config_set_scl_speed_hz", wasm_i2c_device_config_set_scl_speed_hz, "(ii)i", NULL},
    {"i2c_device_config_destroy", wasm_i2c_device_config_destroy, "(i)i", NULL},
    {"i2c_master_bus_add_device", wasm_i2c_master_bus_add_device, "(iii)i", NULL},
    {"i2c_master_bus_rm_device", wasm_i2c_master_bus_rm_device, "(i)i", NULL},
    {"i2c_master_transmit", wasm_i2c_master_transmit, "(iiii)i", NULL},
    {"i2c_master_receive", wasm_i2c_master_receive, "(iiii)i", NULL},
    {"i2c_master_transmit_receive", wasm_i2c_master_transmit_receive, "(iiiiii)i", NULL},
};

bool wos_register_i2c_master(void) {
  return wasm_runtime_register_natives("i2c_master", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
