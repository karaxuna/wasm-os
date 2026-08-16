#include "app_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bindings/bindings.h"
#include "device_config.h"
#include "priorities.h"
#include "wasm_alloc.h"
#include "wasm_export.h"

#define APP_WASM_PATH "/littlefs/main.wasm"

#define APP_TASK_STACK_BYTES (6 * 1024)
#ifdef CONFIG_IDF_TARGET_ESP32P4
#define APP_WASM_STACK_BYTES (32 * 1024)
#else
/* GUI apps (LVGL) have deep call chains, so this must be well above the
 * original 3 KB — but it is also the last big allocation on a nearly full
 * heap on PSRAM-less boards, so it cannot be extravagant either. */
#define APP_WASM_STACK_BYTES (12 * 1024)
#endif
/* WAMR's module heap backs wasm_runtime_module_malloc, which no binding
 * uses; 0 keeps the linear-memory allocation as small (and as likely to fit
 * a fragmented heap) as possible. */
#define APP_WASM_HEAP_BYTES 0

/* One 64 KB wasm page plus allocator/metadata headroom; held during load,
 * released right before instantiation claims it for linear memory. */
#define APP_LINEAR_RESERVE_BYTES (66 * 1024)
/* Shared across both slots: spawned exec envs count against it too. */
#define APP_MAX_WASM_THREADS 8

#define APP_STOP_TIMEOUT_MS 10000
#define APP_TASK_DRAIN_TIMEOUT_MS 2000
#define APP_PATH_MAX 96
#define APP_ERROR_MAX 128

#ifdef CONFIG_FREERTOS_UNICORE
#define APP_CORE 0
#else
#define APP_CORE 1
#endif

#define BIT_STOP_REQUESTED BIT0
#define BIT_STOPPED BIT1

static const char* TAG = "app_runtime";

typedef struct {
  const char* name;
  UBaseType_t priority;
  SemaphoreHandle_t lock;
  EventGroupHandle_t events;
  TaskHandle_t task;       /* NULL when the slot is not running */
  wasm_module_inst_t inst; /* published for app_runtime_stop(); guarded by lock */
  app_state_t state;       /* guarded by lock */
  char path[APP_PATH_MAX];
  char last_error[APP_ERROR_MAX];
} app_slot_t;

static app_slot_t s_slots[WOS_SLOT_COUNT] = {
    [WOS_SLOT_MAIN] = {.name = "wasm_main", .priority = WOS_PRIO_APP_MAIN},
    [WOS_SLOT_CHILD] = {.name = "wasm_child", .priority = WOS_PRIO_APP_CHILD},
};

/* WAMR's load/instantiate paths touch runtime-global structures; serialize
 * them so the two slots never race inside the runtime. */
static SemaphoreHandle_t s_wamr_lock;

static void set_state(app_slot_t* slot, app_state_t state) {
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  slot->state = state;
  xSemaphoreGive(slot->lock);
}

static esp_err_t read_file(const char* path, uint8_t** out_buf, uint32_t* out_size) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    return ESP_ERR_NOT_FOUND;
  }

  long size = 0;
  if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    ESP_LOGE(TAG, "%s is empty or unreadable", path);
    fclose(file);
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t* buf = malloc(size);
  if (!buf) {
    ESP_LOGE(TAG, "Failed to allocate %ld bytes for %s", size, path);
    fclose(file);
    return ESP_ERR_NO_MEM;
  }

  size_t read = fread(buf, 1, size, file);
  fclose(file);
  if (read != (size_t)size) {
    ESP_LOGE(TAG, "Short read on %s: %u of %ld bytes", path, (unsigned)read, size);
    free(buf);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Loaded %s (%ld bytes)", path, size);
  *out_buf = buf;
  *out_size = (uint32_t)size;
  return ESP_OK;
}

/* Run the module's entry point: the WASI _start function, or exported main.
 * Returns false when the entry trapped; the exception is in `slot->last_error`. */
