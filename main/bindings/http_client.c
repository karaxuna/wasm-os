#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

static const char* TAG = "wasm_http";

typedef struct {
  esp_http_client_config_t config;
  char* url; /* owned copy; config.url points here */
} http_config_t;

typedef struct {
  esp_http_client_handle_t client;
  char* post_data; /* owned copy of the current POST body, if any */
} http_client_t;

static void config_destroy(void* ptr) {
  http_config_t* cfg = ptr;
  free(cfg->url);
  free(cfg);
}

static void client_destroy(void* ptr) {
  http_client_t* wrapper = ptr;
  esp_http_client_cleanup(wrapper->client);
  free(wrapper->post_data);
  free(wrapper);
}

static const wos_handle_type_t CONFIG_TYPE = {.name = "http_config", .destroy = config_destroy};
static const wos_handle_type_t CLIENT_TYPE = {.name = "http_client", .destroy = client_destroy};

static uint32_t wasm_http_client_config_create(wasm_exec_env_t exec_env) {
  http_config_t* cfg = calloc(1, sizeof(http_config_t));
  if (!cfg) {
    return WOS_HANDLE_INVALID;
  }

  wos_handle_t handle = wos_handle_create(&CONFIG_TYPE, cfg);
  if (handle == WOS_HANDLE_INVALID) {
    free(cfg);
  }
  return handle;
}

static int32_t wasm_http_client_config_set_url(wasm_exec_env_t exec_env, uint32_t handle, char* url) {
  http_config_t* cfg = wos_handle_deref(handle, &CONFIG_TYPE);
  if (!cfg) {
    return WOS_ERR_INVALID_HANDLE;
  }

  char* copy = strdup(url);
  if (!copy) {
    return WOS_ERR_NO_MEM;
  }
  free(cfg->url);
  cfg->url = copy;
  cfg->config.url = copy;
  return WOS_OK;
}

static int32_t wasm_http_client_config_set_method(wasm_exec_env_t exec_env, uint32_t handle, int32_t method) {
  http_config_t* cfg = wos_handle_deref(handle, &CONFIG_TYPE);
  if (!cfg) {
    return WOS_ERR_INVALID_HANDLE;
  }
  if (method < 0 || method >= HTTP_METHOD_MAX) {
    return WOS_ERR_INVALID_ARG;
  }
  cfg->config.method = (esp_http_client_method_t)method;
  return WOS_OK;
}

static int32_t wasm_http_client_config_set_timeout_ms(wasm_exec_env_t exec_env, uint32_t handle, int32_t timeout_ms) {
  http_config_t* cfg = wos_handle_deref(handle, &CONFIG_TYPE);
  if (!cfg) {
    return WOS_ERR_INVALID_HANDLE;
  }
  cfg->config.timeout_ms = timeout_ms;
  return WOS_OK;
}

static int32_t wasm_http_client_config_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &CONFIG_TYPE);
}

static uint32_t wasm_http_client_init(wasm_exec_env_t exec_env, uint32_t config_handle) {
  http_config_t* cfg = wos_handle_deref(config_handle, &CONFIG_TYPE);
  if (!cfg || !cfg->url) {
    ESP_LOGE(TAG, "http_client_init requires a config with a URL");
    return WOS_HANDLE_INVALID;
  }

  http_client_t* wrapper = calloc(1, sizeof(http_client_t));
  if (!wrapper) {
    return WOS_HANDLE_INVALID;
  }

  wrapper->client = esp_http_client_init(&cfg->config);
  if (!wrapper->client) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    free(wrapper);
    return WOS_HANDLE_INVALID;
  }

  wos_handle_t handle = wos_handle_create(&CLIENT_TYPE, wrapper);
  if (handle == WOS_HANDLE_INVALID) {
    client_destroy(wrapper);
  }
  return handle;
}

static int32_t wasm_http_client_set_header(wasm_exec_env_t exec_env, uint32_t handle, char* key, char* value) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(esp_http_client_set_header(wrapper->client, key, value));
}

