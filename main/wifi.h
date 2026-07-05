#pragma once

#include <stdint.h>

#include "esp_err.h"

/**
 * Connect to a WiFi access point in station mode and block until either an
 * IP address is obtained or `timeout_ms` elapses. `pass` may be NULL for
 * open networks.
 *
 * On failure everything is torn down again so the device keeps running
 * without network. Must not be called twice.
 */
esp_err_t wifi_connect(const char* ssid, const char* pass, uint32_t timeout_ms);
