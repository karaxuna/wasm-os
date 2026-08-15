/**
 * Fetches button definitions over HTTP. For this demo the guest POSTs a test
 * array of {label, id} objects to an echo endpoint (postman-echo reflects the
 * posted JSON back under its "json" key), then parses that array out of the
 * response with jsmn and hands it to the UI. Swapping in a real endpoint that
 * returns Array<{label, id}> requires no other change.
 */
#include "buttons.h"

#include <string.h>

#include "cJSON.h"

#include "wasm_os.h"

#define ECHO_URL "http://postman-echo.com/post"

/* The test payload the demo sends; the echo server returns it verbatim. */
static const char REQUEST_BODY[] =
    "[{\"label\":\"Living Room\",\"id\":\"lr-1\"},"
    "{\"label\":\"Kitchen\",\"id\":\"kitchen-2\"},"
    "{\"label\":\"Garage\",\"id\":\"garage-3\"}]";

static char s_resp[8192];

/* POST REQUEST_BODY to ECHO_URL; returns response length in s_resp or <0. */
static int http_post(void) {
  uint32_t cfg = wos_http_client_config_create();
  if (!cfg) {
    return -1;
  }
  wos_http_client_config_set_url(cfg, ECHO_URL);
  wos_http_client_config_set_method(cfg, WOS_HTTP_METHOD_POST);
  wos_http_client_config_set_timeout_ms(cfg, 15000);
  uint32_t client = wos_http_client_init(cfg);
  wos_http_client_config_destroy(cfg);
  if (!client) {
    return -2;
  }

  int result = -3;
  int body_len = (int)(sizeof(REQUEST_BODY) - 1);
  wos_http_client_set_header(client, "Content-Type", "application/json");
  if (wos_http_client_open(client, body_len) < 0) {
    goto done;
  }
  if (wos_http_client_write(client, REQUEST_BODY, body_len) < 0) {
    goto done;
  }
  if (wos_http_client_fetch_headers(client) < 0) {
    goto done;
  }
  if (wos_http_client_get_status_code(client) != 200) {
    goto done;
  }

  int total = 0;
  for (;;) {
    int n = wos_http_client_read_response(client, s_resp + total, sizeof(s_resp) - 1 - total);
    if (n < 0) {
      goto done;
    }
    if (n == 0) {
      break;
    }
    total += n;
    if (total >= (int)sizeof(s_resp) - 1) {
      break;
    }
  }
  s_resp[total] = '\0';
  result = total;

done:
  wos_http_client_cleanup(client);
  return result;
}

/* Copy a cJSON string value into dst (NUL-terminated, truncated to cap). */
static void copy_str(const cJSON* item, char* dst, int cap) {
  dst[0] = '\0';
  if (cJSON_IsString(item) && item->valuestring) {
    strncpy(dst, item->valuestring, cap - 1);
    dst[cap - 1] = '\0';
  }
}

int buttons_fetch(button_data_t* out, int max) {
  int resp_len = http_post();
  if (resp_len < 0) {
    return resp_len;
  }

  cJSON* root = cJSON_ParseWithLength(s_resp, resp_len);
  if (!root) {
    return -10;
  }

  /* The echo endpoint reflects the posted array under "json"; a real
   * endpoint returning the array directly parses as the array itself. */
  cJSON* array = cJSON_GetObjectItemCaseSensitive(root, "json");
  if (!cJSON_IsArray(array)) {
    array = root;
  }
  if (!cJSON_IsArray(array)) {
    cJSON_Delete(root);
    return -11;
  }

  int count = 0;
  const cJSON* elem = NULL;
  cJSON_ArrayForEach(elem, array) {
    if (count >= max) {
      break;
    }
    const cJSON* label = cJSON_GetObjectItemCaseSensitive(elem, "label");
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(elem, "id");
    if (cJSON_IsString(label)) {
      copy_str(label, out[count].label, BUTTON_LABEL_MAX);
      copy_str(id, out[count].id, BUTTON_ID_MAX);
      count++;
    }
  }

  cJSON_Delete(root);
  return count;
}
