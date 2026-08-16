#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "common.h"
#include "handle.h"
#include "modules.h"

static const char* TAG = "wasm_socket";

static void sockaddr_destroy(void* ptr) {
  free(ptr);
}

static void socket_destroy(void* ptr) {
  int fd = (int)(intptr_t)ptr;
  if (close(fd) < 0) {
    ESP_LOGW(TAG, "Failed to close socket fd %d", fd);
  }
}

static const wos_handle_type_t SOCKADDR_TYPE = {.name = "sockaddr", .destroy = sockaddr_destroy};
static const wos_handle_type_t SOCKET_TYPE = {.name = "socket", .destroy = socket_destroy};

/* Sockets are stored as fd values, never dereferenced as pointers. */
#define FD_TO_PTR(fd) ((void*)(intptr_t)((fd) + 1)) /* +1 so fd 0 is not NULL */
#define PTR_TO_FD(ptr) ((int)(intptr_t)(ptr) - 1)

static uint32_t wasm_socketaddr_create(wasm_exec_env_t exec_env) {
  struct sockaddr_in* addr = calloc(1, sizeof(struct sockaddr_in));
  if (!addr) {
    return WOS_HANDLE_INVALID;
  }

  wos_handle_t handle = wos_handle_create(&SOCKADDR_TYPE, addr);
  if (handle == WOS_HANDLE_INVALID) {
    free(addr);
  }
  return handle;
}

static int32_t wasm_socketaddr_set_family(wasm_exec_env_t exec_env, uint32_t handle, int32_t family) {
  struct sockaddr_in* addr = wos_handle_deref(handle, &SOCKADDR_TYPE);
  if (!addr) {
    return WOS_ERR_INVALID_HANDLE;
  }
  addr->sin_family = family;
  return WOS_OK;
}

static int32_t wasm_socketaddr_set_port(wasm_exec_env_t exec_env, uint32_t handle, int32_t port) {
  struct sockaddr_in* addr = wos_handle_deref(handle, &SOCKADDR_TYPE);
  if (!addr) {
    return WOS_ERR_INVALID_HANDLE;
  }
  if (port < 0 || port > UINT16_MAX) {
    return WOS_ERR_INVALID_ARG;
  }
  addr->sin_port = htons((uint16_t)port);
  return WOS_OK;
}

/* Resolves `host` (name or dotted quad) into the address. Blocking DNS. */
static int32_t wasm_socketaddr_set_host(wasm_exec_env_t exec_env, uint32_t handle, char* host) {
  struct sockaddr_in* addr = wos_handle_deref(handle, &SOCKADDR_TYPE);
  if (!addr) {
    return WOS_ERR_INVALID_HANDLE;
  }

  struct hostent* he = gethostbyname(host);
  if (!he || !he->h_addr_list[0]) {
    ESP_LOGE(TAG, "Failed to resolve host: %s", host);
    return WOS_ERR_NOT_FOUND;
  }

  memcpy(&addr->sin_addr, he->h_addr_list[0], sizeof(addr->sin_addr));
  return WOS_OK;
}

static int32_t wasm_socketaddr_destroy(wasm_exec_env_t exec_env, uint32_t handle) {
  return wos_handle_destroy(handle, &SOCKADDR_TYPE);
}

static uint32_t wasm_socket_create(wasm_exec_env_t exec_env, int32_t domain, int32_t type, int32_t protocol) {
  int fd = socket(domain, type, protocol);
  if (fd < 0) {
    ESP_LOGE(TAG, "socket() failed: errno %d", errno);
    return WOS_HANDLE_INVALID;
  }

  wos_handle_t handle = wos_handle_create(&SOCKET_TYPE, FD_TO_PTR(fd));
  if (handle == WOS_HANDLE_INVALID) {
    close(fd);
  }
  return handle;
}

static int32_t wasm_socket_connect(wasm_exec_env_t exec_env, uint32_t sock_handle, uint32_t addr_handle) {
  void* fd_ptr = wos_handle_deref(sock_handle, &SOCKET_TYPE);
  struct sockaddr_in* addr = wos_handle_deref(addr_handle, &SOCKADDR_TYPE);
  if (!fd_ptr || !addr) {
    return WOS_ERR_INVALID_HANDLE;
  }

  if (connect(PTR_TO_FD(fd_ptr), (struct sockaddr*)addr, sizeof(*addr)) < 0) {
    ESP_LOGE(TAG, "connect() failed: errno %d (%s)", errno, strerror(errno));
    return WOS_ERR_INTERNAL;
  }
  return WOS_OK;
}

static int32_t wasm_socket_send(wasm_exec_env_t exec_env, uint32_t sock_handle, uint32_t buf_aptr, uint32_t len) {
  void* fd_ptr = wos_handle_deref(sock_handle, &SOCKET_TYPE);
  if (!fd_ptr) {
    return WOS_ERR_INVALID_HANDLE;
  }
  const void* buf = wos_guest_ptr(exec_env, buf_aptr, len);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }

  int sent = send(PTR_TO_FD(fd_ptr), buf, len, 0);
  return sent < 0 ? WOS_ERR_INTERNAL : sent;
}

static int32_t wasm_socket_recv(wasm_exec_env_t exec_env, uint32_t sock_handle, uint32_t buf_aptr, uint32_t len) {
  void* fd_ptr = wos_handle_deref(sock_handle, &SOCKET_TYPE);
  if (!fd_ptr) {
    return WOS_ERR_INVALID_HANDLE;
  }
  void* buf = wos_guest_ptr(exec_env, buf_aptr, len);
  if (!buf) {
    return WOS_ERR_BAD_MEMORY;
  }

  int received = recv(PTR_TO_FD(fd_ptr), buf, len, 0);
  return received < 0 ? WOS_ERR_INTERNAL : received;
}

static int32_t wasm_socket_close(wasm_exec_env_t exec_env, uint32_t sock_handle) {
  return wos_handle_destroy(sock_handle, &SOCKET_TYPE);
}

static NativeSymbol k_symbols[] = {
    {"socketaddr_create", wasm_socketaddr_create, "()i", NULL},
    {"socketaddr_set_family", wasm_socketaddr_set_family, "(ii)i", NULL},
    {"socketaddr_set_port", wasm_socketaddr_set_port, "(ii)i", NULL},
    {"socketaddr_set_host", wasm_socketaddr_set_host, "(i$)i", NULL},
    {"socketaddr_destroy", wasm_socketaddr_destroy, "(i)i", NULL},
    {"socket_create", wasm_socket_create, "(iii)i", NULL},
    {"socket_connect", wasm_socket_connect, "(ii)i", NULL},
    {"socket_send", wasm_socket_send, "(iii)i", NULL},
    {"socket_recv", wasm_socket_recv, "(iii)i", NULL},
    {"socket_close", wasm_socket_close, "(i)i", NULL},
};

bool wos_register_socket(void) {
  return wasm_runtime_register_natives("socket", k_symbols, sizeof(k_symbols) / sizeof(k_symbols[0]));
}
