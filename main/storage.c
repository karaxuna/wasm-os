#include "storage.h"

#include <stdlib.h>

#include "nvs.h"

/* Commit (when the write succeeded) and close, preserving the first error. */
static esp_err_t commit_and_close(nvs_handle_t handle, esp_err_t err) {
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

/*
 * nvs_get_str and nvs_get_blob share the query-size-then-read protocol; the
 * wrappers below give them one signature so read_alloc can serve both.
 */
typedef esp_err_t (*sized_read_fn)(nvs_handle_t handle, const char* key, void* buf, size_t* size);

static esp_err_t read_str(nvs_handle_t handle, const char* key, void* buf, size_t* size) {
  return nvs_get_str(handle, key, buf, size);
}

static esp_err_t read_blob(nvs_handle_t handle, const char* key, void* buf, size_t* size) {
  return nvs_get_blob(handle, key, buf, size);
}

static esp_err_t read_alloc(const char* ns, const char* key, sized_read_fn read, void** out, size_t* out_size) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }

  size_t size = 0;
  err = read(handle, key, NULL, &size);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  void* buf = malloc(size);
  if (!buf) {
    nvs_close(handle);
    return ESP_ERR_NO_MEM;
  }

  err = read(handle, key, buf, &size);
  nvs_close(handle);

  if (err != ESP_OK) {
    free(buf);
    return err;
  }

  *out = buf;
  if (out_size) {
    *out_size = size;
  }
  return ESP_OK;
}

esp_err_t storage_get_string(const char* ns, const char* key, char** out) {
  return read_alloc(ns, key, read_str, (void**)out, NULL);
}

esp_err_t storage_get_blob(const char* ns, const char* key, void** out, size_t* out_size) {
  return read_alloc(ns, key, read_blob, out, out_size);
}

esp_err_t storage_set_string(const char* ns, const char* key, const char* value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  return commit_and_close(handle, nvs_set_str(handle, key, value));
}

esp_err_t storage_set_blob(const char* ns, const char* key, const void* data, size_t size) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  return commit_and_close(handle, nvs_set_blob(handle, key, data, size));
}

esp_err_t storage_get_u32(const char* ns, const char* key, uint32_t* out) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_get_u32(handle, key, out);
  nvs_close(handle);
  return err;
}

esp_err_t storage_set_u32(const char* ns, const char* key, uint32_t value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  return commit_and_close(handle, nvs_set_u32(handle, key, value));
}

esp_err_t storage_get_u8(const char* ns, const char* key, uint8_t* out) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_get_u8(handle, key, out);
  nvs_close(handle);
  return err;
}

esp_err_t storage_set_u8(const char* ns, const char* key, uint8_t value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  return commit_and_close(handle, nvs_set_u8(handle, key, value));
}
