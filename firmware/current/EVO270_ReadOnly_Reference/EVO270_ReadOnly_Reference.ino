/*
  EVOHeat EVO270-1 / HW211
  Waveshare ESP32-S3-RS485-CAN
  Canonical public-safe read-only reference implementation.

  This captures the field-proven V2.1.2 architecture:
  - explicit GPIO21 RS485 direction
  - 9600 8N1 / slave 99
  - Function 03 reads only
  - fast + slow polling
  - Wi-Fi + ArduinoOTA
  - MQTT state publishing
  - Home Assistant MQTT Discovery for mapped sensors
  - browser status/map page

  It is not claimed to be a byte-for-byte copy of every locally-installed
  intermediate sketch. See docs/PROJECT_HISTORY.md.
*/

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "secrets.h"
#include "evo270_types.h"
#include "device_profile.h"

static const char *FW_VERSION = "2.1.2-reference-readonly";
static const uint8_t MODBUS_SLAVE = 99;
static const int RS485_TX_PIN = 17;
static const int RS485_RX_PIN = 18;
static const int RS485_EN_PIN = 21;
static const uint32_t MODBUS_BAUD = 9600;
static const uint32_t MODBUS_TIMEOUT_MS = 300;
static const uint32_t FAST_POLL_INTERVAL_MS = 20000;
static const uint32_t SLOW_POLL_INTERVAL_MS = 300000;

static const uint16_t REG_POWER = 1011;
static const uint16_t REG_ACTUAL_MODE = 1013;
static const uint16_t REG_STATUS0 = 2050;
static const uint16_t REG_STATUS1 = 2051;
static const uint16_t REG_FAULT0 = 2085;

HardwareSerial RS485(1);
WebServer web(80);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

HwsState hws;
uint32_t modbusSuccessCount = 0;
uint32_t modbusFailureCount = 0;
unsigned long lastFastPollMs = 0;
unsigned long lastSlowPollMs = 0;
unsigned long lastMqttAttemptMs = 0;

