#include <Arduino.h>
#include "esp_camera.h"

unsigned capturedFrames = 0;
esp_err_t initResult = ESP_FAIL;
esp_err_t stopResult = ESP_FAIL;

// Finite private-room bench test. No microphone, network, SD, or media output.
// Camera pin map from the installed Arduino CameraWebServer XIAO example.
void setup() {
  pinMode(D1, OUTPUT);
  digitalWrite(D1, LOW);
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 15000) delay(10);
  Serial.println("Camera bench test: five RAM-only frames; microphone never initialized.");
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
  esp_err_t err = esp_camera_init(&c);
  initResult = err;
  if (err != ESP_OK) {
    Serial.printf("Camera initialization FAILED: 0x%x. No successful capture claimed.\n", err);
    // Keep indicator on if partial initialization left camera hardware active.
    Serial.println("LED remains on after initialization error; disconnect USB to power off.");
    return;
  }
  sensor_t* sensor = esp_camera_sensor_get();
  Serial.printf("Camera initialized; sensor PID=0x%04x. LED commanded ON.\n", sensor->id.PID);
  unsigned success = 0;
  for (unsigned i = 1; i <= 5; ++i) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame) {
      Serial.printf("Frame %u: %ux%u, %u bytes, format=%d (RAM only).\n", i,
                    unsigned(frame->width), unsigned(frame->height), unsigned(frame->len), int(frame->format));
      ++success;
      capturedFrames = success;
      esp_camera_fb_return(frame);
    } else Serial.printf("Frame %u FAILED.\n", i);
    delay(1000);
  }
  err = esp_camera_deinit();
  stopResult = err;
  if (err == ESP_OK) {
    digitalWrite(D1, LOW);
    Serial.printf("Test finished: %u/5 frames; camera stopped, LED OFF. No images saved/uploaded.\n", success);
  } else Serial.printf("Camera stop error 0x%x; LED stays ON. Disconnect USB.\n", err);
}

void loop() {
  Serial.printf("Camera test result: init=0x%x, frames=%u/5, stop=0x%x; LED=%s; mic never initialized. No media saved/uploaded.\n",
                initResult, capturedFrames, stopResult, digitalRead(D1) ? "ON" : "OFF");
  delay(2000);
}
