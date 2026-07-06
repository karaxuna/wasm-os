#include "serial_cmd.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_vfs_common.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"
#include "soc/soc_caps.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag_vfs.h"
#else
#include "driver/uart_vfs.h"
#endif

#include "app_runtime.h"

#define TAG "serial_cmd"

#define SERIAL_TASK_STACK_BYTES 8192
#define LITTLEFS_PREFIX "/littlefs/"
#define MAIN_WASM_PATH LITTLEFS_PREFIX "main.wasm"
#define MAX_FILENAME_LEN 64
#define MAX_WASM_SIZE (2 * 1024 * 1024)

#define HEADER_READ_TIMEOUT_MS 5000
#define PAYLOAD_READ_TIMEOUT_MS 30000

/* Protocol frame: [MAGIC:4] [CMD:1] [LEN:4 LE] [PAYLOAD:LEN] */
#define FRAME_HEADER_SIZE 9

/*
 * Incoming files are streamed straight to a temp file and renamed into
 * place on completion: buffering a whole app in RAM is impossible for
 * modules larger than the biggest free heap block.
 */
#define PUSH_TMP_PATH LITTLEFS_PREFIX ".push.tmp"

typedef struct {
  FILE* file;        /* NULL when no transfer is in progress */
  uint32_t expected; /* total bytes announced by PUSH_BEGIN */
  uint32_t received;
  char path[sizeof(LITTLEFS_PREFIX) + MAX_FILENAME_LEN];
} push_transfer_t;

static push_transfer_t s_transfer;
static int s_console_fd = -1;

static void transfer_reset(void) {
  if (s_transfer.file) {
    fclose(s_transfer.file);
    unlink(PUSH_TMP_PATH);
  }
  memset(&s_transfer, 0, sizeof(s_transfer));
}

static void send_response(uint8_t response_code, const char* message) {
  uint32_t msg_len = message ? strlen(message) : 0;
  uint8_t header[FRAME_HEADER_SIZE] = {
      SERIAL_CMD_MAGIC_0,
      SERIAL_CMD_MAGIC_1,
      SERIAL_CMD_MAGIC_2,
      SERIAL_CMD_MAGIC_3,
      response_code,
      (uint8_t)(msg_len >> 0),
      (uint8_t)(msg_len >> 8),
      (uint8_t)(msg_len >> 16),
      (uint8_t)(msg_len >> 24),
  };

  bool ok = write(s_console_fd, header, sizeof(header)) == (ssize_t)sizeof(header);
  if (ok && msg_len > 0) {
    ok = write(s_console_fd, message, msg_len) == (ssize_t)msg_len;
  }
  if (!ok) {
    ESP_LOGW(TAG, "Failed to write response 0x%02x", response_code);
  }
  fsync(s_console_fd);
}

static void nak(const char* message) {
  send_response(SERIAL_RSP_NAK, message);
}

static void handle_push_begin(const uint8_t* payload, uint32_t len) {
  transfer_reset();

  if (len < 4) {
    nak("Invalid push begin payload");
    return;
  }

  uint32_t total = payload[0] | (payload[1] << 8) | (payload[2] << 16) | ((uint32_t)payload[3] << 24);
  if (total == 0 || total > MAX_WASM_SIZE) {
    nak("Invalid file size");
    return;
  }

  /* The rest of the payload is the destination filename (flat namespace). */
  const uint8_t* name = payload + 4;
  uint32_t name_len = len - 4;
  if (name_len == 0) {
    nak("Missing filename");
    return;
  }
  if (name_len > MAX_FILENAME_LEN) {
    nak("Filename too long");
    return;
  }
  for (uint32_t i = 0; i < name_len; i++) {
    if (name[i] == '/' || name[i] == '\\' || name[i] == '\0') {
      nak("Invalid filename");
      return;
    }
  }

  s_transfer.file = fopen(PUSH_TMP_PATH, "wb");
  if (!s_transfer.file) {
    ESP_LOGE(TAG, "Failed to open %s for writing", PUSH_TMP_PATH);
    nak("Failed to open file");
    return;
  }
  s_transfer.expected = total;
  s_transfer.received = 0;
  memcpy(s_transfer.path, LITTLEFS_PREFIX, strlen(LITTLEFS_PREFIX));
  memcpy(s_transfer.path + strlen(LITTLEFS_PREFIX), name, name_len);
  s_transfer.path[strlen(LITTLEFS_PREFIX) + name_len] = '\0';

  ESP_LOGI(TAG, "Push begin: expecting %lu bytes -> %s", (unsigned long)total, s_transfer.path);
  send_response(SERIAL_RSP_ACK, NULL);
}

static void handle_push_data(const uint8_t* payload, uint32_t len) {
  if (!s_transfer.file) {
    nak("No push in progress");
    return;
  }
  if (s_transfer.received + len > s_transfer.expected) {
    transfer_reset();
    nak("Data exceeds expected size");
    return;
  }

  if (fwrite(payload, 1, len, s_transfer.file) != len) {
    ESP_LOGE(TAG, "Short write to %s (filesystem full?)", PUSH_TMP_PATH);
    transfer_reset();
    nak("Write failed");
    return;
  }
  s_transfer.received += len;
  ESP_LOGD(TAG, "Push data: %lu/%lu bytes", (unsigned long)s_transfer.received, (unsigned long)s_transfer.expected);
  send_response(SERIAL_RSP_ACK, NULL);
}

