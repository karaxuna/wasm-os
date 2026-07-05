#include "app_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bindings/bindings.h"
#include "device_config.h"
#include "wasm_alloc.h"
#include "wasm_export.h"

#define APP_WASM_PATH "/littlefs/main.wasm"

#define APP_TASK_STACK_BYTES (6 * 1024)
#ifdef CONFIG_IDF_TARGET_ESP32P4
#define APP_WASM_STACK_BYTES (32 * 1024)
#else
#define APP_WASM_STACK_BYTES (3 * 1024)
#endif
#define APP_WASM_HEAP_BYTES (8 * 1024)
#define APP_SHARED_HEAP_BYTES (8 * 1024)
#define APP_MAX_WASM_THREADS 4

#define APP_TASK_PRIORITY 10
#define APP_STOP_TIMEOUT_MS 10000
#define APP_TASK_DRAIN_TIMEOUT_MS 2000

#ifdef CONFIG_FREERTOS_UNICORE
#define APP_CORE 0
#else
#define APP_CORE 1
#endif

#define BIT_STOP_REQUESTED BIT0
#define BIT_STOPPED BIT1

static const char* TAG = "app_runtime";

static struct {
  SemaphoreHandle_t lock;
  EventGroupHandle_t events;
  TaskHandle_t task;       /* NULL when no app is running */
  wasm_module_inst_t inst; /* published for app_runtime_stop(); guarded by lock */
} s_app;

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

/* Run the module's entry point: the WASI _start function, or exported main. */
static void call_entry(wasm_exec_env_t exec_env, wasm_module_inst_t inst) {
  wasm_function_inst_t func = wasm_runtime_lookup_wasi_start_function(inst);
  if (!func) {
    func = wasm_runtime_lookup_function(inst, "main");
  }
  if (!func) {
    ESP_LOGW(TAG, "Module exports neither _start nor main; nothing to run");
    return;
  }

  if (wasm_func_get_param_count(func, inst) != 0 || wasm_func_get_result_count(func, inst) > 2) {
    ESP_LOGE(TAG, "Entry function must take no parameters");
    return;
  }

  uint32_t results[4] = {0};
  if (!wasm_runtime_call_wasm(exec_env, func, 0, results)) {
    ESP_LOGE(TAG, "App trapped: %s", wasm_runtime_get_exception(inst));
  }
}

static void publish_inst(wasm_module_inst_t inst) {
  xSemaphoreTake(s_app.lock, portMAX_DELAY);
  s_app.inst = inst;
  xSemaphoreGive(s_app.lock);
}

/* Give guest-spawned tasks a chance to trap out and self-clean. */
static void drain_spawned_tasks(void) {
  int waited_ms = 0;
  while (wos_bindings_active_tasks() > 0 && waited_ms < APP_TASK_DRAIN_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(10));
    waited_ms += 10;
  }
}

/*
 * The whole life of one app run, executed on the app task. Every WAMR object
 * is local to this function; nothing else ever frees them.
 */
