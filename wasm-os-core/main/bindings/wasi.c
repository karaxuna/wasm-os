#include <stdint.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "common.h"
#include "modules.h"

static const char* TAG = "wasm";

typedef struct {
  uint32_t buf; /* guest address */
  uint32_t len;
} wasi_iovec_t;

/*
 * Minimal WASI fd_write: routes stdout/stderr to the serial log so plain
 * printf/console output from any WASI-targeting language shows up.
 */
static int32_t wasi_fd_write(wasm_exec_env_t exec_env, int32_t fd, uint32_t iovs_aptr, uint32_t iovs_len,
                             uint32_t nwritten_aptr) {
  const wasi_iovec_t* iovs = wos_guest_ptr(exec_env, iovs_aptr, (uint64_t)iovs_len * sizeof(wasi_iovec_t));
  if (!iovs) {
    return 1; /* WASI errno: not success */
  }

  uint32_t total_written = 0;
  for (uint32_t i = 0; i < iovs_len; i++) {
    if (iovs[i].len == 0) {
      continue;
    }
    const char* buf = wos_guest_ptr(exec_env, iovs[i].buf, iovs[i].len);
    if (!buf) {
      return 1;
    }
    if (fd == 1 || fd == 2) {
      esp_log_write(fd == 2 ? ESP_LOG_ERROR : ESP_LOG_INFO, TAG, "%.*s", (int)iovs[i].len, buf);
    } else {
      ESP_LOGW(TAG, "fd_write to fd=%d is not supported", (int)fd);
    }
    total_written += iovs[i].len;
  }

  if (!wos_guest_write_u32(exec_env, nwritten_aptr, total_written)) {
    return 1;
  }
  return 0;
}

/*
 * Stubs for the stdio/exit machinery wasi-libc links into larger apps (e.g.
 * LVGL fixtures): enough to instantiate, not to do file I/O.
 */
static int32_t wasi_fd_close(wasm_exec_env_t exec_env, int32_t fd) {
  return 8; /* WASI errno: badf */
}

static int32_t wasi_fd_seek(wasm_exec_env_t exec_env, int32_t fd, int64_t offset, int32_t whence,
                            uint32_t newoffset_aptr) {
  return 8; /* WASI errno: badf */
}

static void wasi_proc_exit(wasm_exec_env_t exec_env, int32_t code) {
  ESP_LOGI(TAG, "proc_exit(%d)", (int)code);
  wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), "wasi proc exit");
}

static NativeSymbol k_symbols[] = {
    {"fd_write", wasi_fd_write, "(iiii)i", NULL},
    {"fd_close", wasi_fd_close, "(i)i", NULL},
    {"fd_seek", wasi_fd_seek, "(iIii)i", NULL},
    {"proc_exit", wasi_proc_exit, "(i)", NULL},
};

bool wos_register_wasi(void) {
  return wasm_runtime_register_natives("wasi_snapshot_preview1", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