static bool call_entry(app_slot_t* slot, wasm_exec_env_t exec_env, wasm_module_inst_t inst) {
  wasm_function_inst_t func = wasm_runtime_lookup_wasi_start_function(inst);
  if (!func) {
    func = wasm_runtime_lookup_function(inst, "main");
  }
  if (!func) {
    ESP_LOGW(TAG, "%s: module exports neither _start nor main; nothing to run", slot->name);
    return true;
  }

  if (wasm_func_get_param_count(func, inst) != 0 || wasm_func_get_result_count(func, inst) > 2) {
    ESP_LOGE(TAG, "Entry function must take no parameters");
    return true;
  }

  uint32_t results[4] = {0};
  if (!wasm_runtime_call_wasm(exec_env, func, 0, results)) {
    const char* exception = wasm_runtime_get_exception(inst);
    ESP_LOGE(TAG, "%s trapped: %s", slot->name, exception ? exception : "(no exception)");
    xSemaphoreTake(slot->lock, portMAX_DELAY);
    snprintf(slot->last_error, sizeof(slot->last_error), "%s", exception ? exception : "trap");
    xSemaphoreGive(slot->lock);
    return false;
  }
  return true;
}

static void publish_inst(app_slot_t* slot, wasm_module_inst_t inst) {
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  slot->inst = inst;
  xSemaphoreGive(slot->lock);
}

/* Give the slot's guest-spawned tasks a chance to trap out and self-clean. */
static void drain_spawned_tasks(wos_slot_t slot_id) {
  int waited_ms = 0;
  while (wos_bindings_active_tasks(slot_id) > 0 && waited_ms < APP_TASK_DRAIN_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(10));
    waited_ms += 10;
  }
}

/*
 * The whole life of one app run, executed on the slot's task. Every WAMR
 * object below is local to this function; nothing else ever frees them.
 */
