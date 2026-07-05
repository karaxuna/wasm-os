#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/event_groups.h"

#define CONNECT_MAX_RETRIES 10

#define CONNECTED_BIT BIT0
#define FAILED_BIT BIT1

static const char* TAG = "wifi";

static EventGroupHandle_t s_events = NULL;
static int s_retry_count = 0;

static void on_wifi_event(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
  if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_count < CONNECT_MAX_RETRIES) {
      s_retry_count++;
      ESP_LOGI(TAG, "Disconnected, reconnecting (attempt %d/%d)", s_retry_count, CONNECT_MAX_RETRIES);
      esp_wifi_connect();
    } else {
      xEventGroupSetBits(s_events, FAILED_BIT);
    }
  } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_count = 0;
    xEventGroupSetBits(s_events, CONNECTED_BIT);
  }
}

esp_err_t wifi_connect(const char* ssid, const char* pass, uint32_t timeout_ms) {
  if (!ssid) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_events) {
    return ESP_ERR_INVALID_STATE;
  }

  s_events = xEventGroupCreate();
  if (!s_events) {
    return ESP_ERR_NO_MEM;
  }

  esp_netif_t* netif = esp_netif_create_default_wifi_sta();
  esp_event_handler_instance_t wifi_handler = NULL;
  esp_event_handler_instance_t ip_handler = NULL;

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&init_cfg);
  if (err != ESP_OK) {
    goto fail;
  }

  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, &wifi_handler);
  if (err != ESP_OK) {
    goto fail;
  }
  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, &ip_handler);
  if (err != ESP_OK) {
    goto fail;
  }

  wifi_config_t wifi_cfg = {0};
  strncpy((char*)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
  if (pass) {
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char*)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
  } else {
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  }

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    goto fail;
  }
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  if (err != ESP_OK) {
    goto fail;
  }
  err = esp_wifi_start();
  if (err != ESP_OK) {
    goto fail;
  }

  ESP_LOGI(TAG, "Connecting to SSID %s...", ssid);
  EventBits_t bits =
      xEventGroupWaitBits(s_events, CONNECTED_BIT | FAILED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

  if (bits & CONNECTED_BIT) {
    return ESP_OK;
  }

  ESP_LOGW(TAG, "Could not connect to SSID %s (%s)", ssid, (bits & FAILED_BIT) ? "gave up" : "timeout");
  err = ESP_FAIL;
  esp_wifi_stop();

fail:
  if (ip_handler) {
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
  }
  if (wifi_handler) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
  }
  esp_wifi_deinit();
  if (netif) {
    esp_netif_destroy_default_wifi(netif);
  }
  vEventGroupDelete(s_events);
  s_events = NULL;
  return err;
}
