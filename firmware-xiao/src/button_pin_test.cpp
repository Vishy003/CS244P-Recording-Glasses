#include <Arduino.h>

// Input-only diagnosis of a normally-open button to GND. No camera/mic/network.
const uint8_t pins[] = {D0, D2, D3, D4, D5, D6, D7, D8, D9, D10};
const char* names[] = {"D0", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10"};
int previous[10];
uint32_t lastReport = 0;

void setup() {
  pinMode(D1, OUTPUT);
  digitalWrite(D1, LOW);
  Serial.begin(115200);
  for (unsigned i = 0; i < 10; ++i) {
    pinMode(pins[i], INPUT_PULLUP);
    previous[i] = digitalRead(pins[i]);
  }
}

void loop() {
  for (unsigned i = 0; i < 10; ++i) {
    const int value = digitalRead(pins[i]);
    if (value != previous[i]) {
      Serial.printf("INPUT CHANGE %s (GPIO%u): %s\n", names[i], pins[i], value ? "HIGH" : "LOW");
      previous[i] = value;
    }
  }
  if (millis() - lastReport >= 1000) {
    lastReport = millis();
    Serial.print("INPUTS (0=LOW, 1=HIGH): ");
    for (unsigned i = 0; i < 10; ++i) Serial.printf("%s=%d ", names[i], digitalRead(pins[i]));
    Serial.println(" | camera/mic OFF, LED OFF");
  }
  delay(10);
}
