#include "board_check.h"

#include "bme_sensor.h"
#include "board_pins.h"

#include <Arduino.h>

static const int LED_PIN = BOARD_LED_PIN;
static const unsigned long BLINK_MS = 1000;

void boardCheckSetup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.printf("smartClock board check OK (LED GPIO %d)\n", LED_PIN);
  bmeSensorBegin();
}

void boardCheckLoop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.printf("LED ON, uptime=%lu ms\n", millis());
  bmeSensorPrint();
  delay(BLINK_MS);

  digitalWrite(LED_PIN, LOW);
  Serial.printf("LED OFF, uptime=%lu ms\n", millis());
  bmeSensorPrint();
  delay(BLINK_MS);
}