LegacyRegisterSensor directSensors[] = {
  {"usage_of_out_05_01", "Usage of OUT 05 [/01]", "/01", 1020, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"usage_of_out_06_02", "Usage of OUT 06 [/02]", "/02", 1021, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"defrosting_startup_temp_d01", "Defrosting startup temp [D01]", "D01", 1034, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"defrosting_shutdown_temp_d02", "Defrosting shutdown temp [D02]", "D02", 1035, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"duration_of_defrosting_d03", "Duration of defrosting [D03]", "D03", 1036, DecodeType::RAW, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"longest_duration_of_defrosting_d04", "Longest duration of defrosting [D04]", "D04", 1037, DecodeType::DIGI7, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"shortest_duration_of_defrosting_d05", "Shortest duration of defrosting [D05]", "D05", 1038, DecodeType::DIGI7, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"defrosting_way_d06", "Defrosting way [D06]", "D06", 1039, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"intelligent_defrosting_judgement_d07", "Intelligent defrosting judgement [D07]", "D07", 1040, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"eev_adjustment_mode_e01", "Eev adjustment mode [E01]", "E01", 1055, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"target_degree_of_supreheat_e02", "Target degree of supreheat [E02]", "E02", 1056, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"original_position_of_eev_e03", "Original position of eev [E03]", "E03", 1057, DecodeType::DIGI2, "", "", PollGroup::SLOW, true, false, false, 0},
  {"minimal_opening_position_of_eev_e04", "Minimal opening position of eev [E04]", "E04", 1058, DecodeType::DIGI2, "", "", PollGroup::SLOW, true, false, false, 0},
  {"position_of_eev_for_defrosting_e05", "Position of eev for defrosting [E05]", "E05", 1059, DecodeType::DIGI2, "", "", PollGroup::SLOW, true, false, false, 0},
  {"disinfection_target_temp_g01", "Disinfection target temp [G01]", "G01", 1046, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"duration_of_disinfection_g02", "Duration of disinfection [G02]", "G02", 1047, DecodeType::RAW, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"startup_point_of_disinfection_g03", "Startup point of disinfection [G03]", "G03", 1048, DecodeType::RAW, "h", "duration", PollGroup::SLOW, true, false, false, 0},
  {"circle_of_disinfection_g04", "Circle of disinfection [G04]", "G04", 1049, DecodeType::RAW, "d", "duration", PollGroup::SLOW, true, false, false, 0},
  {"remenber_the_status_of_device_when_power_down_h01", "Remenber the status of device when power down [H01]", "H01", 1067, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"heating_source_h03", "Heating source [H03]", "H03", 1069, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"temperature_unit_h07", "Temperature unit [H07]", "H07", 1073, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"device_address_h30", "Device address [H30]", "H30", 1076, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"intelligent_control_mode_h31", "Intelligent control mode [H31]", "H31", 1077, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"circle_of_submitting_data_to_cloud_h32", "Circle of submitting data to cloud [H32]", "H32", 1066, DecodeType::RAW, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"adjustable_range_of_target_temperature_h98", "Adjustable range of target temperature [H98]", "H98", 1074, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"compensate_to_the_shown_temp_h99", "Compensate to the shown temp [H99]", "H99", 1075, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"the_sensor_to_control_solar_water_pump_n01", "The sensor to control solar water pump [N01]", "N01", 1080, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"longest_running_time_of_solar_water_pump_n02", "Longest running time of solar water pump [N02]", "N02", 1081, DecodeType::RAW, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"temp_hysteresis_of_solar_water_pump_n03", "Temp hysteresis of solar water pump [N03]", "N03", 1082, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"activate_the_nighttime_temp_decreases_mode_n04", "Activate the nighttime temp decreases mode [N04]", "N04", 1083, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"startup_point_of_the_nighttime_temp_decreases_mode_n05", "Startup point of the nighttime temp decreases mode [N05]", "N05", 1084, DecodeType::RAW, "h", "duration", PollGroup::SLOW, true, false, false, 0},
  {"shutdown_point_of_the_nighttime_temp_decreases_mode_n06", "Shutdown point of the nighttime temp decreases mode [N06]", "N06", 1085, DecodeType::RAW, "h", "duration", PollGroup::SLOW, true, false, false, 0},
  {"startup_temp_of_decreasing_solar_water_temp_n07", "Startup temp of decreasing solar water temp [N07]", "N07", 1086, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"temp_hysteresis_of_stopping_decreasing_solar_water_temp_n08", "Temp hysteresis of stopping decreasing solar water temp [N08]", "N08", 1087, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"solar_water_releasing_temp_n09", "Solar water releasing temp [N09]", "N09", 1088, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"shutdown_temp_of_solar_water_pump_n10", "Shutdown temp of solar water pump [N10]", "N10", 1089, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"working_mode_of_solar_water_pump_n11", "Working mode of solar water pump [N11]", "N11", 1090, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"target_temp_r01", "Target temp [R01]", "R01", 1104, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
  {"hysteresis_of_heat_pump_startup_bottom_sensor_r03", "Hysteresis of heat pump startup bottom sensor [R03]", "R03", 1106, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"enable_r05_as_setpoint_of_booster_r04", "Enable r05 as setpoint of booster [R04]", "R04", 1107, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_booster_r05", "Setpoint of booster [R05]", "R05", 1108, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"booster_startup_delay_r06", "Booster startup delay [R06]", "R06", 1109, DecodeType::DIGI4, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"booster_replaces_heat_pump_r07", "Booster replaces heat pump [R07]", "R07", 1110, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_ambient_temp_to_activate_booster_to_replace_heat_pump_r08", "Setpoint of ambient temp to activate booster to replace heat pump [R08]", "R08", 1111, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_ambient_temp_to_activate_booster_without_delay_r09", "Setpoint of ambient temp to activate booster without delay [R09]", "R09", 1112, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_ambient_temp_to_activate_booster_with_delay_r10", "Setpoint of ambient temp to activate booster with delay [R10]", "R10", 1113, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"ambient_temp_of_shutting_down_compressor_compulsively_r12", "Ambient temp of shutting down compressor compulsively [R12]", "R12", 1115, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"the_target_temp_of_second_heating_source_r14", "The target temp of second heating source [R14]", "R14", 1117, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"maximal_ambient_temp_of_working_compressor_r15", "Maximal ambient temp of working compressor [R15]", "R15", 1118, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"enable_top_sensor_to_control_compressor_r17", "Enable top sensor to control compressor [R17]", "R17", 1120, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"hysteresis_of_heat_pump_startup_top_sensor_r18", "Hysteresis of heat pump startup top sensor [R18]", "R18", 1121, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_1_of_ambient_temp_to_stop_compressor_r19", "Setpoint 1 of ambient temp to stop compressor [R19]", "R19", 1122, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_2_of_ambient_temp_to_stop_compressor_r20", "Setpoint 2 of ambient temp to stop compressor [R20]", "R20", 1123, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"eev_current_position_o07", "Eev current position [O07]", "O07", 2060, DecodeType::RAW, "", "", PollGroup::FAST, true, false, false, 0},
  {"accumulative_running_time_of_compressor_o08", "Accumulative running time of compressor [O08]", "O08", 2061, DecodeType::RAW, "h", "duration", PollGroup::FAST, true, false, false, 0},
  {"accumulative_running_time_of_booster_o09", "Accumulative running time of booster [O09]", "O09", 2062, DecodeType::RAW, "h", "duration", PollGroup::FAST, true, false, false, 0},
  {"ambient_temperature_t01", "Ambient temperature [T01]", "T01", 2019, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
  {"bottom_temperature_t02", "Bottom temperature [T02]", "T02", 2020, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
  {"top_temperature_t03", "Top temperature [T03]", "T03", 2021, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
  {"coil_temperature_t04", "Coil temperature [T04]", "T04", 2022, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, true, false, false, 0},
  {"suction_temperature_t05", "Suction temperature [T05]", "T05", 2023, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, true, false, false, 0},
  {"solar_temperature_t06", "Solar temperature [T06]", "T06", 2024, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, true, false, false, 0},
  {"temperature_value_shown_on_app_display_t10", "Temperature value shown on app display [T10]", "T10", 2025, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
};
const size_t DIRECT_SENSOR_COUNT = sizeof(directSensors) / sizeof(directSensors[0]);

LegacyBitSensor bitSensors[] = {
  {"compressor_o01", "Compressor [O01]", "O01", 2050, 8},
  {"electrical_heater_o02", "Electrical heater [O02]", "O02", 2050, 9},
  {"4_way_valve_o03", "4-way valve [O03]", "O03", 2050, 10},
  {"fan_high_speed_o04", "Fan high speed [O04]", "O04", 2050, 11},
  {"fan_low_speed_o05", "Fan low speed [O05]", "O05", 2050, 12},
  {"reserve_solar_pump_solar_valve_pump_o06", "Reserve/solar pump/solar valve pump [O06]", "O06", 2050, 13},
  {"3v_de_o10", "3V_DE [O10]", "O10", 2050, 14},
  {"mv_de_o11", "MV_DE [O11]", "O11", 2050, 15},
  {"shutdown_o12", "shutDown [O12]", "O12", 2051, 0},
  {"dtu_wifishi_fou_shang_xian_o13", "DTU&WIFI online [O13]", "O13", 2051, 1},
  {"chu_shuang_defrost_o14", "Defrost [O14]", "O14", 2051, 2},
  {"xi_tong_shi_fou_jin_ru_gao_wen_re_shui_jie_duan_o15", "High-temperature hot-water stage [O15]", "O15", 2051, 3},
  {"remote_on_off_switch_s01", "Remote ON/OFF switch [S01]", "S01", 2050, 0},
  {"over_heat_protection_switch_s02", "Over heat protection switch [S02]", "S02", 2050, 1},
  {"low_pressure_switch_s03", "Low pressure switch [S03]", "S03", 2050, 2},
  {"high_pressure_switch_s04", "High pressure switch [S04]", "S04", 2050, 3},
  {"accelerate_the_running_time_of_heater_s05", "Accelerate the running time of heater [S05]", "S05", 2050, 4},
  {"second_heating_source_s06", "Second heating source [S06]", "S06", 2050, 5},
};
const size_t BIT_SENSOR_COUNT = sizeof(bitSensors) / sizeof(bitSensors[0]);

LegacyPlaceholderSensor placeholderSensors[] = {
  {"jin_ru_can_shu_chao_fan_wei_bao_hu_ci_shu_t11", "T11 legacy placeholder", "T11"},
  {"ji_yi_xin_pian_eepromcun_chu_ci_shu_t12", "T12 legacy placeholder", "T12"},
};
const size_t PLACEHOLDER_SENSOR_COUNT = sizeof(placeholderSensors) / sizeof(placeholderSensors[0]);

uint16_t modbusCRC16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < length; pos++) {
    crc ^= data[pos];
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 1) { crc >>= 1; crc ^= 0xA001; }
      else crc >>= 1;
    }
  }
  return crc;
}

void rs485ReceiveMode() { digitalWrite(RS485_EN_PIN, LOW); }
void rs485TransmitMode() { digitalWrite(RS485_EN_PIN, HIGH); }

float decodeValue(uint16_t raw, DecodeType type) {
  switch (type) {
    case DecodeType::RAW: return (float)raw;
    case DecodeType::TEMP1: return ((int32_t)raw - 60) * 0.5f;
    case DecodeType::TEMP: return ((int16_t)raw) * 0.1f;
    case DecodeType::DIGI2: return raw * 10.0f;
    case DecodeType::DIGI4: return raw * 5.0f;
    case DecodeType::DIGI7: return raw * 0.5f;
  }
  return (float)raw;
}

bool bitIsSet(uint16_t value, uint8_t bit) {
  return (value & (1U << bit)) != 0;
}

bool readHoldingRegister(uint16_t address, uint16_t &value) {
  uint8_t request[8] = {
    MODBUS_SLAVE, 0x03,
    (uint8_t)(address >> 8), (uint8_t)(address & 0xFF),
    0x00, 0x01, 0x00, 0x00
  };
  uint16_t crc = modbusCRC16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = crc >> 8;

  while (RS485.available()) RS485.read();

  rs485TransmitMode();
  delayMicroseconds(200);
  RS485.write(request, sizeof(request));
  RS485.flush();
  delayMicroseconds(200);
  rs485ReceiveMode();

  uint8_t response[16];
  size_t n = 0;
  unsigned long start = millis();
  unsigned long lastByte = start;
  bool any = false;

  while (millis() - start < MODBUS_TIMEOUT_MS) {
    while (RS485.available()) {
      if (n < sizeof(response)) response[n++] = RS485.read();
      else RS485.read();
      any = true;
      lastByte = millis();
    }
    if (any && millis() - lastByte > 10) break;
    ArduinoOTA.handle();
    web.handleClient();
    delay(1);
  }

  if (n != 7 || response[0] != MODBUS_SLAVE || response[1] != 0x03 || response[2] != 0x02) {
    modbusFailureCount++;
    return false;
  }

  uint16_t got = response[5] | ((uint16_t)response[6] << 8);
  uint16_t calc = modbusCRC16(response, 5);
  if (got != calc) {
    modbusFailureCount++;
    return false;
  }

  value = ((uint16_t)response[3] << 8) | response[4];
  modbusSuccessCount++;
  return true;
}

String baseTopic() { return String(MQTT_BASE_TOPIC); }
String availabilityTopic() { return baseTopic() + "/availability"; }
String sensorStateTopic(const char *slug) { return baseTopic() + "/state/sensor/" + slug; }
String discoveryTopic(const char *slug) {
  return "homeassistant/sensor/" + String(DEVICE_CODE) + "/" + slug + "/config";
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  return s;
}

void publishSensorDiscovery(LegacyRegisterSensor &s) {
  String p = "{";
  p += "\"name\":\"" + jsonEscape(String(s.displayName)) + "\",";
  p += "\"unique_id\":\"" + String(DEVICE_CODE) + "_" + s.slug + "\",";
  p += "\"default_entity_id\":\"sensor." + String(DEVICE_CODE) + "_" + s.slug + "\",";
  p += "\"state_topic\":\"" + sensorStateTopic(s.slug) + "\",";
  p += "\"availability_topic\":\"" + availabilityTopic() + "\",";
  if (strlen(s.unit)) p += "\"unit_of_measurement\":\"" + jsonEscape(String(s.unit)) + "\",";
  if (strlen(s.deviceClass)) p += "\"device_class\":\"" + String(s.deviceClass) + "\",";
  if (s.diagnostic) p += "\"entity_category\":\"diagnostic\",";
  p += "\"device\":{\"identifiers\":[\"evo270_" + String(DEVICE_CODE) + "\"],";
  p += "\"name\":\"" + jsonEscape(String(DEVICE_NAME)) + "\",";
  p += "\"manufacturer\":\"EvoHeat\",\"model\":\"EVO270-1 / HW211\",";
  p += "\"sw_version\":\"" + String(FW_VERSION) + "\"}";
  p += "}";
  mqtt.publish(discoveryTopic(s.slug).c_str(), p.c_str(), true);
}

void publishAllDiscovery() {
  for (size_t i=0; i<DIRECT_SENSOR_COUNT; i++) publishSensorDiscovery(directSensors[i]);
}

void publishSensorState(LegacyRegisterSensor &s) {
  if (!mqtt.connected() || !s.valid) return;
  String v(decodeValue(s.raw, s.decode), 1);
  mqtt.publish(sensorStateTopic(s.slug).c_str(), v.c_str(), true);
}

LegacyRegisterSensor *findByCode(const char *code) {
  for (size_t i=0; i<DIRECT_SENSOR_COUNT; i++)
    if (strcmp(directSensors[i].protocolCode, code) == 0) return &directSensors[i];
  return nullptr;
}

bool pollSensor(LegacyRegisterSensor &s) {
  uint16_t raw=0;
  bool ok=readHoldingRegister(s.address, raw);
  s.lastReadOk=ok;
  if (ok) {
    s.raw=raw;
    s.valid=true;
    publishSensorState(s);
  }
  delay(12);
  return ok;
}

bool readCore(uint16_t addr, RegisterValue &dest) {
  uint16_t raw=0;
  bool ok=readHoldingRegister(addr, raw);
  dest.lastReadOk=ok;
  if (ok) { dest.raw=raw; dest.valid=true; }
  delay(12);
  return ok;
}

void pollFast() {
  LegacyRegisterSensor *t01=findByCode("T01");
  if (t01) pollSensor(*t01);

  readCore(REG_POWER, hws.power);
  readCore(REG_ACTUAL_MODE, hws.actualMode);
  readCore(REG_STATUS0, hws.status0);
  readCore(REG_STATUS1, hws.status1);
  readCore(REG_FAULT0, hws.fault0);

  for (size_t i=0; i<DIRECT_SENSOR_COUNT; i++) {
    if (directSensors[i].pollGroup != PollGroup::FAST) continue;
    if (strcmp(directSensors[i].protocolCode, "T01") == 0) continue;
    pollSensor(directSensors[i]);
  }
  lastFastPollMs=millis();
}

void pollSlow() {
  for (size_t i=0; i<DIRECT_SENSOR_COUNT; i++) {
    if (directSensors[i].pollGroup == PollGroup::SLOW) pollSensor(directSensors[i]);
  }
  lastSlowPollMs=millis();
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start=millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-start<20000) delay(250);
}

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  if (strlen(OTA_PASSWORD)) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
}

