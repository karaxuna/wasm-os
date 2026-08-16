// Child fixture: verifies the app-binding policy check from the child slot,
// then traps, so the supervisor sees state CRASHED.
import { println } from "./env";
import { appStart } from "./app";

function s(text: string): ArrayBuffer {
  return String.UTF8.encode(text, true);
}

export function main(): void {
  println(s("CHILD_CRASH_RUNNING"));

  // Management from the child slot must be rejected with -7.
  const rc = appStart(s("anything.wasm"));
  println(s("CHILD_POLICY rc=" + rc.toString()));

  unreachable();
}
