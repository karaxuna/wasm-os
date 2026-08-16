// Reads WIFI_SSID/WIFI_PASS from the app environment (/littlefs/.env),
// connects through the "wifi" binding, and makes an HTTP request to the web.
//
// Serial markers checked by wifi.test.js:
//   WIFI_CONNECTED <ip>
//   HTTP_STATUS <code>
//   wifi test app done
import { println, getenv } from "./env";
import { wifiConnect, wifiWait, wifiState, wifiIp } from "./wifi";
import {
  httpClientConfigCreate,
  httpClientConfigSetUrl,
  httpClientConfigSetTimeoutMs,
  httpClientConfigDestroy,
  httpClientInit,
  httpClientPerform,
  httpClientCleanup,
} from "./http_client";

const URL = "http://example.com/";
const WIFI_TIMEOUT_MS: i32 = 30000;

function s(text: string): ArrayBuffer {
  return String.UTF8.encode(text, true);
}

function readEnv(name: string): string | null {
  const buf = new ArrayBuffer(64);
  const len = getenv(s(name), <u32>changetype<usize>(buf), 64);
  if (len < 0 || len >= 64) {
    return null;
  }
  return String.UTF8.decodeUnsafe(changetype<usize>(buf), len);
}

export function main(): void {
  println(s("wifi test app started"));

  const ssid = readEnv("WIFI_SSID");
  const pass = readEnv("WIFI_PASS");
  if (ssid == null) {
    println(s("WIFI_SSID_NOT_SET"));
    return;
  }

  let rc = wifiConnect(s(ssid), s(pass == null ? "" : pass));
  if (rc != 0) {
    println(s("WIFI_CONNECT_FAILED " + rc.toString()));
    return;
  }

  rc = wifiWait(WIFI_TIMEOUT_MS);
  if (rc != 0) {
    println(s("WIFI_WAIT_FAILED " + rc.toString() + " state=" + wifiState().toString()));
    return;
  }

  const ipBuf = new ArrayBuffer(16);
  const ipLen = wifiIp(<u32>changetype<usize>(ipBuf), 16);

  let ip = "?";
  if (ipLen > 0) {
    ip = String.UTF8.decodeUnsafe(changetype<usize>(ipBuf), ipLen);
  }
  println(s("WIFI_CONNECTED " + ip));

  const config = httpClientConfigCreate();
  if (config == 0) {
    println(s("HTTP_CONFIG_FAILED"));
    return;
  }
  httpClientConfigSetUrl(config, s(URL));
  httpClientConfigSetTimeoutMs(config, 15000);

  const client = httpClientInit(config);
  httpClientConfigDestroy(config);
  if (client == 0) {
    println(s("HTTP_INIT_FAILED"));
    return;
  }

  const status = httpClientPerform(client);
  httpClientCleanup(client);
  println(s("HTTP_STATUS " + status.toString()));

  println(s("wifi test app done"));
}