static void handle_push_end(void) {
  if (!s_transfer.file) {
    nak("No push in progress");
    return;
  }
  if (s_transfer.received != s_transfer.expected) {
    ESP_LOGE(TAG, "Incomplete transfer: got %lu of %lu bytes", (unsigned long)s_transfer.received,
             (unsigned long)s_transfer.expected);
    transfer_reset();
    nak("Incomplete transfer");
    return;
  }

  if (fclose(s_transfer.file) != 0) {
    s_transfer.file = NULL;
    transfer_reset();
    nak("Write failed");
    return;
  }
  s_transfer.file = NULL;

  /* The app is stopped only once the payload is fully on flash, then the
   * temp file atomically replaces the target. */
  bool is_main_app = strcmp(s_transfer.path, MAIN_WASM_PATH) == 0;
  if (is_main_app) {
    app_runtime_stop();
  }

  if (rename(PUSH_TMP_PATH, s_transfer.path) != 0) {
    ESP_LOGE(TAG, "Failed to rename %s -> %s", PUSH_TMP_PATH, s_transfer.path);
    transfer_reset();
    nak("Write failed");
    if (is_main_app) {
      app_runtime_start(); /* old main.wasm is still intact */
    }
    return;
  }

  ESP_LOGI(TAG, "Wrote %lu bytes to %s", (unsigned long)s_transfer.expected, s_transfer.path);
  transfer_reset();
  send_response(SERIAL_RSP_ACK, NULL);

  if (is_main_app) {
    app_runtime_start();
  }
}

static void handle_restart(void) {
  ESP_LOGI(TAG, "Restart requested");
  app_runtime_stop();
  send_response(SERIAL_RSP_ACK, NULL);
  app_runtime_start();
}

/* Read exactly `len` bytes from the console, giving up after `timeout_ms`. */
static bool read_exact(uint8_t* buf, uint32_t len, uint32_t timeout_ms) {
  uint32_t received = 0;
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

  while (received < len) {
    if (xTaskGetTickCount() >= deadline) {
      return false;
    }
    int n = read(s_console_fd, buf + received, len - received);
    if (n > 0) {
      received += n;
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  return true;
}

static void dispatch(uint8_t cmd, const uint8_t* payload, uint32_t len) {
  switch (cmd) {
    case SERIAL_CMD_PUSH_BEGIN:
      handle_push_begin(payload, len);
      break;
    case SERIAL_CMD_PUSH_DATA:
      handle_push_data(payload, len);
      break;
    case SERIAL_CMD_PUSH_END:
      handle_push_end();
      break;
    case SERIAL_CMD_RESTART:
      handle_restart();
      break;
    default:
      ESP_LOGW(TAG, "Unknown command: 0x%02x", cmd);
      nak("Unknown command");
      break;
  }
}

static void serial_cmd_task(void* arg) {
  static const uint8_t magic[] = {SERIAL_CMD_MAGIC_0, SERIAL_CMD_MAGIC_1, SERIAL_CMD_MAGIC_2, SERIAL_CMD_MAGIC_3};
  int magic_idx = 0;
  uint8_t byte;

  while (true) {
    /* Scan the byte stream for the 4-byte magic that opens every frame. */
    int n = read(s_console_fd, &byte, 1);
    if (n <= 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (byte != magic[magic_idx]) {
      magic_idx = (byte == magic[0]) ? 1 : 0;
      continue;
    }
    if (++magic_idx < 4) {
      continue;
    }
    magic_idx = 0;

    uint8_t header[5]; /* [CMD:1] [LEN:4 LE] */
    if (!read_exact(header, sizeof(header), HEADER_READ_TIMEOUT_MS)) {
      ESP_LOGW(TAG, "Timeout reading command header");
      continue;
    }

    uint8_t cmd = header[0];
    uint32_t payload_len = header[1] | (header[2] << 8) | (header[3] << 16) | ((uint32_t)header[4] << 24);

    uint8_t* payload = NULL;
    if (payload_len > 0) {
      if (payload_len > MAX_WASM_SIZE) {
        ESP_LOGE(TAG, "Payload too large: %lu", (unsigned long)payload_len);
        nak("Payload too large");
        continue;
      }
      payload = malloc(payload_len);
      if (!payload) {
        ESP_LOGE(TAG, "Failed to allocate %lu bytes for payload", (unsigned long)payload_len);
        nak("Out of memory");
        continue;
      }
      if (!read_exact(payload, payload_len, PAYLOAD_READ_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "Timeout reading payload (%lu bytes)", (unsigned long)payload_len);
        free(payload);
        nak("Timeout reading payload");
        continue;
      }
    }

    dispatch(cmd, payload, payload_len);
    free(payload);
  }
}

esp_err_t serial_cmd_init(void) {
  /* Disable newline conversion so binary frames pass through unchanged. */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
  usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
  usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
#else
  uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
  uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_LF);
#endif

  /* Works for both UART and USB-Serial/JTAG consoles. */
  s_console_fd = open("/dev/console", O_RDWR | O_NONBLOCK);
  if (s_console_fd < 0) {
    s_console_fd = fileno(stdin);
    int flags = fcntl(s_console_fd, F_GETFL, 0);
    fcntl(s_console_fd, F_SETFL, flags | O_NONBLOCK);
  }

  if (xTaskCreate(serial_cmd_task, "serial_cmd", SERIAL_TASK_STACK_BYTES, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create serial command task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Serial command handler started on fd %d", s_console_fd);
  return ESP_OK;
}
