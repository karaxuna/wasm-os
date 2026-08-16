#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"
#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

/*
 * Transactions store guest buffer *addresses*; they are validated and
 * resolved to native pointers only at transmit time, since linear memory may
 * move between set_buffer and transmit.
 */
typedef struct {
  spi_transaction_t txn;
  uint32_t tx_aptr;
  uint32_t rx_aptr;
} spi_txn_t;

static void device_destroy(void* ptr) {
  spi_bus_remove_device((spi_device_handle_t)ptr);
}

static const wos_handle_type_t BUS_CONFIG_TYPE = {.name = "spi_bus_config", .destroy = free};
static const wos_handle_type_t DEV_CONFIG_TYPE = {.name = "spi_device_config", .destroy = free};
static const wos_handle_type_t DEVICE_TYPE = {.name = "spi_device", .destroy = device_destroy};
static const wos_handle_type_t TXN_TYPE = {.name = "spi_transaction", .destroy = free};

static uint32_t wasm_spi_bus_config_create(wasm_exec_env_t exec_env) {
  spi_bus_config_t* config = calloc(1, sizeof(spi_bus_config_t));
  if (!config) {
    return WOS_HANDLE_INVALID;
  }
  config->mosi_io_num = -1;
  config->miso_io_num = -1;
  config->sclk_io_num = -1;
  config->quadwp_io_num = -1;
  config->quadhd_io_num = -1;

  wos_handle_t handle = wos_handle_create(&BUS_CONFIG_TYPE, config);
  if (handle == WOS_HANDLE_INVALID) {
    free(config);
  }
  return handle;
}

static int32_t set_bus_pin(uint32_t handle, int32_t pin, int offset) {
  spi_bus_config_t* config = wos_handle_deref(handle, &BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  switch (offset) {
    case 0:
      config->mosi_io_num = pin;
      break;
    case 1:
      config->miso_io_num = pin;
      break;
    default:
      config->sclk_io_num = pin;
      break;
  }
  return WOS_OK;
}

static int32_t wasm_spi_bus_config_set_mosi_io_num(wasm_exec_env_t exec_env, uint32_t handle, int32_t pin) {
  return set_bus_pin(handle, pin, 0);
}

static int32_t wasm_spi_bus_config_set_miso_io_num(wasm_exec_env_t exec_env, uint32_t handle, int32_t pin) {
  return set_bus_pin(handle, pin, 1);
}

static int32_t wasm_spi_bus_config_set_sclk_io_num(wasm_exec_env_t exec_env, uint32_t handle, int32_t pin) {
  return set_bus_pin(handle, pin, 2);
}

static int32_t wasm_spi_bus_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &BUS_CONFIG_TYPE);
}

