#include <string.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "common.h"
#include "device_config.h"
#include "modules.h"
#include "wifi.h"

static const char* TAG = "wasm_wifi";

/*
 * Empty ssid means "use the credentials stored in /littlefs/.env"
 * (WIFI_SSID/WIFI_PASS), so real credentials never have to pass through the
 * guest. An explicit ssid with an empty pass targets an open network.
 */
static int32_t wasm_wifi_connect(wasm_exec_env_t exec_env, char* ssid, char* pass) {
  const char* use_ssid = ssid;
  const char* use_pass = pass;

  if (!ssid || ssid[0] == '\0') {
    const device_config_t* config = device_config_get();
    if (!config->wifi_ssid) {
      ESP_LOGW(TAG, "No stored WiFi credentials and no SSID given");
      return WOS_ERR_NOT_FOUND;
    }
    use_ssid = config->wifi_ssid;
    use_pass = config->wifi_pass;
  }
  return wos_err(wifi_connect_start(use_ssid, use_pass));
}

static int32_t wasm_wifi_disconnect(wasm_exec_env_t exec_env) {
  return wos_err(wifi_disconnect());
}

static int32_t wasm_wifi_state(wasm_exec_env_t exec_env) {
  return (int32_t)wifi_get_state();
}

static int32_t wasm_wifi_wait(wasm_exec_env_t exec_env, int32_t timeout_ms) {
  if (timeout_ms < 0) {
    return WOS_ERR_INVALID_ARG;
  }
  return wos_err(wifi_wait_connected((uint32_t)timeout_ms));
}

static int32_t wasm_wifi_ip(wasm_exec_env_t exec_env, uint32_t buf_aptr, uint32_t buf_cap) {
  char ip[16];
  esp_err_t err = wifi_get_ip(ip, sizeof(ip));
  if (err != ESP_OK) {
    return wos_err(err);
  }
  return wos_guest_copy_out(exec_env, buf_aptr, buf_cap, ip, strlen(ip));
}

static NativeSymbol k_symbols[] = {
    {"wifi_connect", wasm_wifi_connect, "($$)i", NULL},
    {"wifi_disconnect", wasm_wifi_disconnect, "()i", NULL},
    {"wifi_state", wasm_wifi_state, "()i", NULL},
    {"wifi_wait", wasm_wifi_wait, "(i)i", NULL},
    {"wifi_ip", wasm_wifi_ip, "(ii)i", NULL},
};

bool wos_register_wifi(void) {
  return wasm_runtime_register_natives("wifi", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
