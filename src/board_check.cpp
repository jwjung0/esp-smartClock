#include "board_check.h"

#include <Arduino.h>

// Many ESP32 DevKits use GPIO 2 for the onboard blue LED.
static const int LED_PIN = 2;
static const unsigned long BLINK_MS = 1000;

void boardCheckSetup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("smartClock board check OK");
}

void boardCheckLoop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.printf("LED ON, uptime=%lu ms\n", millis());
  delay(BLINK_MS);

  digitalWrite(LED_PIN, LOW);
  Serial.printf("LED OFF, uptime=%lu ms\n", millis());
  delay(BLINK_MS);
}
