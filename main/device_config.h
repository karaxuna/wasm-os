#pragma once

#include <stdint.h>

/**
 * Device configuration persisted in NVS (namespace "config"):
 *   ssid / pass   - WiFi credentials
 *   env           - ';'-separated KEY=VALUE pairs passed to the WASM app
 *   log_level     - esp_log_level_t as u8
 *
 * Loaded once at boot; the loaded values live for the firmware's lifetime.
 */

typedef struct {
  char* wifi_ssid; /* NULL when not configured */
  char* wifi_pass; /* NULL for open networks */
  char** env;      /* KEY=VALUE strings handed to WASI */
  uint32_t env_count;
} device_config_t;

/** Load configuration from NVS and apply the stored log level. */
void device_config_load(void);

/** The loaded configuration. Valid after device_config_load(). */
const device_config_t* device_config_get(void);