/* Copies the body host-side: guest memory may move before perform() runs. */
static int32_t wasm_http_client_set_post_field(wasm_exec_env_t exec_env, uint32_t handle, uint32_t data_aptr,
                                               uint32_t len) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* data = wos_guest_ptr(exec_env, data_aptr, len);
  if (!data) {
    return WOS_ERR_BAD_MEMORY;
  }

  char* copy = malloc(len);
  if (!copy) {
    return WOS_ERR_NO_MEM;
  }
  memcpy(copy, data, len);
  free(wrapper->post_data);
  wrapper->post_data = copy;

  return wos_err(esp_http_client_set_post_field(wrapper->client, copy, len));
}

/* Returns the HTTP status code, or a negative error. */
static int32_t wasm_http_client_perform(wasm_exec_env_t exec_env, uint32_t handle) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }

  esp_err_t err = esp_http_client_perform(wrapper->client);
  if (err != ESP_OK) {
    return wos_err(err);
  }
  return esp_http_client_get_status_code(wrapper->client);
}

static int32_t wasm_http_client_open(wasm_exec_env_t exec_env, uint32_t handle, int32_t write_len) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return wos_err(esp_http_client_open(wrapper->client, write_len));
}

static int32_t wasm_http_client_write(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr, uint32_t len) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const char* buf = wos_guest_ptr(exec_env, buf_aptr, len);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }

  int written = esp_http_client_write(wrapper->client, buf, len);
  return written < 0 ? WOS_ERR_INTERNAL : written;
}

/* Returns the response content length (or a negative error). */
static int32_t wasm_http_client_fetch_headers(wasm_exec_env_t exec_env, uint32_t handle) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }

  int64_t content_length = esp_http_client_fetch_headers(wrapper->client);
  return content_length < 0 ? WOS_ERR_INTERNAL : (int32_t)content_length;
}

/* Returns the number of bytes read into the guest buffer. */
static int32_t wasm_http_client_read_response(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr,
                                              uint32_t len) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }
  char* buf = wos_guest_ptr(exec_env, buf_aptr, len);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }

  int read = esp_http_client_read_response(wrapper->client, buf, len);
  return read < 0 ? WOS_ERR_INTERNAL : read;
}

static int32_t wasm_http_client_get_status_code(wasm_exec_env_t exec_env, uint32_t handle) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return esp_http_client_get_status_code(wrapper->client);
}

static int32_t wasm_http_client_get_content_length(wasm_exec_env_t exec_env, uint32_t handle) {
  http_client_t* wrapper = wos_handle_deref(handle, &CLIENT_TYPE);
  if (!wrapper) {
    return WOS_ERR_INVALID_HANDLE;
  }
  int64_t length = esp_http_client_get_content_length(wrapper->client);
  return length < 0 ? WOS_ERR_INTERNAL : (int32_t)length;
}

static int32_t wasm_http_client_cleanup(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &CLIENT_TYPE);
}

static NativeSymbol k_symbols[] = {
    {"http_client_config_create", wasm_http_client_config_create, "()i", NULL},
    {"http_client_config_set_url", wasm_http_client_config_set_url, "(i$)i", NULL},
    {"http_client_config_set_method", wasm_http_client_config_set_method, "(ii)i", NULL},
    {"http_client_config_set_timeout_ms", wasm_http_client_config_set_timeout_ms, "(ii)i", NULL},
    {"http_client_config_destroy", wasm_http_client_config_destroy, "(i)i", NULL},
    {"http_client_init", wasm_http_client_init, "(i)i", NULL},
    {"http_client_set_header", wasm_http_client_set_header, "(i$$)i", NULL},
    {"http_client_set_post_field", wasm_http_client_set_post_field, "(iii)i", NULL},
    {"http_client_perform", wasm_http_client_perform, "(i)i", NULL},
    {"http_client_open", wasm_http_client_open, "(ii)i", NULL},
    {"http_client_write", wasm_http_client_write, "(iii)i", NULL},
    {"http_client_fetch_headers", wasm_http_client_fetch_headers, "(i)i", NULL},
    {"http_client_read_response", wasm_http_client_read_response, "(iii)i", NULL},
    {"http_client_get_status_code", wasm_http_client_get_status_code, "(i)i", NULL},
    {"http_client_get_content_length", wasm_http_client_get_content_length, "(i)i", NULL},
    {"http_client_cleanup", wasm_http_client_cleanup, "(i)i", NULL},
};

bool wos_register_http_client(void) {
  return wasm_runtime_register_natives("http_client", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
