#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include "wasm_export.h"

#include "common.h"
#include "device_config.h"
#include "modules.h"

static const char* TAG = "wasm_env";

static void wasm_reboot(wasm_exec_env_t exec_env) {
  ESP_LOGI(TAG, "App requested reboot");
  esp_restart();
}

static void wasm_delay(wasm_exec_env_t exec_env, int32_t milliseconds) {
  vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

/* Milliseconds since boot; wraps every ~49 days. Tick source for GUI loops. */
static uint32_t wasm_millis(wasm_exec_env_t exec_env) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static void wasm_print(wasm_exec_env_t exec_env, char* message) {
  esp_log_write(ESP_LOG_INFO, TAG, "%s", message);
}

static void wasm_println(wasm_exec_env_t exec_env, char* message) {
  ESP_LOGI(TAG, "%s", message);
}

/* AssemblyScript abort handler: log and stop execution. */
static void wasm_abort(wasm_exec_env_t exec_env, int32_t message, int32_t file_name, int32_t line, int32_t column) {
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
  ESP_LOGE(TAG, "abort() called at line %d, column %d", (int)line, (int)column);
  if (inst) {
    wasm_runtime_set_exception(inst, "abort() called by app");
  }
}

static double wasm_strtod(wasm_exec_env_t exec_env, char* str, uint32_t endptr_aptr) {
  char* end = NULL;
  double result = strtod(str, &end);

  if (endptr_aptr != 0 && end != NULL) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    uint32_t* out = wos_guest_ptr(exec_env, endptr_aptr, sizeof(uint32_t));
    if (out && inst) {
      *out = (uint32_t)wasm_runtime_addr_native_to_app(inst, end);
    }
  }
  return result;
}

/*
 * getenv, snprintf-style: returns the value's length; the value (plus NUL)
 * is copied into the guest buffer when it fits. WOS_ERR_NOT_FOUND when the
 * variable is not set.
 */
static int32_t wasm_getenv(wasm_exec_env_t exec_env, char* name, uint32_t buf_aptr, uint32_t buf_cap) {
  if (!name || name[0] == '\0') {
    return WOS_ERR_INVALID_ARG;
  }

  const device_config_t* config = device_config_get();
  size_t name_len = strlen(name);
  for (uint32_t i = 0; i < config->env_count; i++) {
    const char* entry = config->env[i];
    if (entry && strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
      const char* value = entry + name_len + 1;
      return wos_guest_copy_out(exec_env, buf_aptr, buf_cap, value, strlen(value));
    }
  }
  return WOS_ERR_NOT_FOUND;
}

/* WASI environ_* (old-ABI compatibility for modules importing from "env"). */

static int32_t wasm_environ_sizes_get(wasm_exec_env_t exec_env, uint32_t count_aptr, uint32_t buf_size_aptr) {
  const device_config_t* config = device_config_get();

  uint32_t buf_size = 0;
  for (uint32_t i = 0; i < config->env_count; i++) {
    if (config->env[i]) {
      buf_size += strlen(config->env[i]) + 1;
    }
  }

  if (!wos_guest_write_u32(exec_env, count_aptr, config->env_count) ||
      !wos_guest_write_u32(exec_env, buf_size_aptr, buf_size)) {
    return 1;
  }
  return 0;
}

static int32_t wasm_environ_get(wasm_exec_env_t exec_env, uint32_t environ_aptr, uint32_t buf_aptr) {
  const device_config_t* config = device_config_get();
  if (config->env_count == 0) {
    return 0;
  }

  uint32_t buf_size = 0;
  for (uint32_t i = 0; i < config->env_count; i++) {
    if (config->env[i]) {
      buf_size += strlen(config->env[i]) + 1;
    }
  }

  uint32_t* environ_out = wos_guest_ptr(exec_env, environ_aptr, (uint64_t)config->env_count * sizeof(uint32_t));
  char* buf = wos_guest_ptr(exec_env, buf_aptr, buf_size);
  if (!environ_out || !buf) {
    return 1;
  }

  uint32_t offset = 0;
  for (uint32_t i = 0; i < config->env_count; i++) {
    if (config->env[i]) {
      size_t len = strlen(config->env[i]) + 1;
      memcpy(buf + offset, config->env[i], len);
      environ_out[i] = buf_aptr + offset;
      offset += len;
    }
  }
  return 0;
}

/* Porffor (JS-to-WASM compiler) support: bare print imports. */

static void wasm_porffor_print(wasm_exec_env_t exec_env, int32_t value) {
  printf("%" PRId32, value);
}

static void wasm_porffor_print_char(wasm_exec_env_t exec_env, int32_t char_code) {
  printf("%c", (char)char_code);
}

static NativeSymbol k_env_symbols[] = {
    {"abort", wasm_abort, "(iiii)", NULL},
    {"reboot", wasm_reboot, "()", NULL},
    {"delay", wasm_delay, "(i)", NULL},
    {"millis", wasm_millis, "()i", NULL},
    {"print", wasm_print, "($)", NULL},
    {"println", wasm_println, "($)", NULL},
    {"strtod", wasm_strtod, "($i)F", NULL},
    {"getenv", wasm_getenv, "($ii)i", NULL},
    {"__wasi_environ_sizes_get", wasm_environ_sizes_get, "(ii)i", NULL},
    {"__wasi_environ_get", wasm_environ_get, "(ii)i", NULL},
};

static NativeSymbol k_porffor_symbols[] = {
    {"print", wasm_porffor_print, "(i)", NULL},
    {"printChar", wasm_porffor_print_char, "(i)", NULL},
};

bool wos_register_env(void) {
  return wasm_runtime_register_natives("env", k_env_symbols, sizeof(k_env_symbols) / sizeof(k_env_symbols[0])) &&
         wasm_runtime_register_natives("", k_porffor_symbols, sizeof(k_porffor_symbols) / sizeof(k_porffor_symbols[0]));
}
