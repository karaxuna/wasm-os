#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/event_groups.h"

#define CONNECT_MAX_RETRIES 10

#define CONNECTED_BIT BIT0
#define FAILED_BIT BIT1

static const char* TAG = "wifi";

static EventGroupHandle_t s_events = NULL;
static esp_netif_t* s_netif = NULL;
static volatile wifi_state_t s_state = WIFI_STATE_DISCONNECTED;
static bool s_started = false;
static int s_retry_count = 0;

static void on_wifi_event(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
  if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    /* Ignore the event a deliberate wifi_disconnect() produces. */
    if (s_state != WIFI_STATE_CONNECTING && s_state != WIFI_STATE_CONNECTED) {
      return;
    }
    xEventGroupClearBits(s_events, CONNECTED_BIT);
    if (s_retry_count < CONNECT_MAX_RETRIES) {
      s_retry_count++;
      s_state = WIFI_STATE_CONNECTING;
      ESP_LOGI(TAG, "Disconnected, reconnecting (attempt %d/%d)", s_retry_count, CONNECT_MAX_RETRIES);
      esp_wifi_connect();
    } else {
      ESP_LOGW(TAG, "Giving up after %d attempts", CONNECT_MAX_RETRIES);
      s_state = WIFI_STATE_FAILED;
      xEventGroupSetBits(s_events, FAILED_BIT);
    }
  } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_count = 0;
    s_state = WIFI_STATE_CONNECTED;
    xEventGroupClearBits(s_events, FAILED_BIT);
    xEventGroupSetBits(s_events, CONNECTED_BIT);
  }
}

/* One-time driver setup; kept for the firmware's lifetime once it succeeds. */
static esp_err_t ensure_init(void) {
  if (s_events) {
    return ESP_OK;
  }

  s_events = xEventGroupCreate();
  if (!s_events) {
    return ESP_ERR_NO_MEM;
  }

  s_netif = esp_netif_create_default_wifi_sta();

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&init_cfg);
  if (err != ESP_OK) {
    goto fail;
  }

  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL);
  if (err != ESP_OK) {
    goto fail_deinit;
  }
  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL);
  if (err != ESP_OK) {
    goto fail_deinit;
  }
  return ESP_OK;

fail_deinit:
  esp_wifi_deinit();
fail:
  if (s_netif) {
    esp_netif_destroy_default_wifi(s_netif);
    s_netif = NULL;
  }
  vEventGroupDelete(s_events);
  s_events = NULL;
  return err;
}

esp_err_t wifi_connect_start(const char* ssid, const char* pass) {
  if (!ssid || ssid[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_state == WIFI_STATE_CONNECTING) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = ensure_init();
  if (err != ESP_OK) {
    return err;
  }

  /* Stop a previous session so esp_wifi_start() re-emits STA_START, which is
   * what kicks off the actual connect. */
  if (s_started) {
    s_state = WIFI_STATE_DISCONNECTED;
    esp_wifi_stop();
    s_started = false;
  }

  wifi_config_t wifi_cfg = {0};
  strncpy((char*)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
  if (pass && pass[0] != '\0') {
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char*)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
  } else {
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    return err;
  }
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  if (err != ESP_OK) {
    return err;
  }

  s_retry_count = 0;
  xEventGroupClearBits(s_events, CONNECTED_BIT | FAILED_BIT);
  s_state = WIFI_STATE_CONNECTING;

  err = esp_wifi_start();
  if (err != ESP_OK) {
    s_state = WIFI_STATE_DISCONNECTED;
    return err;
  }
  s_started = true;

  ESP_LOGI(TAG, "Connecting to SSID %s...", ssid);
  return ESP_OK;
}

esp_err_t wifi_disconnect(void) {
  if (!s_events) {
    return ESP_OK;
  }

  /* Flip the state first so on_wifi_event ignores the resulting
   * STA_DISCONNECTED instead of retrying. */
  s_state = WIFI_STATE_DISCONNECTED;
  xEventGroupClearBits(s_events, CONNECTED_BIT | FAILED_BIT);

  if (s_started) {
    esp_wifi_stop();
    s_started = false;
  }
  return ESP_OK;
}

wifi_state_t wifi_get_state(void) {
  return s_state;
}

esp_err_t wifi_wait_connected(uint32_t timeout_ms) {
  if (!s_events) {
    return ESP_ERR_INVALID_STATE;
  }

  EventBits_t bits =
      xEventGroupWaitBits(s_events, CONNECTED_BIT | FAILED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
  if (bits & CONNECTED_BIT) {
    return ESP_OK;
  }
  if (bits & FAILED_BIT) {
    return ESP_FAIL;
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_get_ip(char* buf, size_t cap) {
  if (s_state != WIFI_STATE_CONNECTED || !s_netif) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_netif_ip_info_t info;
  esp_err_t err = esp_netif_get_ip_info(s_netif, &info);
  if (err != ESP_OK) {
    return err;
  }
  snprintf(buf, cap, IPSTR, IP2STR(&info.ip));
  return ESP_OK;
}