void connectMqtt() {
  if (mqtt.connected() || WiFi.status()!=WL_CONNECTED) return;
  if (millis()-lastMqttAttemptMs<5000) return;
  lastMqttAttemptMs=millis();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  String willTopic=availabilityTopic();
  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD,
                   willTopic.c_str(), 0, true, "offline")) {
    mqtt.publish(willTopic.c_str(), "online", true);
    publishAllDiscovery();
  }
}

String webPage() {
  String s="<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  s+="<style>body{font-family:system-ui;margin:20px}table{border-collapse:collapse;width:100%}td,th{padding:6px;border-bottom:1px solid #ddd;text-align:left}code{font-size:.9em}</style></head><body>";
  s+="<h1>EVO270 read-only monitor</h1><p>"+String(DEVICE_NAME)+"</p>";
  s+="<p>Modbus OK: "+String(modbusSuccessCount)+" &nbsp; Fail: "+String(modbusFailureCount)+"</p>";
  s+="<table><tr><th>Code</th><th>Register</th><th>Value</th><th>Entity</th></tr>";
  for (size_t i=0;i<DIRECT_SENSOR_COUNT;i++) {
    LegacyRegisterSensor &x=directSensors[i];
    s+="<tr><td>"+String(x.protocolCode)+"</td><td>"+String(x.address)+"</td><td>";
    s+=x.valid ? String(decodeValue(x.raw,x.decode),1) : "—";
    s+="</td><td><code>sensor."+String(DEVICE_CODE)+"_"+x.slug+"</code></td></tr>";
  }
  s+="</table></body></html>";
  return s;
}

void setupWeb() {
  web.on("/", HTTP_GET, [](){ web.send(200,"text/html; charset=utf-8",webPage()); });
  web.on("/health", HTTP_GET, [](){
    String j="{\"wifi\":" + String(WiFi.status()==WL_CONNECTED?"true":"false") +
             ",\"mqtt\":" + String(mqtt.connected()?"true":"false") +
             ",\"modbus_successes\":" + String(modbusSuccessCount) +
             ",\"modbus_failures\":" + String(modbusFailureCount) + "}";
    web.send(200,"application/json",j);
  });
  web.begin();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(RS485_EN_PIN, OUTPUT);
  rs485ReceiveMode();
  RS485.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  connectWiFi();
  setupOTA();
  setupWeb();
  connectMqtt();
  pollFast();
  pollSlow();
}

void loop() {
  ArduinoOTA.handle();
  web.handleClient();
  if (WiFi.status()!=WL_CONNECTED) WiFi.reconnect();
  connectMqtt();
  mqtt.loop();

  if (millis()-lastFastPollMs >= FAST_POLL_INTERVAL_MS) pollFast();
  if (millis()-lastSlowPollMs >= SLOW_POLL_INTERVAL_MS) pollSlow();
  delay(2);
}
