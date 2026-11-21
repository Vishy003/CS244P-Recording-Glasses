#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include <time.h>
#include <sys/time.h>
#if __has_include("device_secrets.h")
#include "device_secrets.h"
#else
#include "device_secrets.example.h"
#endif

// Corrected user-confirmed wiring: VCC->3V3, GND->GND, SDA->D4, SCL->D5.
// On XIAO ESP32S3: D4=GPIO5; D5=GPIO6.
int SDA_PIN = 5;
int SCL_PIN = 6;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
uint8_t imuAddress = 0;
uint8_t imuIdentity = 0;
const char* sensorModel = "unidentified";
char bootId[32];
uint32_t sequenceNumber = 0;
uint32_t lastSample = 0;
uint32_t lastStatus = 0;
uint32_t retryAt = 0;
uint32_t backoffMs = 2000;
uint32_t lastImuProbe = 0;
uint32_t lastWifiRetry = 0;
bool networkEnabled = false;
bool wifiReported = false;
WiFiClientSecure tls;
PubSubClient mqtt(tls);

// Bench-only physical USB clock provisioning; never accepted over MQTT/Wi-Fi.
// Use the trusted laptop's current UTC epoch, not a fixed build timestamp.
void serviceUsbClock() {
  static char command[40];
  static size_t used = 0;
  static bool overflow = false;
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      command[used] = '\0';
      if (!overflow && strncmp(command, "TIME ", 5) == 0) {
        char* end = nullptr;
        const unsigned long long epoch = strtoull(command + 5, &end, 10);
        if (end != command + 5 && *end == '\0' && epoch >= 1700000000ULL && epoch < 4102444800ULL) {
          // Never change the clock under an active TLS connection.
          mqtt.disconnect();
          tls.stop();
          struct timeval utc = {static_cast<time_t>(epoch), 0};
          if (settimeofday(&utc, nullptr) == 0) {
            retryAt = 0;
            Serial.println("Clock synchronized from trusted USB laptop; TLS validation remains enabled.");
          }
        } else Serial.println("Rejected invalid USB time command.");
      }
      used = 0;
      overflow = false;
    } else if (used < sizeof(command) - 1) command[used++] = ch;
    else overflow = true;
  }
}

bool readRegisters(uint8_t addr, uint8_t reg, uint8_t* data, size_t length) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, length, true) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

bool writeRegister(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool initializeImu() {
  for (uint8_t addr : {uint8_t(0x68), uint8_t(0x69)}) {
    uint8_t identity = 0;
    if (!readRegisters(addr, 0x75, &identity, 1)) continue;
    Serial.printf("IMU address 0x%02X; WHO_AM_I=0x%02X\n", addr, identity);
    const char* model = nullptr;
    if (identity == 0x68) model = "MPU-6050";
    else if (identity == 0x70) model = "MPU-6500";
    else if (identity == 0x71 || identity == 0x73) model = "MPU-925x";
    if (!model) {
      Serial.println("Unsupported sensor identity: refusing to publish guessed values.");
      continue;
    }
    // Shared MPU6xxx/925x register layout: wake, +/-2g, +/-250 degrees/second.
    if (!writeRegister(addr, 0x6B, 0x01)) continue;
    delay(100);
    if (!writeRegister(addr, 0x1B, 0x00) || !writeRegister(addr, 0x1C, 0x00) ||
        !writeRegister(addr, 0x1A, 0x03) || !writeRegister(addr, 0x19, 0x09)) continue;
    uint8_t range[2] = {};
    if (!readRegisters(addr, 0x1B, range, 2) || (range[0] & 0x18) || (range[1] & 0x18)) continue;
    imuAddress = addr;
    imuIdentity = identity;
    sensorModel = model;
    Serial.printf("Verified %s; SDA=GPIO%d, SCL=GPIO%d.\n", model, SDA_PIN, SCL_PIN);
    return true;
  }
  Serial.printf("No supported IMU: SDA=GPIO%d SCL=GPIO%d. No readings fabricated.\n", SDA_PIN, SCL_PIN);
  static bool scanned = false;
  static unsigned respondingAddresses = 0;
  if (!scanned) {
    scanned = true;
    Serial.printf("I2C diagnostics: SDA=%d SCL=%d (idle bus should be HIGH).\n",
                  digitalRead(SDA_PIN), digitalRead(SCL_PIN));
    for (uint8_t address = 8; address < 120; ++address) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0) {
        Serial.printf("I2C ACK at 0x%02X\n", address);
        ++respondingAddresses;
      }
    }
  }
  Serial.printf("Initial I2C scan: %u responding addresses; current SDA=%d SCL=%d.\n",
                respondingAddresses, digitalRead(SDA_PIN), digitalRead(SCL_PIN));
  return false;
}

bool readImu(float acceleration[3], float angularVelocity[3]) {
  uint8_t data[14];
  if (!imuAddress || !readRegisters(imuAddress, 0x3B, data, sizeof(data))) return false;
  for (int i = 0; i < 3; ++i) {
    const int16_t rawAccel = static_cast<int16_t>((uint16_t(data[2*i]) << 8) | data[2*i+1]);
    const int16_t rawGyro = static_cast<int16_t>((uint16_t(data[8+2*i]) << 8) | data[9+2*i]);
    acceleration[i] = rawAccel * (9.80665f / 16384.0f); // Includes gravity.
    angularVelocity[i] = rawGyro / 131.0f;
  }
  return true;
}

