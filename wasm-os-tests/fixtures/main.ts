import { println, delay } from "./env";
import { gpioSetDirection, gpioSetLevel, GPIO_MODE_OUTPUT } from "./gpio";

const LED_PIN: u32 = 2;

export function main(): void {
  // Set GPIO 2 as output (built-in LED on many ESP32 boards)
  gpioSetDirection(LED_PIN, GPIO_MODE_OUTPUT);

  println(String.UTF8.encode("wasm-os test app started", true));

  // Blink LED 3 times
  for (let i: u32 = 0; i < 3; i++) {
    gpioSetLevel(LED_PIN, 1);
    delay(500);
    gpioSetLevel(LED_PIN, 0);
    delay(500);
  }

  println(String.UTF8.encode("wasm-os test app done", true));
}
