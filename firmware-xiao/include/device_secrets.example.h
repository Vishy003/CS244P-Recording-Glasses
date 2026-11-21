#pragma once
// Copy to device_secrets.h and fill locally, or use scripts/prepare_firmware.py --xiao.
// These empty defaults permit a USB-only real-sensor test. No anonymous MQTT.
static const char WIFI_SSID[] = "";
static const char WIFI_PASSWORD[] = "";
static const char MQTT_HOST[] = "";
static const char MQTT_PASSWORD[] = "";
static const char MQTT_CA[] = "";
