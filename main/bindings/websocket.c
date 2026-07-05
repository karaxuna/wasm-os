#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"
#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

/*
 * WebSocket client. Events arrive on the ESP event task and are queued as
 * host-side copies; the guest drains them with websocket_client_recv, which
 * copies payload data into a guest-provided buffer. No guest memory is ever
 * touched from the event task.
 */

#define WS_EVENT_QUEUE_LEN 10
#define WS_EVENT_ENQUEUE_TIMEOUT_MS 100
#define WS_BUFFER_SIZE 2048

static const char* TAG = "wasm_websocket";

typedef struct {
  int32_t id;
  char* data; /* owned malloc'd copy, NULL for data-less events */
  int32_t data_len;
  int32_t payload_offset;
  int32_t payload_len;
} ws_event_t;

/* Layout of the event struct written into guest memory by recv. */
typedef struct {
  int32_t id;
  int32_t data_len;
  int32_t payload_offset;
  int32_t payload_len;
} ws_guest_event_t;

typedef struct {
  esp_websocket_client_handle_t client;
  QueueHandle_t events;
} ws_client_t;

static void drain_queue(QueueHandle_t queue) {
  ws_event_t event;
  while (xQueueReceive(queue, &event, 0) == pdTRUE) {
    free(event.data);
  }
}

static void ws_client_destroy(void* ptr) {
  ws_client_t* ws = ptr;
  esp_websocket_client_stop(ws->client);
  esp_websocket_client_destroy(ws->client);
  drain_queue(ws->events);
  vQueueDelete(ws->events);
  free(ws);
}

static const wos_handle_type_t WS_CLIENT_TYPE = {.name = "websocket_client", .destroy = ws_client_destroy};

static void on_websocket_event(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  ws_client_t* ws = handler_args;
  esp_websocket_event_data_t* data = event_data;

  ws_event_t event = {.id = event_id};
  if (event_id == WEBSOCKET_EVENT_DATA) {
    if (data->data_len == 0) {
      return;
    }
    event.data = malloc(data->data_len);
    if (!event.data) {
      ESP_LOGE(TAG, "Dropping %d-byte websocket chunk: out of memory", data->data_len);
      return;
    }
    memcpy(event.data, data->data_ptr, data->data_len);
    event.data_len = data->data_len;
    event.payload_offset = data->payload_offset;
    event.payload_len = data->payload_len;
  }

  if (xQueueSend(ws->events, &event, pdMS_TO_TICKS(WS_EVENT_ENQUEUE_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGW(TAG, "Event queue full, dropping event %d", (int)event_id);
    free(event.data);
  }
}

static uint32_t wasm_websocket_client_connect(wasm_exec_env_t exec_env, char* uri) {
  ESP_LOGI(TAG, "Connecting to %s...", uri);
  esp_websocket_client_config_t config = {
      .uri = uri,
      .buffer_size = WS_BUFFER_SIZE,
      .ping_interval_sec = 5,
      .reconnect_timeout_ms = 5000,
  };

  ws_client_t* ws = calloc(1, sizeof(ws_client_t));
  if (!ws) {
    return WOS_HANDLE_INVALID;
  }

  ws->events = xQueueCreate(WS_EVENT_QUEUE_LEN, sizeof(ws_event_t));
  ws->client = esp_websocket_client_init(&config);
  if (!ws->events || !ws->client) {
    goto fail;
  }
  if (esp_websocket_register_events(ws->client, WEBSOCKET_EVENT_ANY, on_websocket_event, ws) != ESP_OK) {
    goto fail;
  }
  if (esp_websocket_client_start(ws->client) != ESP_OK) {
    goto fail;
  }

  wos_handle_t handle = wos_handle_create(&WS_CLIENT_TYPE, ws);
  if (handle == WOS_HANDLE_INVALID) {
    ws_client_destroy(ws);
  }
  return handle;

fail:
  if (ws->client) {
    esp_websocket_client_destroy(ws->client);
  }
  if (ws->events) {
    vQueueDelete(ws->events);
  }
  free(ws);
  return WOS_HANDLE_INVALID;
}

/*
 * Wait up to timeout_ms for the next event. Payload data is copied into the
 * guest buffer at buf_aptr (up to buf_cap bytes); event metadata is written
 * to the 16-byte ws_guest_event_t at event_out_aptr.
 *
 * Returns 1 when an event was delivered, 0 on timeout, negative on error.
 */
static int32_t wasm_websocket_client_recv(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr,
                                          uint32_t buf_cap, int32_t timeout_ms, uint32_t event_out_aptr) {
  ws_client_t* ws = wos_handle_deref(handle, &WS_CLIENT_TYPE);
  if (!ws) {
    return WOS_ERR_INVALID_HANDLE;
  }
  ws_guest_event_t* out = wos_guest_ptr(exec_env, event_out_aptr, sizeof(ws_guest_event_t));
  if (!out) {
    return WOS_ERR_BAD_MEMORY;
  }

  ws_event_t event;
  if (xQueueReceive(ws->events, &event, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return 0;
  }

  int32_t copied = 0;
  if (event.data) {
    copied = event.data_len < (int32_t)buf_cap ? event.data_len : (int32_t)buf_cap;
    if (copied > 0) {
      void* buf = wos_guest_ptr(exec_env, buf_aptr, copied);
      if (!buf) {
        free(event.data);
        return WOS_ERR_BAD_MEMORY;
      }
      memcpy(buf, event.data, copied);
    }
    if (copied < event.data_len) {
      ESP_LOGW(TAG, "Guest buffer too small: %d of %d bytes delivered", (int)copied, (int)event.data_len);
    }
    free(event.data);
  }

  out->id = event.id;
  out->data_len = copied;
  out->payload_offset = event.payload_offset;
  out->payload_len = event.payload_len;
  return 1;
}

static int32_t wasm_websocket_client_send_text(wasm_exec_env_t exec_env, uint32_t handle, char* text) {
  ws_client_t* ws = wos_handle_deref(handle, &WS_CLIENT_TYPE);
  if (!ws) {
    return WOS_ERR_INVALID_HANDLE;
  }

  int sent = esp_websocket_client_send_text(ws->client, text, strlen(text), portMAX_DELAY);
  return sent < 0 ? WOS_ERR_INTERNAL : sent;
}

static int32_t wasm_websocket_client_close(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &WS_CLIENT_TYPE);
}

static NativeSymbol k_symbols[] = {
    {"websocket_client_connect", wasm_websocket_client_connect, "($)i", NULL},
    {"websocket_client_recv", wasm_websocket_client_recv, "(iiiii)i", NULL},
    {"websocket_client_send_text", wasm_websocket_client_send_text, "(i$)i", NULL},
    {"websocket_client_close", wasm_websocket_client_close, "(i)i", NULL},
};

bool wos_register_websocket(void) {
  return wasm_runtime_register_natives("websocket", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
