#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

/*
 * Filesystem access, sandboxed to the LittleFS partition. Paths may be given
 * relative ("data.txt") or absolute ("/littlefs/data.txt"); anything that
 * escapes /littlefs (other roots, ".." components) is rejected.
 */

#define FS_ROOT "/littlefs/"
#define FS_PATH_MAX 128

static const char* TAG = "wasm_fs";

static void file_destroy(void* ptr) {
  fclose((FILE*)ptr);
}

static void dir_destroy(void* ptr) {
  closedir((DIR*)ptr);
}

static const wos_handle_type_t FILE_TYPE = {.name = "file", .destroy = file_destroy};
static const wos_handle_type_t DIR_TYPE = {.name = "directory", .destroy = dir_destroy};

static bool resolve_path(const char* in, char* out, size_t out_size) {
  if (!in || in[0] == '\0') {
    return false;
  }
  if (strstr(in, "..")) {
    return false;
  }

  int written;
  if (strncmp(in, FS_ROOT, strlen(FS_ROOT)) == 0) {
    written = snprintf(out, out_size, "%s", in);
  } else if (in[0] == '/') {
    return false; /* absolute path outside the sandbox */
  } else {
    written = snprintf(out, out_size, FS_ROOT "%s", in);
  }
  return written > 0 && (size_t)written < out_size;
}

static uint32_t wasm_fs_open(wasm_exec_env_t exec_env, char* path, char* mode) {
  char resolved[FS_PATH_MAX];
  if (!resolve_path(path, resolved, sizeof(resolved))) {
    ESP_LOGE(TAG, "Rejected path: %s", path ? path : "(null)");
    return WOS_HANDLE_INVALID;
  }

  FILE* file = fopen(resolved, mode);
  if (!file) {
    return WOS_HANDLE_INVALID;
  }

  wos_handle_t handle = wos_handle_create(&FILE_TYPE, file);
  if (handle == WOS_HANDLE_INVALID) {
    fclose(file);
  }
  return handle;
}

static int32_t wasm_fs_close(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &FILE_TYPE);
}

static int32_t wasm_fs_read(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr, uint32_t len) {
  FILE* file = wos_handle_deref(handle, &FILE_TYPE);
  if (!file) {
    return WOS_ERR_INVALID_HANDLE;
  }
  void* buf = wos_guest_ptr(exec_env, buf_aptr, len);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }
  return (int32_t)fread(buf, 1, len, file);
}

static int32_t wasm_fs_write(wasm_exec_env_t exec_env, uint32_t handle, uint32_t buf_aptr, uint32_t len) {
  FILE* file = wos_handle_deref(handle, &FILE_TYPE);
  if (!file) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* buf = wos_guest_ptr(exec_env, buf_aptr, len);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }
  return (int32_t)fwrite(buf, 1, len, file);
}

static int32_t wasm_fs_seek(wasm_exec_env_t exec_env, uint32_t handle, int32_t offset, int32_t whence) {
  FILE* file = wos_handle_deref(handle, &FILE_TYPE);
  if (!file) {
    return WOS_ERR_INVALID_HANDLE;
  }
  return fseek(file, offset, whence) == 0 ? WOS_OK : WOS_ERR_INVALID_ARG;
}

static int32_t wasm_fs_tell(wasm_exec_env_t exec_env, uint32_t handle) {
  FILE* file = wos_handle_deref(handle, &FILE_TYPE);
  if (!file) {
    return WOS_ERR_INVALID_HANDLE;
  }
  long pos = ftell(file);
  return pos < 0 ? WOS_ERR_INTERNAL : (int32_t)pos;
}

static int32_t wasm_fs_unlink(wasm_exec_env_t exec_env, char* path) {
  char resolved[FS_PATH_MAX];
  if (!resolve_path(path, resolved, sizeof(resolved))) {
    return WOS_ERR_INVALID_ARG;
  }
  return unlink(resolved) == 0 ? WOS_OK : WOS_ERR_NOT_FOUND;
}

static uint32_t wasm_fs_opendir(wasm_exec_env_t exec_env, char* path) {
  char resolved[FS_PATH_MAX];
  /* Allow opening the root itself ("" is not a valid file but "/" means root). */
  if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
    strcpy(resolved, FS_ROOT);
  } else if (!resolve_path(path, resolved, sizeof(resolved))) {
    ESP_LOGE(TAG, "Rejected path: %s", path);
    return WOS_HANDLE_INVALID;
  }

  DIR* dir = opendir(resolved);
  if (!dir) {
    return WOS_HANDLE_INVALID;
  }

  wos_handle_t handle = wos_handle_create(&DIR_TYPE, dir);
  if (handle == WOS_HANDLE_INVALID) {
    closedir(dir);
  }
  return handle;
}

/*
 * Read the next directory entry: copies its name into the guest buffer
 * (snprintf-style) and writes the entry type (DT_*) to type_out_aptr.
 * Returns 1 for an entry, 0 at the end of the directory, negative on error.
 */
static int32_t wasm_fs_readdir(wasm_exec_env_t exec_env, uint32_t handle, uint32_t name_buf_aptr, uint32_t name_cap,
                               uint32_t type_out_aptr) {
  DIR* dir = wos_handle_deref(handle, &DIR_TYPE);
  if (!dir) {
    return WOS_ERR_INVALID_HANDLE;
  }

  struct dirent* entry = readdir(dir);
  if (!entry) {
    return 0;
  }

  int32_t copied = wos_guest_copy_out(exec_env, name_buf_aptr, name_cap, entry->d_name, strlen(entry->d_name));
  if (copied < 0) {
    return copied;
  }
  if (type_out_aptr != 0 && !wos_guest_write_u32(exec_env, type_out_aptr, entry->d_type)) {
    return WOS_ERR_BAD_MEMORY;
  }
  return 1;
}

static int32_t wasm_fs_closedir(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &DIR_TYPE);
}

static NativeSymbol k_symbols[] = {
    {"fs_open", wasm_fs_open, "($$)i", NULL},         {"fs_close", wasm_fs_close, "(i)i", NULL},
    {"fs_read", wasm_fs_read, "(iii)i", NULL},        {"fs_write", wasm_fs_write, "(iii)i", NULL},
    {"fs_seek", wasm_fs_seek, "(iii)i", NULL},        {"fs_tell", wasm_fs_tell, "(i)i", NULL},
    {"fs_unlink", wasm_fs_unlink, "($)i", NULL},      {"fs_opendir", wasm_fs_opendir, "($)i", NULL},
    {"fs_readdir", wasm_fs_readdir, "(iiii)i", NULL}, {"fs_closedir", wasm_fs_closedir, "(i)i", NULL},
};

bool wos_register_fs(void) {
  return wasm_runtime_register_natives("fs", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
