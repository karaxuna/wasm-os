// Deliberately hostile app for the priority regression test: it never yields,
// never calls a host function, and never returns. The serial handler must stay
// reachable anyway — see main/priorities.h.
import { println } from "./env";

// Mutated so the loop cannot be optimised away.
let spin: u64 = 0;

export function main(): void {
  println(String.UTF8.encode("hostile app spinning", true));

  while (true) {
    spin = spin + 1;
  }
}