static void run_app(wos_slot_t slot_id) {
  app_slot_t* slot = &s_slots[slot_id];
  uint8_t* binary = NULL;
  uint32_t binary_size = 0;
  wasm_module_t module = NULL;
  wasm_module_inst_t inst = NULL;
  wasm_exec_env_t exec_env = NULL;
  void* linear_reserve = NULL;
  bool crashed = false;
  char error_buf[128];

  if (read_file(slot->path, &binary, &binary_size) != ESP_OK) {
    if (slot_id == WOS_SLOT_MAIN) {
      ESP_LOGW(TAG, "No app installed. Push one with: wasm-os push ./app.wasm");
    } else {
      ESP_LOGW(TAG, "%s: cannot read %s", slot->name, slot->path);
    }
    goto teardown;
  }

  /*
   * Instantiation needs one contiguous block for the app's linear memory
   * (one 64 KB wasm page + allocator overhead). On a fragmented heap the
   * loader's many small allocations would otherwise nibble away the only
   * block that large, so reserve it now and hand it back just before
   * wasm_runtime_instantiate. Best effort: NULL simply skips the insurance.
   */
  linear_reserve = malloc(APP_LINEAR_RESERVE_BYTES);

  xSemaphoreTake(s_wamr_lock, portMAX_DELAY);

  LoadArgs load_args = {.wasm_binary_freeable = true};
  module = wasm_runtime_load_ex(binary, binary_size, &load_args, error_buf, sizeof(error_buf));
  if (!module) {
    xSemaphoreGive(s_wamr_lock);
    ESP_LOGE(TAG, "Failed to load module: %s", error_buf);
    goto teardown;
  }

  const device_config_t* config = device_config_get();
  wasm_runtime_set_wasi_args(module, NULL, 0, NULL, 0, (const char**)config->env, config->env_count, NULL, 0);

  if (wasm_runtime_is_underlying_binary_freeable(module)) {
    free(binary);
    binary = NULL;
  }

  free(linear_reserve);
  linear_reserve = NULL;

  inst = wasm_runtime_instantiate(module, APP_WASM_STACK_BYTES, APP_WASM_HEAP_BYTES, error_buf, sizeof(error_buf));
  xSemaphoreGive(s_wamr_lock);
  if (!inst) {
    ESP_LOGE(TAG, "Failed to instantiate module: %s (%u free, largest block %u)", error_buf,
             heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    goto teardown;
  }
  publish_inst(slot, inst);

  exec_env = wasm_runtime_create_exec_env(inst, APP_WASM_STACK_BYTES);
  if (!exec_env) {
    ESP_LOGE(TAG, "Failed to create exec env");
    goto teardown;
  }

  set_state(slot, APP_STATE_RUNNING);
  crashed = !call_entry(slot, exec_env, inst);

  /*
   * Entry returned (or trapped). Keep the instance alive until a stop is
   * requested: guest-spawned tasks and callbacks may still be running, and
   * a crashed child stays visible as CRASHED until its manager reclaims it.
   */
  if (!(xEventGroupGetBits(slot->events) & BIT_STOP_REQUESTED)) {
    if (crashed) {
      set_state(slot, APP_STATE_CRASHED);
    } else {
      ESP_LOGI(TAG, "%s entry returned; instance stays live until stop/restart", slot->name);
    }
    xEventGroupWaitBits(slot->events, BIT_STOP_REQUESTED, pdFALSE, pdFALSE, portMAX_DELAY);
  }

teardown:
  set_state(slot, APP_STATE_STOPPING);
  publish_inst(slot, NULL);

  if (inst) {
    /* Make sure spawned guest tasks trap out promptly, then let them exit. */
    wasm_runtime_terminate(inst);
    drain_spawned_tasks(slot_id);
  }
  wos_bindings_reset_slot(slot_id);

  if (exec_env) {
    wasm_runtime_destroy_exec_env(exec_env);
  }
  xSemaphoreTake(s_wamr_lock, portMAX_DELAY);
  if (inst) {
    wasm_runtime_deinstantiate(inst);
  }
  if (module) {
    wasm_runtime_unload(module);
  }
  xSemaphoreGive(s_wamr_lock);
  free(linear_reserve);
  free(binary);

  /* The runtime persists across runs, so leaks accumulate; make them visible. */
  ESP_LOGI(TAG, "%s teardown done: heap %u free, largest block %u", slot->name,
           heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void app_task(void* arg) {
  wos_slot_t slot_id = (wos_slot_t)(uintptr_t)arg;
  app_slot_t* slot = &s_slots[slot_id];

  ESP_LOGI(TAG, "%s task started on core %d", slot->name, APP_CORE);
  wos_owner_bind(xTaskGetCurrentTaskHandle(), slot_id);
  run_app(slot_id);
  wos_owner_unbind(xTaskGetCurrentTaskHandle());

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  slot->task = NULL;
  slot->state = APP_STATE_STOPPED;
  xSemaphoreGive(slot->lock);

  xEventGroupSetBits(slot->events, BIT_STOPPED);
  ESP_LOGI(TAG, "%s task finished", slot->name);
  vTaskDelete(NULL);
}

void app_runtime_init(void) {
  s_wamr_lock = xSemaphoreCreateMutex();
  for (int i = 0; i < WOS_SLOT_COUNT; i++) {
    s_slots[i].lock = xSemaphoreCreateMutex();
    s_slots[i].events = xEventGroupCreate();
    if (!s_slots[i].lock || !s_slots[i].events) {
      ESP_LOGE(TAG, "Failed to allocate app runtime primitives");
      abort();
    }
  }
  if (!s_wamr_lock) {
    abort();
  }

  RuntimeInitArgs init_args;
  memset(&init_args, 0, sizeof(init_args));
  init_args.mem_alloc_type = Alloc_With_Allocator;
  init_args.mem_alloc_option.allocator.malloc_func = wasm_malloc;
  init_args.mem_alloc_option.allocator.realloc_func = wasm_realloc;
  init_args.mem_alloc_option.allocator.free_func = wasm_free;
  init_args.max_thread_num = APP_MAX_WASM_THREADS;

  if (!wasm_runtime_full_init(&init_args)) {
    ESP_LOGE(TAG, "Failed to initialize WASM runtime");
    abort();
  }
  wasm_runtime_set_log_level(WASM_LOG_LEVEL_WARNING);

  if (!wos_register_all_bindings()) {
    ESP_LOGE(TAG, "Failed to register native bindings");
    abort();
  }
}

esp_err_t app_runtime_start(wos_slot_t slot_id, const char* path) {
  if (slot_id >= WOS_SLOT_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }
  app_slot_t* slot = &s_slots[slot_id];

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  if (slot->task) {
    xSemaphoreGive(slot->lock);
    return ESP_ERR_INVALID_STATE;
  }
  snprintf(slot->path, sizeof(slot->path), "%s", path ? path : APP_WASM_PATH);
  slot->last_error[0] = '\0';
  slot->state = APP_STATE_STARTING;

  xEventGroupClearBits(slot->events, BIT_STOP_REQUESTED | BIT_STOPPED);
  BaseType_t created = xTaskCreatePinnedToCore(app_task, slot->name, APP_TASK_STACK_BYTES,
                                               (void*)(uintptr_t)slot_id, slot->priority, &slot->task, APP_CORE);
  if (created != pdPASS) {
    slot->state = APP_STATE_STOPPED;
    xSemaphoreGive(slot->lock);
    ESP_LOGE(TAG, "Failed to create %s task", slot->name);
    return ESP_ERR_NO_MEM;
  }
  xSemaphoreGive(slot->lock);
  return ESP_OK;
}

esp_err_t app_runtime_stop(wos_slot_t slot_id, uint32_t timeout_ms) {
  if (slot_id >= WOS_SLOT_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }
  app_slot_t* slot = &s_slots[slot_id];
  if (timeout_ms == 0) {
    timeout_ms = APP_STOP_TIMEOUT_MS;
  }

  xSemaphoreTake(slot->lock, portMAX_DELAY);
  bool running = slot->task != NULL;
  if (running) {
    xEventGroupSetBits(slot->events, BIT_STOP_REQUESTED);
    if (slot->inst) {
      wasm_runtime_terminate(slot->inst);
    }
  }
  xSemaphoreGive(slot->lock);

  if (!running) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Stopping %s...", slot->name);
  EventBits_t bits = xEventGroupWaitBits(slot->events, BIT_STOPPED, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
  if (!(bits & BIT_STOPPED)) {
    xSemaphoreTake(slot->lock, portMAX_DELAY);
    TaskHandle_t stuck = slot->task;
    slot->task = NULL;
    slot->inst = NULL;
    slot->state = APP_STATE_STOPPED;
    snprintf(slot->last_error, sizeof(slot->last_error), "did not stop within %u ms", (unsigned)timeout_ms);
    xSemaphoreGive(slot->lock);

    if (stuck) {
      ESP_LOGE(TAG, "%s did not stop within %u ms; force-deleting task (WAMR objects leak)", slot->name,
               (unsigned)timeout_ms);
      vTaskDelete(stuck);
      wos_owner_unbind(stuck);
      /* Best effort: a task stuck in guest code holds no binding locks, so
       * reclaiming its binding state is safe; the instance/module leak. */
      wos_bindings_reset_slot(slot_id);
    }
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "%s stopped", slot->name);
  return ESP_OK;
}

app_state_t app_runtime_state(wos_slot_t slot_id) {
  if (slot_id >= WOS_SLOT_COUNT) {
    return APP_STATE_STOPPED;
  }
  app_slot_t* slot = &s_slots[slot_id];
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  app_state_t state = slot->state;
  xSemaphoreGive(slot->lock);
  return state;
}

bool app_runtime_is_running(wos_slot_t slot_id) {
  return app_runtime_state(slot_id) != APP_STATE_STOPPED;
}

int32_t app_runtime_last_error(wos_slot_t slot_id, char* buf, size_t cap) {
  if (slot_id >= WOS_SLOT_COUNT || !buf) {
    return 0;
  }
  app_slot_t* slot = &s_slots[slot_id];
  xSemaphoreTake(slot->lock, portMAX_DELAY);
  int32_t len = (int32_t)strlen(slot->last_error);
  if (cap > 0) {
    snprintf(buf, cap, "%s", slot->last_error);
  }
  xSemaphoreGive(slot->lock);
  return len;
}