static int32_t wasm_spi_bus_initialize(wasm_exec_env_t exec_env, int32_t host_id, uint32_t config_handle,
                                       int32_t dma_chan) {
  spi_bus_config_t* config = wos_handle_deref(config_handle, &BUS_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(spi_bus_initialize((spi_host_device_t)host_id, config, (spi_dma_chan_t)dma_chan));
}

static int32_t wasm_spi_bus_free(wasm_exec_env_t exec_env, int32_t host_id) {
  return wos_err(spi_bus_free((spi_host_device_t)host_id));
}

static uint32_t wasm_spi_device_interface_config_create(wasm_exec_env_t exec_env) {
  spi_device_interface_config_t* config = calloc(1, sizeof(spi_device_interface_config_t));
  if (!config) {
    return WOS_HANDLE_INVALID;
  }
  config->clock_speed_hz = SPI_MASTER_FREQ_20M;
  config->queue_size = 7;
  config->mode = 3;
  config->flags = SPI_DEVICE_NO_DUMMY;

  wos_handle_t handle = wos_handle_create(&DEV_CONFIG_TYPE, config);
  if (handle == WOS_HANDLE_INVALID) {
    free(config);
  }
  return handle;
}

static int32_t wasm_spi_device_interface_config_set_spics_io_num(wasm_exec_env_t exec_env, uint32_t handle,
                                                                 int32_t pin) {
  spi_device_interface_config_t* config = wos_handle_deref(handle, &DEV_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->spics_io_num = pin;
  return WOS_OK;
}

static int32_t wasm_spi_device_interface_config_set_clock_speed_hz(wasm_exec_env_t exec_env, uint32_t handle,
                                                                   int32_t hz) {
  spi_device_interface_config_t* config = wos_handle_deref(handle, &DEV_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  config->clock_speed_hz = hz;
  return WOS_OK;
}

static int32_t wasm_spi_device_interface_config_set_mode(wasm_exec_env_t exec_env, uint32_t handle, int32_t mode) {
  spi_device_interface_config_t* config = wos_handle_deref(handle, &DEV_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }
  if (mode < 0 || mode > 3) {
    return WOS_ERR_INVALID_ARG;
  }
  config->mode = (uint8_t)mode;
  return WOS_OK;
}

static int32_t wasm_spi_device_interface_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DEV_CONFIG_TYPE);
}

/* On success writes the new device handle (u32) to device_out_aptr. */
static int32_t wasm_spi_bus_add_device(wasm_exec_env_t exec_env, int32_t host_id, uint32_t config_handle,
                                       uint32_t device_out_aptr) {
  spi_device_interface_config_t* config = wos_handle_deref(config_handle, &DEV_CONFIG_TYPE);
  if (!config) {
    return WOS_ERR_INVALID_HANDLE;
  }

  spi_device_handle_t device = NULL;
  esp_err_t err = spi_bus_add_device((spi_host_device_t)host_id, config, &device);
  if (err != ESP_OK) {
    return wos_err(err);
  }

  wos_handle_t handle = wos_handle_create(&DEVICE_TYPE, device);
  if (handle == WOS_HANDLE_INVALID || !wos_guest_write_u32(exec_env, device_out_aptr, handle)) {
    if (handle != WOS_HANDLE_INVALID) {
      wos_handle_destroy(handle, &DEVICE_TYPE);
    } else {
      spi_bus_remove_device(device);
    }
    return handle == WOS_HANDLE_INVALID ? WOS_ERR_NO_MEM : WOS_ERR_BAD_MEMORY;
  }
  return WOS_OK;
}

static int32_t wasm_spi_bus_remove_device(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DEVICE_TYPE);
}

static uint32_t wasm_spi_transaction_create(wasm_exec_env_t exec_env) {
  spi_txn_t* txn = calloc(1, sizeof(spi_txn_t));
  if (!txn) {
    return WOS_HANDLE_INVALID;
  }

  wos_handle_t handle = wos_handle_create(&TXN_TYPE, txn);
  if (handle == WOS_HANDLE_INVALID) {
    free(txn);
  }
  return handle;
}

static int32_t wasm_spi_transaction_set_length(wasm_exec_env_t exec_env, uint32_t handle, int32_t length_bits) {
  spi_txn_t* txn = wos_handle_deref(handle, &TXN_TYPE);
  if (!txn) {
    return WOS_ERR_INVALID_HANDLE;
  }
  if (length_bits < 0) {
    return WOS_ERR_INVALID_ARG;
  }
  txn->txn.length = (size_t)length_bits;
  return WOS_OK;
}

static int32_t wasm_spi_transaction_set_tx_buffer(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr) {
  spi_txn_t* txn = wos_handle_deref(handle, &TXN_TYPE);
  if (!txn) {
    return WOS_ERR_INVALID_HANDLE;
  }
  txn->tx_aptr = buf_aptr;
  return WOS_OK;
}

static int32_t wasm_spi_transaction_set_rx_buffer(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr) {
  spi_txn_t* txn = wos_handle_deref(handle, &TXN_TYPE);
  if (!txn) {
    return WOS_ERR_INVALID_HANDLE;
  }
  txn->rx_aptr = buf_aptr;
  return WOS_OK;
}

