#include <Arduino.h>
#include "esp_camera.h"

// Button: D2 -> normally-open switch -> GND. Internal pull-up, active LOW.
// Indicator: D1 -> 330 ohm resistor -> LED anode; cathode -> GND.
// Private-room RAM capture with one USB JPEG preview per session. No microphone, SD, network, or saved video.
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t FRAME_INTERVAL_MS = 100;
bool previewSent = false;
bool capturing = false;
bool fault = false;
bool armed = false;
int rawButton = HIGH;
int stableButton = HIGH;
uint32_t changedAt = 0;
uint32_t lastFrameAt = 0;
uint32_t lastReportAt = 0;
uint32_t frames = 0;
uint32_t failedFrames = 0;

void stopCapture() {
  if (!capturing) return;
  const esp_err_t result = esp_camera_deinit();
  capturing = false;
  if (result == ESP_OK) {
    digitalWrite(D1, LOW);
    Serial.printf("STOPPED: camera off, LED off; %lu frames acquired, %lu failed. One preview offered over USB. No video saved.\n",
                  (unsigned long)frames, (unsigned long)failedFrames);
  } else {
    fault = true;
    Serial.printf("FAULT: camera shutdown 0x%x. LED stays ON; disconnect power.\n", result);
  }
}

void startCapture() {
  if (fault || capturing) return;
  // Camera initialization may begin acquisition immediately, so light comes first.
  digitalWrite(D1, HIGH);
  delay(250);
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = 15; c.pin_d1 = 17; c.pin_d2 = 18; c.pin_d3 = 16;
  c.pin_d4 = 14; c.pin_d5 = 12; c.pin_d6 = 11; c.pin_d7 = 48;
  c.pin_xclk = 10; c.pin_pclk = 13; c.pin_vsync = 38; c.pin_href = 47;
  c.pin_sccb_sda = 40; c.pin_sccb_scl = 39;
  c.pin_pwdn = -1; c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count = 1;
  c.fb_location = CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  const esp_err_t result = esp_camera_init(&c);
  if (result != ESP_OK) {
    fault = true;
    Serial.printf("FAULT: camera initialization 0x%x. LED stays ON in case hardware is active; disconnect power.\n", result);
    return;
  }
  frames = failedFrames = 0;
  previewSent = false;
  lastFrameAt = lastReportAt = millis();
  capturing = true;
  Serial.println("CAPTURING: LED on; frames in RAM, first frame exported as USB preview. Press again to stop. Microphone off.");
}

void setup() {
  pinMode(D1, OUTPUT);
  digitalWrite(D1, LOW);
  pinMode(D2, INPUT_PULLUP);
  Serial.begin(115200);
  rawButton = stableButton = digitalRead(D2);
  changedAt = millis();
  Serial.println("READY: camera off. Release button, then press D2 to start; press again to stop. RAM only; no saved video.");
}

void loop() {
  const uint32_t now = millis();
  const int reading = digitalRead(D2);
  if (reading != rawButton) {
    rawButton = reading;
    changedAt = now;
  }
  if (now - changedAt >= DEBOUNCE_MS) {
    // A button held during reset cannot start capture: require a stable release.
    if (rawButton == HIGH) armed = true;
    if (stableButton != rawButton) {
      stableButton = rawButton;
      if (stableButton == LOW && armed) {
        armed = false;
        if (capturing) stopCapture();
        else startCapture();
      }
    }
  }
  const uint32_t captureNow = millis();
  if (capturing && captureNow - lastFrameAt >= FRAME_INTERVAL_MS) {
    lastFrameAt = captureNow;
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame) {
      ++frames;
      // Export one preview per button-started session over USB.
      if (!previewSent) {
        Serial.printf("PREVIEW_JPEG_BEGIN %u\n", unsigned(frame->len));
        const char hex[] = "0123456789abcdef";
        char line[129];
        for (size_t offset = 0; offset < frame->len; offset += 64) {
          size_t n = min(size_t(64), frame->len - offset);
          for (size_t j = 0; j < n; ++j) {
            line[2*j] = hex[frame->buf[offset+j] >> 4];
            line[2*j+1] = hex[frame->buf[offset+j] & 15];
          }
          line[2*n] = 0;
          Serial.println(line);
        }
        Serial.println("PREVIEW_JPEG_END");
        previewSent = true;
      }
      esp_camera_fb_return(frame);
    } else {
      ++failedFrames;
      Serial.println("Frame acquisition failed; stopping capture.");
      stopCapture();
    }
  }
  if (captureNow - lastReportAt >= 2000) {
    lastReportAt = captureNow;
    Serial.printf("STATE=%s button=%s frames=%lu failed=%lu LED=%s; microphone off; storage not implemented.\n",
                  fault ? "FAULT" : capturing ? "CAPTURING" : "IDLE",
                  rawButton == LOW ? "PRESSED" : "RELEASED", (unsigned long)frames,
                  (unsigned long)failedFrames, digitalRead(D1) ? "ON" : "OFF");
  }
  delay(2);
}
