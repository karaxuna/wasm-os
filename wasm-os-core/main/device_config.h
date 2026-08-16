#pragma once

#include <stdint.h>

/**
 * Device configuration read from /littlefs/.env, one KEY=VALUE per line.
 * Blank lines and those starting with '#' are ignored; values may be quoted.
 *
 * LOG_LEVEL (esp_log_level_t as a number) is consumed by the firmware; every
 * other key — including WIFI_SSID/WIFI_PASS — is passed to the WASM app as an
 * environment variable. Apps read them via the env bindings (getenv) and use
 * them explicitly, e.g. passing WiFi credentials to wifi_connect.
 *
 * Loaded once at boot; the loaded values live for the firmware's lifetime.
 */

typedef struct {
  char** env; /* KEY=VALUE strings handed to WASI */
  uint32_t env_count;
} device_config_t;

/** Load configuration from /littlefs/.env and apply the stored log level. */
void device_config_load(void);

/** The loaded configuration. Valid after device_config_load(). */
const device_config_t* device_config_get(void);
