// Simulated supervisor for tests/supervisor.test.js: runs as main.wasm and
// exercises the app binding against two child fixtures.
//
// Serial markers, in order:
//   SUP_STARTED
//   CHILD_CRASH_DETECTED / CHILD_ERROR <trap> / CHILD_RECLAIMED
//   SUP_ALIVE <n>            (heartbeats while child-spin monopolizes its prio)
//   SPIN_STOP rc=<rc> status=<state>
//   SUP_DONE
import { println, delay, millis } from "./env";
import { appStart, appStop, appStatus, appLastError, APP_STATE_CRASHED, APP_STATE_STOPPED } from "./app";

const CRASH_DETECT_TIMEOUT_MS: u32 = 10000;

function s(text: string): ArrayBuffer {
  return String.UTF8.encode(text, true);
}

function lastError(): string {
  const buf = new ArrayBuffer(128);
  const len = appLastError(<u32>changetype<usize>(buf), 128);
  if (len <= 0) {
    return "";
  }
  return String.UTF8.decodeUnsafe(changetype<usize>(buf), len < 128 ? len : 127);
}

export function main(): void {
  println(s("SUP_STARTED"));

  // 1. Crash detection: start a child that traps, watch it become CRASHED.
  let rc = appStart(s("child-crash.wasm"));
  if (rc != 0) {
    println(s("SUP_FAIL start-crash rc=" + rc.toString()));
    return;
  }

  const deadline = millis() + CRASH_DETECT_TIMEOUT_MS;
  while (appStatus() != APP_STATE_CRASHED && millis() < deadline) {
    delay(100);
  }
  if (appStatus() != APP_STATE_CRASHED) {
    println(s("SUP_FAIL no-crash status=" + appStatus().toString()));
    return;
  }
  println(s("CHILD_CRASH_DETECTED"));
  println(s("CHILD_ERROR " + lastError()));

  rc = appStop(2000);
  if (rc != 0 || appStatus() != APP_STATE_STOPPED) {
    println(s("SUP_FAIL reclaim rc=" + rc.toString()));
    return;
  }
  println(s("CHILD_RECLAIMED"));

  // 2. Spin control: a child that never yields must not starve this task,
  // and must be force-reclaimable. Total spin time stays under the 5 s idle
  // watchdog: 4 x 250 ms heartbeats + a 2.5 s stop timeout.
  rc = appStart(s("child-spin.wasm"));
  if (rc != 0) {
    println(s("SUP_FAIL start-spin rc=" + rc.toString()));
    return;
  }

  for (let i = 0; i < 4; i++) {
    delay(250);
    println(s("SUP_ALIVE " + i.toString()));
  }

  rc = appStop(2500);
  println(s("SPIN_STOP rc=" + rc.toString() + " status=" + appStatus().toString()));

  println(s("SUP_DONE"));
}
