#pragma once

#include "esp_err.h"

// Serial protocol magic bytes: "WOS!" (0x57 0x4F 0x53 0x21)
#define SERIAL_CMD_MAGIC_0 0x57
#define SERIAL_CMD_MAGIC_1 0x4F
#define SERIAL_CMD_MAGIC_2 0x53
#define SERIAL_CMD_MAGIC_3 0x21

// Commands from CLI to device
#define SERIAL_CMD_PUSH_BEGIN 0x01 // Start file transfer: payload = [total_size:4 LE]
#define SERIAL_CMD_PUSH_DATA 0x02  // File data chunk
#define SERIAL_CMD_PUSH_END 0x03   // End file transfer, write to flash
#define SERIAL_CMD_RESTART 0x04    // Restart WASM module
#define SERIAL_CMD_DELETE 0x05     // Delete a file: payload = filename
#define SERIAL_CMD_LIST 0x06       // List files: ACK payload = repeated [type:1][size:4 LE][name_len:1][name]

// Responses from device to CLI
#define SERIAL_RSP_ACK 0x80
#define SERIAL_RSP_NAK 0x81

// Initialize the serial command handler task
esp_err_t serial_cmd_init(void);