void serviceNetwork() {
  if (!networkEnabled) return;
  if (WiFi.status() != WL_CONNECTED) {
    wifiReported = false;
    if (millis() - lastWifiRetry >= 15000) {
      lastWifiRetry = millis();
      WiFi.reconnect();
    }
    if (millis() - lastStatus > 5000) {
      Serial.printf("Wi-Fi pending; status=%d. Local sensor sampling continues.\n", WiFi.status());
      lastStatus = millis();
    }
    return;
  }
  if (!wifiReported) {
    Serial.print("Wi-Fi connected; XIAO IP: ");
    Serial.println(WiFi.localIP());
    wifiReported = true;
  }
  if (time(nullptr) < 1700000000) {
    if (millis() - lastStatus > 5000) {
      Serial.println("Waiting for network clock synchronization before verified TLS.");
      lastStatus = millis();
    }
    return;
  }
  if (!mqtt.connected()) {
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - retryAt) < 0) return;
    if (!mqtt.connect(bootId, "group18-glasses-01", MQTT_PASSWORD)) {
      Serial.printf("MQTT pending; state=%d; retry after %lu ms.\n", mqtt.state(), static_cast<unsigned long>(backoffMs));
      retryAt = now + backoffMs;
      backoffMs = min(backoffMs * 2, uint32_t(60000));
      return;
    }
    backoffMs = 2000;
    Serial.println("MQTT connected with credentials and server certificate validation.");
  }
  mqtt.loop();
}

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 2500) delay(10);
  Serial.println("Group18 XIAO real IMU vertical-slice test.");
  Serial.println("Camera, microphone, SD recording, and command execution are NOT initialized.");
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  if (!initializeImu()) {
    Serial.println("Testing reversed D4/D5 assignment without moving wires.");
    Wire.end();
    SDA_PIN = 6;
    SCL_PIN = 5;
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    Wire.setTimeOut(50);
    if (!initializeImu()) {
      Wire.end();
      SDA_PIN = 5;
      SCL_PIN = 6;
      Wire.begin(SDA_PIN, SCL_PIN);
      Wire.setClock(100000);
      Wire.setTimeOut(50);
      Serial.println("No IMU in either orientation. Restored SDA D4 / SCL D5 for retries.");
    }
  }
  snprintf(bootId, sizeof(bootId), "xiao-%08lx-%08lx",
      static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  networkEnabled = WIFI_SSID[0] && WIFI_PASSWORD[0] && MQTT_HOST[0] && MQTT_PASSWORD[0] && MQTT_CA[0];
  if (!networkEnabled) {
    Serial.println("USB sensor test only: private network configuration not supplied.");
    return;
  }
  WiFi.persistent(false);
  WiFi.setHostname("group18-xiao-imu");
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      Serial.printf("Wi-Fi disconnected: reason %u.\n", info.wifi_sta_disconnected.reason);
  });
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  tls.setCACert(MQTT_CA);
  mqtt.setServer(MQTT_HOST, 8883);
  mqtt.setBufferSize(1536);
  mqtt.setSocketTimeout(3);
}

void loop() {
  delay(10);
  serviceUsbClock();
  serviceNetwork();
  if (!imuAddress && millis() - lastImuProbe > 5000) {
    lastImuProbe = millis();
    initializeImu();
  }
  if (millis() - lastSample < SAMPLE_INTERVAL_MS) return;
  lastSample = millis();
  float a[3], g[3];
  if (!readImu(a, g)) {
    if (imuAddress) Serial.println("IMU read failed; skipping sample.");
    return;
  }
  Serial.printf("REAL IMU %s: acceleration [%.3f, %.3f, %.3f] m/s^2; gyro [%.3f, %.3f, %.3f] deg/s\n",
      sensorModel, a[0], a[1], a[2], g[0], g[1], g[2]);
  if (!mqtt.connected()) return;
  const time_t now = time(nullptr);
  struct tm utc;
  gmtime_r(&now, &utc);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  char payload[850];
  snprintf(payload, sizeof(payload),
      "{\"device_id\":\"group18-glasses-01\",\"device_model\":\"XIAO-ESP32S3-Sense\","
      "\"source\":\"xiao_imu\",\"sensor_model\":\"%s\",\"sensor_whoami\":%u,"
      "\"boot_id\":\"%s\",\"sequence\":%lu,\"event\":\"motion_sample\","
      "\"timestamp\":\"%s\",\"uptime_s\":%lu,\"rssi_dbm\":%ld,"
      "\"acceleration_m_s2\":[%.5f,%.5f,%.5f],\"angular_velocity_deg_s\":[%.5f,%.5f,%.5f]}",
      sensorModel, imuIdentity, bootId, static_cast<unsigned long>(sequenceNumber), timestamp,
      static_cast<unsigned long>(millis()/1000), static_cast<long>(WiFi.RSSI()),
      a[0], a[1], a[2], g[0], g[1], g[2]);
  if (mqtt.publish("grp18/devices/group18-glasses-01/telemetry", payload, false)) {
    Serial.printf("MQTT sample sent; sequence=%lu. Check Grafana for receipt.\n", static_cast<unsigned long>(sequenceNumber++));
  }
  // Bench test only: QoS 0; no SD queue, application acknowledgement, or battery-life claim.
}
