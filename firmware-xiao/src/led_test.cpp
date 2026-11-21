#include <Arduino.h>

// Bench test: D1 -> 330 ohm resistor -> LED anode; LED cathode -> GND.
// No camera, microphone, recording, or network initialization.
void setup() {
  pinMode(D1, OUTPUT);
  digitalWrite(D1, HIGH);
  Serial.begin(115200);
}

void loop() {
  Serial.println("LED BENCH TEST: D1 (GPIO2) commanded HIGH; visually confirm LED is on. Camera/mic off.");
  delay(2000);
}
