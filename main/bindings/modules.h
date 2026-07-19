#pragma once

#include <stdbool.h>

/**
 * Internal registry of binding modules. Each module implements its
 * wos_register_* function (registering NativeSymbols with WAMR) and, where it
 * keeps per-app state outside the handle table, a wos_*_reset hook.
 *
 * Including this header from every module .c keeps the definitions in sync
 * with the table in bindings.c.
 */

bool wos_register_callback(void);
bool wos_register_env(void);
bool wos_register_esp_lcd(void);
bool wos_register_fs(void);
bool wos_register_gpio(void);
bool wos_register_http_client(void);
bool wos_register_i2c_master(void);
bool wos_register_i2s_std(void);
bool wos_register_output(void);
bool wos_register_shared_memory(void);
bool wos_register_socket(void);
bool wos_register_spi_master(void);
bool wos_register_storage(void);
bool wos_register_task(void);
bool wos_register_wasi(void);
bool wos_register_websocket(void);

/* Per-app state resets, called from wos_bindings_reset() at app teardown. */
void wos_callbacks_reset(void);
void wos_i2c_master_reset(void);
void wos_shared_memory_reset(void);

/* Spawned guest tasks still running; teardown waits for this to reach 0. */
int wos_tasks_active(void);

/* Force-kill any guest task that refused to exit. Last resort at teardown. */
void wos_tasks_reset(void);
