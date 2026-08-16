#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * Station-mode WiFi manager driven by the WASM app through the "wifi"
 * binding. The firmware never connects on its own; the guest starts a
 * connection and polls (or waits on) the state.
 *
 * The driver is initialized lazily on the first connect and stays up for the
 * firmware's lifetime, so an established connection survives app restarts.
 */

typedef enum {
  WIFI_STATE_DISCONNECTED = 0,
  WIFI_STATE_CONNECTING = 1,
  WIFI_STATE_CONNECTED = 2,
  WIFI_STATE_FAILED = 3,
} wifi_state_t;

/**
 * Start connecting to an access point. Non-blocking; progress is reported
 * via wifi_get_state()/wifi_wait_connected(). `pass` may be NULL for open
 * networks. Fails with ESP_ERR_INVALID_STATE while a connect is already in
 * flight; when already connected, the current network is dropped first.
 */
esp_err_t wifi_connect_start(const char* ssid, const char* pass);

/** Drop the connection (if any) and stop the WiFi driver. */
esp_err_t wifi_disconnect(void);

/** Current connection state. */
wifi_state_t wifi_get_state(void);

/**
 * Block until the in-flight connect resolves. ESP_OK when connected,
 * ESP_FAIL when it gave up, ESP_ERR_TIMEOUT when `timeout_ms` elapses first.
 */
esp_err_t wifi_wait_connected(uint32_t timeout_ms);

/**
 * Format the station's IPv4 address into `buf`. ESP_ERR_INVALID_STATE when
 * not connected.
 */
esp_err_t wifi_get_ip(char* buf, size_t cap);
