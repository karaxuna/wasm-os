#include <stdlib.h>
#include <string.h>

#include "wasm_export.h"

#include "common.h"
#include "modules.h"
#include "storage.h"

/*
 * NVS access for guests. Getters that return variable-sized data are
 * snprintf-style: they return the full size and copy into the guest buffer
 * only when it is large enough.
 */

static int32_t wasm_storage_get_string(wasm_exec_env_t exec_env, char* ns, char* key, uint32_t buf_aptr,
                                       uint32_t buf_cap) {
  char* value = NULL;
  esp_err_t err = storage_get_string(ns, key, &value);
  if (err != ESP_OK) {
    return wos_err(err);
  }

  int32_t result = wos_guest_copy_out(exec_env, buf_aptr, buf_cap, value, strlen(value));
  free(value);
  return result;
}

static int32_t wasm_storage_set_string(wasm_exec_env_t exec_env, char* ns, char* key, char* value) {
  return wos_err(storage_set_string(ns, key, value));
}

static int32_t wasm_storage_get_u32(wasm_exec_env_t exec_env, char* ns, char* key, uint32_t out_aptr) {
  uint32_t value = 0;
  esp_err_t err = storage_get_u32(ns, key, &value);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  return wos_guest_write_u32(exec_env, out_aptr, value) ? WOS_OK : WOS_ERR_BAD_MEMORY;
}

static int32_t wasm_storage_set_u32(wasm_exec_env_t exec_env, char* ns, char* key, uint32_t value) {
  return wos_err(storage_set_u32(ns, key, value));
}

static int32_t wasm_storage_get_u8(wasm_exec_env_t exec_env, char* ns, char* key, uint32_t out_aptr) {
  uint8_t value = 0;
  esp_err_t err = storage_get_u8(ns, key, &value);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  uint8_t* out = wos_guest_ptr(exec_env, out_aptr, sizeof(uint8_t));
  if (!out) {
    return WOS_ERR_BAD_MEMORY;
  }
  *out = value;
  return WOS_OK;
}

static int32_t wasm_storage_set_u8(wasm_exec_env_t exec_env, char* ns, char* key, uint32_t value) {
  return wos_err(storage_set_u8(ns, key, (uint8_t)value));
}

/* Returns the blob's size; copies it into the guest buffer when it fits. */
static int32_t wasm_storage_get_blob(wasm_exec_env_t exec_env, char* ns, char* key, uint32_t buf_aptr,
                                     uint32_t buf_cap) {
  void* value = NULL;
  size_t size = 0;
  esp_err_t err = storage_get_blob(ns, key, &value, &size);
  if (err != ESP_OK) {
    return wos_err(err);
  }

  int32_t result = (int32_t)size;
  if (buf_cap >= size) {
    void* out = wos_guest_ptr(exec_env, buf_aptr, buf_cap);
    if (out) {
      memcpy(out, value, size);
    } else {
      result = WOS_ERR_BAD_MEMORY;
    }
  }
  free(value);
  return result;
}

static int32_t wasm_storage_set_blob(wasm_exec_env_t exec_env, char* ns, char* key, uint32_t data_aptr, uint32_t size) {
  const void* data = wos_guest_ptr(exec_env, data_aptr, size);
  if (!data) {
    return WOS_ERR_BAD_MEMORY;
  }
  return wos_err(storage_set_blob(ns, key, data, size));
}

static NativeSymbol k_symbols[] = {
    {"storage_get_string", wasm_storage_get_string, "($$ii)i", NULL},
    {"storage_set_string", wasm_storage_set_string, "($$$)i", NULL},
    {"storage_get_u32", wasm_storage_get_u32, "($$i)i", NULL},
    {"storage_set_u32", wasm_storage_set_u32, "($$i)i", NULL},
    {"storage_get_u8", wasm_storage_get_u8, "($$i)i", NULL},
    {"storage_set_u8", wasm_storage_set_u8, "($$i)i", NULL},
    {"storage_get_blob", wasm_storage_get_blob, "($$ii)i", NULL},
    {"storage_set_blob", wasm_storage_set_blob, "($$ii)i", NULL},
};

bool wos_register_storage(void) {
  return wasm_runtime_register_natives("storage", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
