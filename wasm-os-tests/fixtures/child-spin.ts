// Child fixture: spins forever without yielding. The supervisor must stay
// scheduled above it and be able to force-reclaim the slot.
import { println } from "./env";

function s(text: string): ArrayBuffer {
  return String.UTF8.encode(text, true);
}

export function main(): void {
  println(s("CHILD_SPIN_RUNNING"));

  let x: u32 = 0;
  while (true) {
    x = x + 1;
  }
}
