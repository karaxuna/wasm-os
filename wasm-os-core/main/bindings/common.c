#include "common.h"

#include <string.h>

int32_t wos_err(esp_err_t err) {
  if (err == ESP_OK) {
    return WOS_OK;
  }
  if (err > 0) {
    return -(int32_t)err;
  }
  /* ESP_FAIL and other negative codes carry no detail worth forwarding. */
  return WOS_ERR_INTERNAL;
}

void* wos_guest_ptr(wasm_exec_env_t exec_env, uint64_t addr, uint64_t len) {
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
  if (!inst || addr == 0) {
    return NULL;
  }
  if (!wasm_runtime_validate_app_addr(inst, addr, len)) {
    return NULL;
  }
  return wasm_runtime_addr_app_to_native(inst, addr);
}

bool wos_guest_write_u32(wasm_exec_env_t exec_env, uint64_t addr, uint32_t value) {
  uint32_t* out = wos_guest_ptr(exec_env, addr, sizeof(uint32_t));
  if (!out) {
    return false;
  }
  *out = value;
  return true;
}

int32_t wos_guest_copy_out(wasm_exec_env_t exec_env, uint64_t buf_addr, uint32_t buf_cap, const void* src,
                           uint32_t src_len) {
  if (buf_cap >= src_len + 1) {
    char* out = wos_guest_ptr(exec_env, buf_addr, buf_cap);
    if (!out) {
      return WOS_ERR_BAD_MEMORY;
    }
    memcpy(out, src, src_len);
    out[src_len] = '\0';
  }
  return (int32_t)src_len;
}
