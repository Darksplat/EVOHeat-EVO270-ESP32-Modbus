#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Select a friendly/location profile. The hardware identity itself is NOT
// hard-coded: it is derived automatically from this Waveshare ESP32's Wi-Fi MAC.
#define EVO270_PROFILE_LAUNDRY_BATHROOM 1
#define EVO270_PROFILE_ENSUITE_KITCHEN  2

#ifndef EVO270_PROFILE
#define EVO270_PROFILE EVO270_PROFILE_LAUNDRY_BATHROOM
#endif

#if EVO270_PROFILE == EVO270_PROFILE_LAUNDRY_BATHROOM
static const char *HOSTNAME     = "evo270-laundry-bathroom";
static const char *DEVICE_LABEL = "EVO270 - Bathroom & Laundry";
#elif EVO270_PROFILE == EVO270_PROFILE_ENSUITE_KITCHEN
static const char *HOSTNAME     = "evo270-ensuite-kitchen";
static const char *DEVICE_LABEL = "EVO270 - Ensuite & Kitchen";
#else
#error "Unknown EVO270_PROFILE"
#endif

// Runtime identity buffers. These intentionally belong to the replacement
// Waveshare ESP32, not to the removed Aqua Temp Wi-Fi module.
static char evo270DeviceCode[13] = {0};       // 12 hex digits, no colons
static char evo270DeviceName[96] = {0};
static char evo270MqttClientId[32] = {0};
static char evo270MqttBaseTopic[32] = {0};

static void ensureEvo270DeviceIdentity() {
  if (evo270DeviceCode[0] != '\0') return;

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();

  // WiFi.macAddress() should provide the factory station MAC. If it is not
  // available for some reason, fall back to the ESP32 eFuse MAC.
  if (mac.length() != 12 || mac == "000000000000") {
    const uint64_t efuseMac = ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL;
    char fallback[13];
    snprintf(fallback, sizeof(fallback), "%012llx",
             (unsigned long long)efuseMac);
    mac = fallback;
  }

  snprintf(evo270DeviceCode, sizeof(evo270DeviceCode), "%s", mac.c_str());
  snprintf(evo270DeviceName, sizeof(evo270DeviceName), "%s [%s]",
           DEVICE_LABEL, evo270DeviceCode);
  snprintf(evo270MqttClientId, sizeof(evo270MqttClientId), "evo270-%s",
           evo270DeviceCode);
  snprintf(evo270MqttBaseTopic, sizeof(evo270MqttBaseTopic), "evo270/%s",
           evo270DeviceCode);
}

static const char *deviceCode() {
  ensureEvo270DeviceIdentity();
  return evo270DeviceCode;
}

static const char *deviceName() {
  ensureEvo270DeviceIdentity();
  return evo270DeviceName;
}

static const char *mqttClientId() {
  ensureEvo270DeviceIdentity();
  return evo270MqttClientId;
}

static const char *mqttBaseTopic() {
  ensureEvo270DeviceIdentity();
  return evo270MqttBaseTopic;
}

// Preserve the names expected by the main sketch while making them dynamic.
#define DEVICE_CODE     deviceCode()
#define DEVICE_NAME     deviceName()
#define MQTT_CLIENT_ID  mqttClientId()
#define MQTT_BASE_TOPIC mqttBaseTopic()
