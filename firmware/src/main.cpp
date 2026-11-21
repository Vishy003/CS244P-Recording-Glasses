#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include <time.h>
#include "device_secrets.h"

// Kit ESP32 connectivity test only: no camera, microphone, motion sensor,
// voltage divider, recording commands, or fabricated sensor readings.
WiFiClientSecure tls;
PubSubClient mqtt(tls);
char bootId[24];
uint32_t sequenceNumber = 0;
uint32_t lastPublish = 0;
uint32_t retryAt = 0;
uint32_t backoffMs = 2000;
bool firstPublish = true;
uint32_t lastStatus = 0;
bool wifiReported = false;
constexpr uint32_t TEST_INTERVAL_MS = 15000; // Test mode, not final 15-minute schedule.

void setup() {
  Serial.begin(115200);
  delay(500);
  snprintf(bootId, sizeof(bootId), "kit-%08lx-%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  Serial.println("Group18 kit ESP32 telemetry-only test. No camera or sensor data.");
  if (!WIFI_SSID[0] || !WIFI_PASSWORD[0] || !MQTT_HOST[0]) {
    Serial.println("Missing private Wi-Fi/server configuration; firmware stays idle.");
    return;
  }
  WiFi.persistent(false);
  WiFi.setHostname("group18-kit-test");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true); // Clear station credentials without deinitializing the radio.
  delay(300);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("Wi-Fi disconnected; reason code %u.\n", info.wifi_sta_disconnected.reason);
    }
  });
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Powered connectivity test: avoid modem-sleep handshake issues.
  WiFi.setAutoReconnect(true);
  int found = WiFi.scanNetworks();
  bool exactMatch = false;
  int strongestRssi = -128;
  int selectedChannel = 0;
  uint8_t selectedBssid[6] = {};
  unsigned matchingAccessPoints = 0;
  for (int i = 0; i < found; ++i) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      exactMatch = true;
      ++matchingAccessPoints;
      if (WiFi.RSSI(i) > strongestRssi) {
        strongestRssi = WiFi.RSSI(i);
        selectedChannel = WiFi.channel(i);
        memcpy(selectedBssid, WiFi.BSSID(i), sizeof(selectedBssid));
      }
      Serial.printf("Saved network visible at %d dBm; security type %d.\n", WiFi.RSSI(i), WiFi.encryptionType(i));
    }
  }
  Serial.printf("Saved network found in 2.4 GHz scan: %s.\n", exactMatch ? "yes" : "no");
  Serial.printf("Matching access points: %u; selected channel: %d.\n", matchingAccessPoints, selectedChannel);
  WiFi.scanDelete();
  if (exactMatch) {
    // Pin this diagnostic session to the strongest matching AP to avoid roaming
    // or selecting another mesh node during the authentication handshake.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, selectedChannel, selectedBssid);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  tls.setCACert(MQTT_CA);
  mqtt.setServer(MQTT_HOST, 8883);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(10);
}

void loop() {
  delay(20);
  if (!WIFI_SSID[0] || !WIFI_PASSWORD[0] || !MQTT_HOST[0]) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastStatus > 5000) {
      Serial.printf("Waiting for saved Wi-Fi network (status %d).\n", WiFi.status());
      lastStatus = millis();
    }
    wifiReported = false;
    return;
  }
  if (!wifiReported) {
    Serial.print("Wi-Fi connected; ESP32 address: ");
    Serial.println(WiFi.localIP());
    wifiReported = true;
  }
  time_t now = time(nullptr);
  if (now < 1700000000) {
    if (millis() - lastStatus > 5000) {
      Serial.println("Waiting for network time before TLS certificate validation.");
      lastStatus = millis();
    }
    return; // No insecure TLS fallback if time is unknown.
  }
  const uint32_t tick = millis();
  if (!mqtt.connected()) {
    if (static_cast<int32_t>(tick - retryAt) < 0) return;
    if (!mqtt.connect(bootId, "group18-glasses-01", MQTT_PASSWORD)) {
      Serial.printf("MQTT not connected (state %d); retry in %lu ms\n", mqtt.state(),
                    static_cast<unsigned long>(backoffMs));
      retryAt = tick + backoffMs;
      backoffMs = min(backoffMs * 2, static_cast<uint32_t>(60000));
      return;
    }
    backoffMs = 2000;
    Serial.println("Authenticated MQTT over verified TLS connected.");
  }
  mqtt.loop();
  if (!firstPublish && tick - lastPublish < TEST_INTERVAL_MS) return;
  struct tm utc;
  gmtime_r(&now, &utc);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  char payload[600];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"group18-glasses-01\",\"device_model\":\"course-kit-esp32\","
    "\"boot_id\":\"%s\",\"sequence\":%lu,\"timestamp\":\"%s\","
    "\"event\":\"heartbeat\",\"source\":\"kit_esp32_test\",\"uptime_s\":%lu,"
    "\"rssi_dbm\":%ld,\"free_heap_bytes\":%lu,\"reset_reason\":\"%d\"}",
    bootId, static_cast<unsigned long>(sequenceNumber), timestamp,
    static_cast<unsigned long>(millis() / 1000), static_cast<long>(WiFi.RSSI()),
    static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<int>(esp_reset_reason()));
  if (mqtt.publish("grp18/devices/group18-glasses-01/telemetry", payload, false)) {
    Serial.println(payload);
    ++sequenceNumber;
    firstPublish = false;
    lastPublish = tick;
  }
  // PubSubClient publishes at QoS 0. This test does not claim persistent queueing,
  // durable-storage acknowledgements, deduplication, or reliability-run firmware.
}