static void run_app(void) {
  uint8_t* binary = NULL;
  uint32_t binary_size = 0;
  wasm_module_t module = NULL;
  wasm_module_inst_t inst = NULL;
  wasm_exec_env_t exec_env = NULL;
  bool heap_attached = false;
  char error_buf[128];

  RuntimeInitArgs init_args;
  memset(&init_args, 0, sizeof(init_args));
  init_args.mem_alloc_type = Alloc_With_Allocator;
  init_args.mem_alloc_option.allocator.malloc_func = wasm_malloc;
  init_args.mem_alloc_option.allocator.realloc_func = wasm_realloc;
  init_args.mem_alloc_option.allocator.free_func = wasm_free;
  init_args.max_thread_num = APP_MAX_WASM_THREADS;

  if (!wasm_runtime_full_init(&init_args)) {
    ESP_LOGE(TAG, "Failed to initialize WASM runtime");
    return;
  }
  wasm_runtime_set_log_level(WASM_LOG_LEVEL_WARNING);

  if (!wos_register_all_bindings()) {
    ESP_LOGE(TAG, "Failed to register native bindings");
    goto teardown;
  }

  if (read_file(APP_WASM_PATH, &binary, &binary_size) != ESP_OK) {
    ESP_LOGW(TAG, "No app installed. Push one with: wasm-os push ./app.wasm");
    goto teardown;
  }

  LoadArgs load_args = {.wasm_binary_freeable = true};
  module = wasm_runtime_load_ex(binary, binary_size, &load_args, error_buf, sizeof(error_buf));
  if (!module) {
    ESP_LOGE(TAG, "Failed to load module: %s", error_buf);
    goto teardown;
  }

  const device_config_t* config = device_config_get();
  wasm_runtime_set_wasi_args(module, NULL, 0, NULL, 0, (const char**)config->env, config->env_count, NULL, 0);

  if (wasm_runtime_is_underlying_binary_freeable(module)) {
    free(binary);
    binary = NULL;
  }

  inst = wasm_runtime_instantiate(module, APP_WASM_STACK_BYTES, APP_WASM_HEAP_BYTES, error_buf, sizeof(error_buf));
  if (!inst) {
    ESP_LOGE(TAG, "Failed to instantiate module: %s", error_buf);
    goto teardown;
  }
  publish_inst(inst);

  SharedHeapInitArgs heap_args = {.size = APP_SHARED_HEAP_BYTES};
  wasm_shared_heap_t heap = wasm_runtime_create_shared_heap(&heap_args);
  if (!heap || !wasm_runtime_attach_shared_heap(inst, heap)) {
    ESP_LOGE(TAG, "Failed to create/attach shared heap");
    goto teardown;
  }
  heap_attached = true;

  exec_env = wasm_runtime_create_exec_env(inst, APP_WASM_STACK_BYTES);
  if (!exec_env) {
    ESP_LOGE(TAG, "Failed to create exec env");
    goto teardown;
  }

  call_entry(exec_env, inst);

  /*
   * Entry returned (or trapped). Keep the instance alive until a stop is
   * requested: guest-spawned tasks and callbacks may still be running.
   */
  if (!(xEventGroupGetBits(s_app.events) & BIT_STOP_REQUESTED)) {
    ESP_LOGI(TAG, "App entry returned; instance stays live until stop/restart");
    xEventGroupWaitBits(s_app.events, BIT_STOP_REQUESTED, pdFALSE, pdFALSE, portMAX_DELAY);
  }

teardown:
  publish_inst(NULL);

  if (inst) {
    /* Make sure spawned guest tasks trap out promptly, then let them exit. */
    wasm_runtime_terminate(inst);
    drain_spawned_tasks();
  }
  wos_bindings_reset();

  if (exec_env) {
    wasm_runtime_destroy_exec_env(exec_env);
  }
  if (heap_attached) {
    wasm_runtime_detach_shared_heap(inst);
  }
  if (inst) {
    wasm_runtime_deinstantiate(inst);
  }
  if (module) {
    wasm_runtime_unload(module);
  }
  free(binary);
  wasm_runtime_destroy();
}

static void app_task(void* arg) {
  ESP_LOGI(TAG, "App task started on core %d", APP_CORE);
  run_app();

  xSemaphoreTake(s_app.lock, portMAX_DELAY);
  s_app.task = NULL;
  xSemaphoreGive(s_app.lock);

  xEventGroupSetBits(s_app.events, BIT_STOPPED);
  ESP_LOGI(TAG, "App task finished");
  vTaskDelete(NULL);
}

void app_runtime_init(void) {
  s_app.lock = xSemaphoreCreateMutex();
  s_app.events = xEventGroupCreate();
  if (!s_app.lock || !s_app.events) {
    ESP_LOGE(TAG, "Failed to allocate app runtime primitives");
    abort();
  }
}

esp_err_t app_runtime_start(void) {
  xSemaphoreTake(s_app.lock, portMAX_DELAY);
  if (s_app.task) {
    xSemaphoreGive(s_app.lock);
    return ESP_ERR_INVALID_STATE;
  }

  xEventGroupClearBits(s_app.events, BIT_STOP_REQUESTED | BIT_STOPPED);
  BaseType_t created = xTaskCreatePinnedToCore(app_task, "wasm_app", APP_TASK_STACK_BYTES, NULL, APP_TASK_PRIORITY,
                                               &s_app.task, APP_CORE);
  xSemaphoreGive(s_app.lock);

  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create app task");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t app_runtime_stop(void) {
  xSemaphoreTake(s_app.lock, portMAX_DELAY);
  bool running = s_app.task != NULL;
  if (running) {
    xEventGroupSetBits(s_app.events, BIT_STOP_REQUESTED);
    if (s_app.inst) {
      wasm_runtime_terminate(s_app.inst);
    }
  }
  xSemaphoreGive(s_app.lock);

  if (!running) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Stopping app...");
  EventBits_t bits =
      xEventGroupWaitBits(s_app.events, BIT_STOPPED, pdTRUE, pdFALSE, pdMS_TO_TICKS(APP_STOP_TIMEOUT_MS));
  if (!(bits & BIT_STOPPED)) {
    xSemaphoreTake(s_app.lock, portMAX_DELAY);
    TaskHandle_t stuck = s_app.task;
    s_app.task = NULL;
    s_app.inst = NULL;
    xSemaphoreGive(s_app.lock);

    if (stuck) {
      ESP_LOGE(TAG, "App did not stop within %d ms; force-deleting task (resources leak)", APP_STOP_TIMEOUT_MS);
      vTaskDelete(stuck);
    }
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "App stopped");
  return ESP_OK;
}

bool app_runtime_is_running(void) {
  xSemaphoreTake(s_app.lock, portMAX_DELAY);
  bool running = s_app.task != NULL;
  xSemaphoreGive(s_app.lock);
  return running;
}