static int32_t wasm_spi_transaction_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &TXN_TYPE);
}

static int32_t wasm_spi_device_transmit(wasm_exec_env_t exec_env, uint32_t device_handle, uint32_t txn_handle) {
  spi_device_handle_t device = wos_handle_deref(device_handle, &DEVICE_TYPE);
  spi_txn_t* txn = wos_handle_deref(txn_handle, &TXN_TYPE);
  if (!device || !txn) {
    return WOS_ERR_INVALID_HANDLE;
  }

  size_t byte_len = (txn->txn.length + 7) / 8;
  txn->txn.tx_buffer = NULL;
  txn->txn.rx_buffer = NULL;
  if (txn->tx_aptr) {
    txn->txn.tx_buffer = wos_guest_ptr(exec_env, txn->tx_aptr, byte_len);
    if (!txn->txn.tx_buffer) {
      return WOS_ERR_BAD_MEMORY;
    }
  }
  if (txn->rx_aptr) {
    size_t rx_len = txn->txn.rxlength ? (txn->txn.rxlength + 7) / 8 : byte_len;
    txn->txn.rx_buffer = wos_guest_ptr(exec_env, txn->rx_aptr, rx_len);
    if (!txn->txn.rx_buffer) {
      return WOS_ERR_BAD_MEMORY;
    }
  }

  return wos_err(spi_device_transmit(device, &txn->txn));
}

static NativeSymbol k_symbols[] = {
    {"spi_bus_config_create", wasm_spi_bus_config_create, "()i", NULL},
    {"spi_bus_config_set_mosi_io_num", wasm_spi_bus_config_set_mosi_io_num, "(ii)i", NULL},
    {"spi_bus_config_set_miso_io_num", wasm_spi_bus_config_set_miso_io_num, "(ii)i", NULL},
    {"spi_bus_config_set_sclk_io_num", wasm_spi_bus_config_set_sclk_io_num, "(ii)i", NULL},
    {"spi_bus_config_destroy", wasm_spi_bus_config_destroy, "(i)i", NULL},
    {"spi_bus_initialize", wasm_spi_bus_initialize, "(iii)i", NULL},
    {"spi_bus_free", wasm_spi_bus_free, "(i)i", NULL},
    {"spi_device_interface_config_create", wasm_spi_device_interface_config_create, "()i", NULL},
    {"spi_device_interface_config_set_spics_io_num", wasm_spi_device_interface_config_set_spics_io_num, "(ii)i", NULL},
    {"spi_device_interface_config_set_clock_speed_hz", wasm_spi_device_interface_config_set_clock_speed_hz, "(ii)i",
     NULL},
    {"spi_device_interface_config_set_mode", wasm_spi_device_interface_config_set_mode, "(ii)i", NULL},
    {"spi_device_interface_config_destroy", wasm_spi_device_interface_config_destroy, "(i)i", NULL},
    {"spi_bus_add_device", wasm_spi_bus_add_device, "(iii)i", NULL},
    {"spi_bus_remove_device", wasm_spi_bus_remove_device, "(i)i", NULL},
    {"spi_transaction_create", wasm_spi_transaction_create, "()i", NULL},
    {"spi_transaction_set_length", wasm_spi_transaction_set_length, "(ii)i", NULL},
    {"spi_transaction_set_tx_buffer", wasm_spi_transaction_set_tx_buffer, "(ii)i", NULL},
    {"spi_transaction_set_rx_buffer", wasm_spi_transaction_set_rx_buffer, "(ii)i", NULL},
    {"spi_transaction_destroy", wasm_spi_transaction_destroy, "(i)i", NULL},
    {"spi_device_transmit", wasm_spi_device_transmit, "(ii)i", NULL},
};

bool wos_register_spi_master(void) {
  return wasm_runtime_register_natives("spi_master", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
