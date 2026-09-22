/*
  EVOHeat EVO270-1 - Ensuite / Kitchen HWS
  Waveshare ESP32-S3-RS485-CAN
  Firmware V2.2.7-clock-test-link

  V2.2.1
  ---------
  - Keeps the proven GPIO21 RS485 direction handling and complete legacy map.
  - Adds VERIFIED Modbus function 06 writes for three user controls only:
      1011 Power: 0=OFF, 1=ON
      1012 Requested operating mode: 0/2/3/4/7
      1104 R01 target temperature: 10.0-60.0 C in 0.5 C steps
  - Every write must receive a valid FC06 echo and then pass an FC03 read-back.
  - Invalid MQTT payloads are rejected and the real controller state is republished.
  - All other HW211 registers remain read-only.
  - Temperature-unit select remains compatibility/read-only.
  - Fan speed remains status-only; the climate entity no longer advertises fan control.
  - Cleans Home Assistant MQTT entity names so the device name is not duplicated.
  - Preserves the migrated legacy Home Assistant entity IDs and unique IDs.
  - Adds write counters and the last verified write to the browser diagnostics.
  - READ-ONLY verification scan for HW211 vacation/timer registers L01-L13 (1129-1141).
  - READ-ONLY verification scan for the DTU/Wi-Fi clock block M11-M16 (1151-1156),
    plus register 1150 as an offset candidate because another workbook sheet differs.
  - ESP32 NTP clock (Australia/Melbourne) is shown beside controller time so the
    exact clock mapping and drift can be confirmed before enabling clock/timer writes.
  - Vacation/timer controls are writable with verification; controller clock registers remain read-only.

  V2.2.7 SCHEDULE CONTROLS
  ------------------------
  - Enables verified writes for the confirmed DTU/Wi-Fi vacation/timer block:
      L01-L04 1129-1132 vacation date enable + date
      L05-L13 1133-1141 timer enable mask + Timer 1/2 ON/OFF times
  - Home Assistant gets native MQTT switch/date/time entities for these controls.
  - Timer/date changes temporarily disable the affected schedule while changing
    multiple registers, then restore its enable bit. Failed writes attempt rollback.
  - Controller clock registers 1150-1156 remain READ ONLY because both physical
    EVO270 units returned zeroes there despite their internal timers operating.
  - V2.2.2 adds READ-ONLY probes of 1157 and 1158 to distinguish the two
    published HW211 register-map layouts before any controller-clock write is attempted.
    1157/1158 are never written by this diagnostic firmware.
  - V2.2.3 additionally probes H30=1 as an alternate Modbus slave, READ ONLY,
    for registers 1150-1158. This tests whether the controller clock is exposed on
    the central-controller address rather than the DTU/Wi-Fi address 99.
  - V2.2.4 keeps automatic clock writes DISABLED and adds one explicitly
    confirmed manual NTP->controller clock-write test on slave 99. The test writes
    DTU-map clock fields 1152-1156 first, then sets M11/1151=1 as the apply flag.
  - V2.2.5 enables the proven weekly clock check every Monday during the
    01:00 local hour. It compares the live EVO270 clock against the ESP32 NTP clock
    and only corrects the EVO270 when the clock is invalid, M11 is not enabled, or
    drift exceeds 90 seconds. Failed attempts can retry every 10 minutes until 02:00.
  - V2.2.6 corrects an important interpretation from V2.2.5: registers
    1151-1156 are a clock-write/apply mailbox, not a continuously advancing live
    controller clock. Therefore no drift is inferred from them. The firmware now
    performs one forced NTP clock push each Monday during the 01:00 local hour,
    with exact mailbox payload read-back verification. Failed attempts may retry
    every 10 minutes during that hour.

  TRANSPORT - PROVEN ON THIS INSTALLATION
  ---------------------------------------
  TX GPIO17
  RX GPIO18
  EN GPIO21  HIGH=transmit, LOW=receive
  9600 8N1
  DTU/Wi-Fi slave address 99

  UNIT IDENTITY
  -------------
  EVO270 – Ensuite & Kitchen 34EAE79F4BCE
  Aqua Temp / HA prefix: 34eae79f4bce
  Serial: A022112238161
  Legacy climate: climate.34eae79f4bce
  Legacy operating mode:
    select.back_fence_garden_evo270_ensuite_kitchen_34eae79f4bce_34eae79f4bce_operating_mode

  REQUIRED LIBRARY
  ----------------
  PubSubClient by Nick O'Leary

  PROTOCOL SOURCES
  ----------------
  sjtrny/esphome-hw211 protocol/hw211_modbus.json
  echopin664/EVO270-1-HWS

  WRITE SAFETY
  ------------
  These are controller commands, not electrical isolation or a safety cut-out.
  The EVO270's own control logic remains authoritative. In particular, a scheduled
  disinfection cycle may be controller-managed independently of the normal power
  state. Do not use Home Assistant as an emergency or service isolation control.
*/

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <time.h>

// ============================================================================
// PRIVATE CREDENTIALS
// ============================================================================
//
// Keep secrets.h in the SAME Arduino sketch folder as this .ino file.
// Edit secrets.h once; future firmware versions can reuse it.
//
// IMPORTANT: Do not commit secrets.h to GitHub.
//
#include "secrets.h"
#include "evo270_types.h"

// ============================================================================
// DEVICE
// ============================================================================

static const char *HOSTNAME       = "evo270-ensuite-kitchen";
static const char *DEVICE_CODE    = "34eae79f4bce";
static const char *DEVICE_CODE_UP = "34EAE79F4BCE";
static const char *DEVICE_NAME    = "EVO270 – Ensuite & Kitchen 34EAE79F4BCE";
static const char *FW_VERSION     = "2.2.7-clock-test-link";
static const char *MQTT_CLIENT_ID = "evo270-34eae79f4bce";

static const char *MQTT_BASE_TOPIC  = "evo270/34eae79f4bce";
static const char *MQTT_AVAIL_TOPIC = "evo270/34eae79f4bce/availability";

static const uint8_t MODBUS_SLAVE = 99;

// H30 / register 1076 has been confirmed as 1 on the Ensuite & Kitchen unit.
// V2.2.3 uses that address for BOOT-ONLY READ-ONLY clock diagnostics.
// All normal operation and every write remain on the proven DTU/Wi-Fi slave 99.
static const uint8_t CENTRAL_MODBUS_SLAVE = 1;

static const int RS485_TX_PIN = 17;
static const int RS485_RX_PIN = 18;
static const int RS485_EN_PIN = 21;
static const uint32_t MODBUS_BAUD = 9600;
static const uint32_t MODBUS_TIMEOUT_MS = 300;

// Dynamic values/status every 20 seconds.
// Configuration/setpoint registers every 5 minutes.
static const uint32_t FAST_POLL_INTERVAL_MS = 20000;
static const uint32_t SLOW_POLL_INTERVAL_MS = 300000;


// Installation timezone: Victoria, Australia. POSIX rule automatically handles AEDT/AEST.
static const char *LOCAL_TZ = "AEST-10AEDT,M10.1.0,M4.1.0/3";
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.google.com";

// Weekly controller-clock maintenance.
// struct tm uses Sunday=0, Monday=1.
static const bool WEEKLY_CLOCK_SYNC_ENABLED = true;
static const int WEEKLY_CLOCK_SYNC_WDAY = 1;
static const int WEEKLY_CLOCK_SYNC_HOUR = 1;
static const long CLOCK_CORRECTION_THRESHOLD_SECONDS = 90;
static const int WEEKLY_CLOCK_RETRY_BUCKET_MINUTES = 10;

// Keep true while validating V2.1. Once stable this can be changed to false
// to make the browser/serial log much quieter.
static const bool LOG_MODBUS_FRAMES = true;

// The temporary V2.0 retained discovery set has already been removed.
// Keep this disabled for normal V2.2 operation on both units.
static const bool REMOVE_V20_TEST_DISCOVERY = false;

// ============================================================================
// CORE REGISTERS
// ============================================================================

static const uint16_t REG_POWER          = 1011;
static const uint16_t REG_REQUESTED_MODE = 1012;
static const uint16_t REG_ACTUAL_MODE    = 1013;
static const uint16_t REG_TARGET_TEMP    = 1104;
static const uint16_t REG_STATUS0        = 2050;
static const uint16_t REG_STATUS1        = 2051;
static const uint16_t REG_FAULT0         = 2085;

static const float TARGET_TEMP_MIN_C = 10.0f;
static const float TARGET_TEMP_MAX_C = 60.0f;
static const float TARGET_TEMP_STEP_C = 0.5f;


// ============================================================================
// VACATION / TIMER CONTROL REGISTERS + READ-ONLY CLOCK DIAGNOSTICS
// ============================================================================
//
// L01-L13 are now writable with verified FC06 + FC03 read-back. Clock remains read-only.
// DTU/Wi-Fi map (slave 99):
//   L01-L04 1129-1132 vacation date controls
//   L05-L13 1133-1141 two on/off timer periods
//   M11-M16 1151-1156 controller clock block
// Register 1150 remains read only as an offset candidate because another workbook
// sheet places the clock block one register earlier. We will trust live values.

struct DiagnosticRegister {
  const char *code;
  const char *name;
  uint16_t address;
  bool valid;
  bool lastReadOk;
  uint16_t raw;
};

DiagnosticRegister vacationTimerDiagnostics[] = {
  {"L01", "Vacation date enable bits", 1129, false, false, 0},
  {"L02", "Vacation year", 1130, false, false, 0},
  {"L03", "Vacation month", 1131, false, false, 0},
  {"L04", "Vacation day", 1132, false, false, 0},
  {"L05", "Timer enable mask", 1133, false, false, 0},
  {"L06", "Timer 1 ON hour", 1134, false, false, 0},
  {"L07", "Timer 1 ON minute", 1135, false, false, 0},
  {"L08", "Timer 1 OFF hour", 1136, false, false, 0},
  {"L09", "Timer 1 OFF minute", 1137, false, false, 0},
  {"L10", "Timer 2 ON hour", 1138, false, false, 0},
  {"L11", "Timer 2 ON minute", 1139, false, false, 0},
  {"L12", "Timer 2 OFF hour", 1140, false, false, 0},
  {"L13", "Timer 2 OFF minute", 1141, false, false, 0},
};

DiagnosticRegister clockDiagnostics[] = {
  {"CAND", "Offset candidate / HW211 M11 clock-modify enable", 1150, false, false, 0},
  {"M11", "DTU M11 / HW211 M12 candidate", 1151, false, false, 0},
  {"M12", "DTU minute / HW211 minute candidate", 1152, false, false, 0},
  {"M13", "DTU hour / HW211 month candidate", 1153, false, false, 0},
  {"M14", "DTU day / HW211 day candidate", 1154, false, false, 0},
  {"M15", "DTU month / HW211 weekday candidate", 1155, false, false, 0},
  {"M16", "DTU year / HW211 year candidate", 1156, false, false, 0},
  {"MAP57", "Map discriminator: HW211 unit address / DTU reserved", 1157, false, false, 0},
  {"MAP58", "Map discriminator: HW211 control mode / DTU heartbeat", 1158, false, false, 0},
};

DiagnosticRegister centralClockDiagnostics[] = {
  {"S1-M11", "Clock modify enable", 1150, false, false, 0},
  {"S1-M12", "Current hour", 1151, false, false, 0},
  {"S1-M13", "Current minute", 1152, false, false, 0},
  {"S1-M14", "Current month", 1153, false, false, 0},
  {"S1-M15", "Current day", 1154, false, false, 0},
  {"S1-M16", "Current weekday", 1155, false, false, 0},
  {"S1-M17", "Current year (00-99)", 1156, false, false, 0},
  {"S1-M30", "Unit address", 1157, false, false, 0},
  {"S1-M31", "Online intelligent control mode", 1158, false, false, 0},
};

const size_t VACATION_TIMER_DIAG_COUNT =
  sizeof(vacationTimerDiagnostics) / sizeof(vacationTimerDiagnostics[0]);
const size_t CLOCK_DIAG_COUNT =
  sizeof(clockDiagnostics) / sizeof(clockDiagnostics[0]);
const size_t CENTRAL_CLOCK_DIAG_COUNT =
  sizeof(centralClockDiagnostics) / sizeof(centralClockDiagnostics[0]);

DiagnosticRegister *findVacationTimerDiag(uint16_t address);
void publishVacationTimerStates();

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

HardwareSerial RS485(1);
WebServer web(80);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ============================================================================
// LOG BUFFER
// ============================================================================

static const size_t LOG_CAPACITY = 220;
String logLines[LOG_CAPACITY];
size_t logHead = 0;
size_t logCount = 0;

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  return s;
}

void addLog(const String &line) {
  Serial.println(line);
  logLines[logHead] = line;
  logHead = (logHead + 1) % LOG_CAPACITY;
  if (logCount < LOG_CAPACITY) logCount++;
}

String renderLog() {
  String out;
  out.reserve(20000);
  size_t start = (logHead + LOG_CAPACITY - logCount) % LOG_CAPACITY;

  for (size_t i = 0; i < logCount; i++) {
    size_t index = (start + i) % LOG_CAPACITY;
    out += htmlEscape(logLines[index]);
    out += "\n";
  }

  return out;
}

void clearLog() {
  logHead = 0;
  logCount = 0;
  for (size_t i = 0; i < LOG_CAPACITY; i++) logLines[i] = "";
  addLog("Browser log cleared.");
}

// ============================================================================
// REGISTER/ENTITY DEFINITIONS
// ============================================================================

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
  {"eev_adjustment_mode_e01", "EEV adjustment mode [E01]", "E01", 1055, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"target_degree_of_supreheat_e02", "Target degree of supreheat [E02]", "E02", 1056, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"original_position_of_eev_e03", "Original position of EEV [E03]", "E03", 1057, DecodeType::DIGI2, "", "", PollGroup::SLOW, true, false, false, 0},
  {"minimal_opening_position_of_eev_e04", "Minimal opening position of EEV [E04]", "E04", 1058, DecodeType::DIGI2, "", "", PollGroup::SLOW, true, false, false, 0},
  {"position_of_eev_for_defrosting_e05", "Position of EEV for defrosting [E05]", "E05", 1059, DecodeType::DIGI2, "", "", PollGroup::SLOW, true, false, false, 0},
  {"disinfection_target_temp_g01", "Disinfection target temp [G01]", "G01", 1046, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"duration_of_disinfection_g02", "Duration of disinfection [G02]", "G02", 1047, DecodeType::RAW, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"startup_point_of_disinfection_g03", "Startup point of disinfection [G03]", "G03", 1048, DecodeType::RAW, "h", "duration", PollGroup::SLOW, true, false, false, 0},
  {"circle_of_disinfection_g04", "Circle of disinfection [G04]", "G04", 1049, DecodeType::RAW, "d", "duration", PollGroup::SLOW, true, false, false, 0},
  {"remenber_the_status_of_device_when_power_down_h01", "Remenber the status of device when power down [H01]", "H01", 1067, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"heating_source_h03", "Heating source [H03]", "H03", 1069, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"temperature_unit_h07", "Temperature unit [H07]", "H07", 1073, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"device_address_h30", "Device address [H30]", "H30", 1076, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"intelligent_control_mode_h31", "Intelligent control mode [H31]", "H31", 1077, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"circle_of_submitting_data_to_cloud_h32", "Circle of submitting data to Cloud [H32]", "H32", 1066, DecodeType::RAW, "min", "duration", PollGroup::SLOW, true, false, false, 0},
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
  {"hysteresis_of_heat_pump_startup_bottom_sensor_r03", "Hysteresis of heat pump startup(bottom sensor) [R03]", "R03", 1106, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"enable_r05_as_setpoint_of_booster_r04", "Enable R05 as setpoint of booster? [R04]", "R04", 1107, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_booster_r05", "Setpoint of booster [R05]", "R05", 1108, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"booster_startup_delay_r06", "Booster startup delay [R06]", "R06", 1109, DecodeType::DIGI4, "min", "duration", PollGroup::SLOW, true, false, false, 0},
  {"booster_replaces_heat_pump_r07", "Booster replaces heat pump? [R07]", "R07", 1110, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_ambient_temp_to_activate_booster_to_replace_heat_pump_r08", "Setpoint of ambient temp to activate booster to replace heat pump [R08]", "R08", 1111, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_ambient_temp_to_activate_booster_without_delay_r09", "Setpoint of ambient temp to activate booster without delay [R09]", "R09", 1112, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_of_ambient_temp_to_activate_booster_with_delay_r10", "Setpoint of ambient temp to activate booster with delay [R10]", "R10", 1113, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"ambient_temp_of_shutting_down_compressor_compulsively_r12", "Ambient temp of shutting down compressor compulsively [R12]", "R12", 1115, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"the_target_temp_of_second_heating_source_r14", "The target temp of second heating source [R14]", "R14", 1117, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"maximal_ambient_temp_of_working_compressor_r15", "Maximal ambient temp of working compressor [R15]", "R15", 1118, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"enable_top_sensor_to_control_compressor_r17", "Enable top sensor to control compressor? [R17]", "R17", 1120, DecodeType::RAW, "", "", PollGroup::SLOW, true, false, false, 0},
  {"hysteresis_of_heat_pump_startup_top_sensor_r18", "Hysteresis of heat pump startup(top sensor) [R18]", "R18", 1121, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_1_of_ambient_temp_to_stop_compressor_r19", "Setpoint 1 of ambient temp to stop compressor [R19]", "R19", 1122, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"setpoint_2_of_ambient_temp_to_stop_compressor_r20", "Setpoint 2 of ambient temp to stop compressor [R20]", "R20", 1123, DecodeType::TEMP1, "°C", "temperature", PollGroup::SLOW, true, false, false, 0},
  {"eev_current_position_o07", "EEV current position [O07]", "O07", 2060, DecodeType::RAW, "", "", PollGroup::FAST, true, false, false, 0},
  {"accumulative_running_time_of_compressor_o08", "Accumulative running time of compressor [O08]", "O08", 2061, DecodeType::RAW, "h", "duration", PollGroup::FAST, true, false, false, 0},
  {"accumulative_running_time_of_booster_o09", "Accumulative running time of booster [O09]", "O09", 2062, DecodeType::RAW, "h", "duration", PollGroup::FAST, true, false, false, 0},
  {"ambient_temperature_t01", "Ambient temperature [T01]", "T01", 2019, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
  {"bottom_temperature_t02", "Bottom temperature [T02]", "T02", 2020, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
  {"top_temperature_t03", "Top temperature [T03]", "T03", 2021, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
  {"coil_temperature_t04", "Coil temperature [T04]", "T04", 2022, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, true, false, false, 0},
  {"suction_temperature_t05", "Suction temperature [T05]", "T05", 2023, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, true, false, false, 0},
  {"solar_temperature_t06", "Solar temperature [T06]", "T06", 2024, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, true, false, false, 0},
  {"temperature_value_shown_on_app_display_t10", "Temperature value shown on APP/display [T10]", "T10", 2025, DecodeType::TEMP1, "°C", "temperature", PollGroup::FAST, false, false, false, 0},
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
  {"dtu_wifishi_fou_shang_xian_o13", "DTU&WIFI是否上线 [O13]", "O13", 2051, 1},
  {"chu_shuang_defrost_o14", "除霜Defrost [O14]", "O14", 2051, 2},
  {"xi_tong_shi_fou_jin_ru_gao_wen_re_shui_jie_duan_o15", "系统是否进入高温热水阶段 [O15]", "O15", 2051, 3},
  {"remote_on_off_switch_s01", "Remote ON/OFF switch [S01]", "S01", 2050, 0},
  {"over_heat_protection_switch_s02", "Over heat protection switch [S02]", "S02", 2050, 1},
  {"low_pressure_switch_s03", "Low pressure switch [S03]", "S03", 2050, 2},
  {"high_pressure_switch_s04", "High pressure switch [S04]", "S04", 2050, 3},
  {"accelerate_the_running_time_of_heater_s05", "Accelerate the running time of heater [S05]", "S05", 2050, 4},
  {"second_heating_source_s06", "Second heating source [S06]", "S06", 2050, 5},
};

const size_t BIT_SENSOR_COUNT = sizeof(bitSensors) / sizeof(bitSensors[0]);

LegacyPlaceholderSensor placeholderSensors[] = {
  {"jin_ru_can_shu_chao_fan_wei_bao_hu_ci_shu_t11", "进入参数超范围保护次数 [T11]", "T11"},
  {"ji_yi_xin_pian_eepromcun_chu_ci_shu_t12", "记忆芯片EEPROM存储次数 [T12]", "T12"},
};

const size_t PLACEHOLDER_SENSOR_COUNT =
  sizeof(placeholderSensors) / sizeof(placeholderSensors[0]);

HwsState hws;

// ============================================================================
// COUNTERS / SCHEDULING
// ============================================================================

uint32_t modbusSuccessCount = 0;
uint32_t modbusFailureCount = 0;
uint32_t modbusWriteSuccessCount = 0;
uint32_t modbusWriteFailureCount = 0;
uint32_t fastPollCount = 0;
uint32_t slowPollCount = 0;

unsigned long lastFastPollMs = 0;
unsigned long lastSlowPollMs = 0;
unsigned long lastGoodModbusMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastWriteMs = 0;

String lastWriteSummary = "None";

String clockSyncStatus = "Waiting for Monday 01:00";
String lastClockCheckLocal = "Never";
String lastClockCorrectionLocal = "Never";
String lastClockCommandTarget = "Never";

int weeklyClockSuccessYear = -1;
int weeklyClockSuccessYday = -1;
int weeklyClockAttemptYear = -1;
int weeklyClockAttemptYday = -1;
int weeklyClockAttemptBucket = -1;

// ============================================================================
// MODBUS
// ============================================================================

uint16_t modbusCRC16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;

  for (size_t pos = 0; pos < length; pos++) {
    crc ^= data[pos];

    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

String bytesToHex(const uint8_t *data, size_t length) {
  String out;
  out.reserve(length * 3);
  char b[4];

  for (size_t i = 0; i < length; i++) {
    snprintf(b, sizeof(b), "%02X", data[i]);
    out += b;
    if (i + 1 < length) out += " ";
  }

  return out;
}

void rs485ReceiveMode() {
  digitalWrite(RS485_EN_PIN, LOW);
}

void rs485TransmitMode() {
  digitalWrite(RS485_EN_PIN, HIGH);
}

/*
  Proven Waveshare direction sequence for both reads and writes:
      GPIO21 HIGH
      transmit full RTU frame
      RS485.flush()
      short guard delay
      GPIO21 LOW
      receive

  Reads use function 03.
  Verified single-register controls use function 06.
  The proven controller-clock update uses narrowly-scoped function 16 writes only
  for clock registers 1151-1156.
*/
bool readHoldingRegister(uint16_t address, uint16_t &value) {
  uint8_t request[8] = {
    MODBUS_SLAVE,
    0x03,
    static_cast<uint8_t>((address >> 8) & 0xFF),
    static_cast<uint8_t>(address & 0xFF),
    0x00,
    0x01,
    0x00,
    0x00
  };

  uint16_t crc = modbusCRC16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  while (RS485.available()) RS485.read();

  rs485TransmitMode();
  delayMicroseconds(200);

  if (LOG_MODBUS_FRAMES) {
    addLog("TX [" + String(address) + "]: " + bytesToHex(request, sizeof(request)));
  }

  RS485.write(request, sizeof(request));
  RS485.flush();
  delayMicroseconds(200);
  rs485ReceiveMode();

  uint8_t response[16];
  size_t received = 0;
  unsigned long start = millis();

  while ((millis() - start) < MODBUS_TIMEOUT_MS && received < sizeof(response)) {
    while (RS485.available() && received < sizeof(response)) {
      response[received++] = static_cast<uint8_t>(RS485.read());
    }

    if (received >= 7) break;
    if (received >= 5 && (response[1] & 0x80)) break;
    delay(1);
  }

  if (received == 0) {
    modbusFailureCount++;
    addLog("RX [" + String(address) + "]: NO RESPONSE");
    return false;
  }

  if (LOG_MODBUS_FRAMES) {
    addLog("RX [" + String(address) + "]: " + bytesToHex(response, received));
  }

  if (received >= 5 &&
      response[0] == MODBUS_SLAVE &&
      response[1] == (0x03 | 0x80)) {
    modbusFailureCount++;
    addLog("MODBUS EXCEPTION [" + String(address) + "]: code " + String(response[2]));
    return false;
  }

  if (received < 7) {
    modbusFailureCount++;
    addLog("BAD RX [" + String(address) + "]: short frame");
    return false;
  }

  if (response[0] != MODBUS_SLAVE || response[1] != 0x03 || response[2] != 0x02) {
    modbusFailureCount++;
    addLog("BAD RX [" + String(address) + "]: unexpected slave/function/length");
    return false;
  }

  uint16_t receivedCrc =
    static_cast<uint16_t>(response[5]) |
    (static_cast<uint16_t>(response[6]) << 8);

  uint16_t calculatedCrc = modbusCRC16(response, 5);

  if (receivedCrc != calculatedCrc) {
    modbusFailureCount++;
    addLog("BAD RX [" + String(address) + "]: CRC mismatch");
    return false;
  }

  value = (static_cast<uint16_t>(response[3]) << 8) | response[4];

  modbusSuccessCount++;
  lastGoodModbusMs = millis();
  return true;
}

bool readHoldingRegisterFromSlave(uint8_t slave, uint16_t address, uint16_t &value) {
  uint8_t request[8] = {
    slave,
    0x03,
    static_cast<uint8_t>((address >> 8) & 0xFF),
    static_cast<uint8_t>(address & 0xFF),
    0x00,
    0x01,
    0x00,
    0x00
  };

  uint16_t crc = modbusCRC16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  while (RS485.available()) RS485.read();

  rs485TransmitMode();
  delayMicroseconds(200);

  if (LOG_MODBUS_FRAMES) {
    addLog("DIAG TX slave=" + String(slave) + " reg=" + String(address) +
           ": " + bytesToHex(request, sizeof(request)));
  }

  RS485.write(request, sizeof(request));
  RS485.flush();
  delayMicroseconds(200);
  rs485ReceiveMode();

  uint8_t response[16];
  size_t received = 0;
  unsigned long start = millis();

  while ((millis() - start) < MODBUS_TIMEOUT_MS && received < sizeof(response)) {
    while (RS485.available() && received < sizeof(response)) {
      response[received++] = static_cast<uint8_t>(RS485.read());
    }

    if (received >= 7) break;
    if (received >= 5 && (response[1] & 0x80)) break;
    delay(1);
  }

  if (received == 0) {
    addLog("DIAG RX slave=" + String(slave) + " reg=" + String(address) + ": NO RESPONSE");
    return false;
  }

  if (LOG_MODBUS_FRAMES) {
    addLog("DIAG RX slave=" + String(slave) + " reg=" + String(address) +
           ": " + bytesToHex(response, received));
  }

  if (received >= 5 &&
      response[0] == slave &&
      response[1] == (0x03 | 0x80)) {
    addLog("DIAG MODBUS EXCEPTION slave=" + String(slave) +
           " reg=" + String(address) + " code=" + String(response[2]));
    return false;
  }

  if (received < 7) {
    addLog("DIAG BAD RX slave=" + String(slave) + " reg=" + String(address) +
           ": short frame");
    return false;
  }

  if (response[0] != slave || response[1] != 0x03 || response[2] != 0x02) {
    addLog("DIAG BAD RX slave=" + String(slave) + " reg=" + String(address) +
           ": unexpected slave/function/length");
    return false;
  }

  uint16_t receivedCrc =
    static_cast<uint16_t>(response[5]) |
    (static_cast<uint16_t>(response[6]) << 8);

  uint16_t calculatedCrc = modbusCRC16(response, 5);
  if (receivedCrc != calculatedCrc) {
    addLog("DIAG BAD RX slave=" + String(slave) + " reg=" + String(address) +
           ": CRC mismatch");
    return false;
  }

  value = (static_cast<uint16_t>(response[3]) << 8) | response[4];
  return true;
}

bool writeMultipleRegistersFrame(
  uint16_t startAddress,
  const uint16_t *values,
  uint16_t count,
  const String &label
) {
  if (count == 0 || count > 20) {
    addLog("FC16 " + label + " refused: invalid register count " + String(count));
    return false;
  }

  const size_t requestLen = 9 + (count * 2);
  uint8_t request[49]; // max count 20 => 49 bytes
  request[0] = MODBUS_SLAVE;
  request[1] = 0x10;
  request[2] = static_cast<uint8_t>((startAddress >> 8) & 0xFF);
  request[3] = static_cast<uint8_t>(startAddress & 0xFF);
  request[4] = static_cast<uint8_t>((count >> 8) & 0xFF);
  request[5] = static_cast<uint8_t>(count & 0xFF);
  request[6] = static_cast<uint8_t>(count * 2);

  for (uint16_t i = 0; i < count; i++) {
    request[7 + (i * 2)] = static_cast<uint8_t>((values[i] >> 8) & 0xFF);
    request[8 + (i * 2)] = static_cast<uint8_t>(values[i] & 0xFF);
  }

  uint16_t crc = modbusCRC16(request, 7 + count * 2);
  request[7 + count * 2] = crc & 0xFF;
  request[8 + count * 2] = (crc >> 8) & 0xFF;

  while (RS485.available()) RS485.read();

  rs485TransmitMode();
  delayMicroseconds(200);

  addLog(
    "CLOCK TEST FC16 TX " + label +
    " start=" + String(startAddress) +
    " count=" + String(count) +
    ": " + bytesToHex(request, requestLen)
  );

  RS485.write(request, requestLen);
  RS485.flush();
  delayMicroseconds(200);
  rs485ReceiveMode();

  uint8_t response[16];
  size_t received = 0;
  unsigned long start = millis();

  while ((millis() - start) < MODBUS_TIMEOUT_MS && received < sizeof(response)) {
    while (RS485.available() && received < sizeof(response)) {
      response[received++] = static_cast<uint8_t>(RS485.read());
    }
    if (received >= 8) break;
    if (received >= 5 && (response[1] & 0x80)) break;
    delay(1);
  }

  if (received == 0) {
    addLog("CLOCK TEST FC16 RX " + label + ": NO RESPONSE");
    return false;
  }

  addLog("CLOCK TEST FC16 RX " + label + ": " + bytesToHex(response, received));

  if (received >= 5 &&
      response[0] == MODBUS_SLAVE &&
      response[1] == (0x10 | 0x80)) {
    addLog("CLOCK TEST FC16 EXCEPTION " + label + ": code " + String(response[2]));
    return false;
  }

  if (received < 8 || response[0] != MODBUS_SLAVE || response[1] != 0x10) {
    addLog("CLOCK TEST FC16 BAD RX " + label + ": unexpected response");
    return false;
  }

  uint16_t receivedCrc =
    static_cast<uint16_t>(response[6]) |
    (static_cast<uint16_t>(response[7]) << 8);
  uint16_t calculatedCrc = modbusCRC16(response, 6);
  if (receivedCrc != calculatedCrc) {
    addLog("CLOCK TEST FC16 BAD RX " + label + ": CRC mismatch");
    return false;
  }

  uint16_t echoedStart =
    (static_cast<uint16_t>(response[2]) << 8) | response[3];
  uint16_t echoedCount =
    (static_cast<uint16_t>(response[4]) << 8) | response[5];

  if (echoedStart != startAddress || echoedCount != count) {
    addLog("CLOCK TEST FC16 BAD ECHO " + label);
    return false;
  }

  addLog("CLOCK TEST FC16 ACK " + label + " accepted.");
  return true;
}

bool writeSingleRegisterFrame(uint16_t address, uint16_t value) {
  uint8_t request[8] = {
    MODBUS_SLAVE,
    0x06,
    static_cast<uint8_t>((address >> 8) & 0xFF),
    static_cast<uint8_t>(address & 0xFF),
    static_cast<uint8_t>((value >> 8) & 0xFF),
    static_cast<uint8_t>(value & 0xFF),
    0x00,
    0x00
  };

  uint16_t crc = modbusCRC16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  while (RS485.available()) RS485.read();

  rs485TransmitMode();
  delayMicroseconds(200);

  addLog(
    "WRITE TX [" + String(address) + "] value=" + String(value) +
    ": " + bytesToHex(request, sizeof(request))
  );

  RS485.write(request, sizeof(request));
  RS485.flush();
  delayMicroseconds(200);
  rs485ReceiveMode();

  uint8_t response[16];
  size_t received = 0;
  unsigned long start = millis();

  while ((millis() - start) < MODBUS_TIMEOUT_MS && received < sizeof(response)) {
    while (RS485.available() && received < sizeof(response)) {
      response[received++] = static_cast<uint8_t>(RS485.read());
    }

    if (received >= 8) break;
    if (received >= 5 && (response[1] & 0x80)) break;
    delay(1);
  }

  if (received == 0) {
    addLog("WRITE RX [" + String(address) + "]: NO RESPONSE");
    return false;
  }

  addLog("WRITE RX [" + String(address) + "]: " + bytesToHex(response, received));

  if (received >= 5 &&
      response[0] == MODBUS_SLAVE &&
      response[1] == (0x06 | 0x80)) {
    addLog("WRITE MODBUS EXCEPTION [" + String(address) + "]: code " + String(response[2]));
    return false;
  }

  if (received < 8) {
    addLog("WRITE BAD RX [" + String(address) + "]: short frame");
    return false;
  }

  if (response[0] != MODBUS_SLAVE || response[1] != 0x06) {
    addLog("WRITE BAD RX [" + String(address) + "]: unexpected slave/function");
    return false;
  }

  uint16_t receivedCrc =
    static_cast<uint16_t>(response[6]) |
    (static_cast<uint16_t>(response[7]) << 8);

  uint16_t calculatedCrc = modbusCRC16(response, 6);

  if (receivedCrc != calculatedCrc) {
    addLog("WRITE BAD RX [" + String(address) + "]: CRC mismatch");
    return false;
  }

  uint16_t echoedAddress =
    (static_cast<uint16_t>(response[2]) << 8) |
    response[3];

  uint16_t echoedValue =
    (static_cast<uint16_t>(response[4]) << 8) |
    response[5];

  if (echoedAddress != address || echoedValue != value) {
    addLog(
      "WRITE BAD ECHO [" + String(address) + "] addr=" +
      String(echoedAddress) + " value=" + String(echoedValue)
    );
    return false;
  }

  return true;
}

bool writeRegisterVerified(
  uint16_t address,
  uint16_t requestedRaw,
  const String &label,
  uint16_t &verifiedRaw
) {
  addLog(
    "WRITE REQUEST " + label +
    " register=" + String(address) +
    " raw=" + String(requestedRaw)
  );

  if (!writeSingleRegisterFrame(address, requestedRaw)) {
    modbusWriteFailureCount++;
    lastWriteSummary = label + " FAILED - no valid FC06 acknowledgement";
    lastWriteMs = millis();
    addLog(lastWriteSummary);
    return false;
  }

  delay(120);

  uint16_t readBack = 0;
  if (!readHoldingRegister(address, readBack)) {
    modbusWriteFailureCount++;
    lastWriteSummary = label + " FAILED - FC03 read-back failed";
    lastWriteMs = millis();
    addLog(lastWriteSummary);
    return false;
  }

  if (readBack != requestedRaw) {
    modbusWriteFailureCount++;
    lastWriteSummary =
      label + " FAILED - read-back " + String(readBack) +
      " != requested " + String(requestedRaw);
    lastWriteMs = millis();
    addLog(lastWriteSummary);
    return false;
  }

  verifiedRaw = readBack;
  modbusWriteSuccessCount++;
  lastWriteMs = millis();
  lastWriteSummary =
    label + " OK - register " + String(address) +
    " verified raw=" + String(readBack);
  addLog(lastWriteSummary);
  return true;
}

bool bitIsSet(uint16_t value, uint8_t bit) {
  return (value & (1U << bit)) != 0;
}

float decodeValue(uint16_t raw, DecodeType type) {
  switch (type) {
    case DecodeType::TEMP1:
      return (static_cast<int32_t>(raw) - 60) * 0.5f;

    case DecodeType::TEMP:
      return static_cast<int16_t>(raw) * 0.1f;

    case DecodeType::DIGI2:
      return raw * 10.0f;

    case DecodeType::DIGI4:
      return raw * 5.0f;

    case DecodeType::DIGI7:
      return raw * 0.5f;

    case DecodeType::RAW:
    default:
      return static_cast<float>(raw);
  }
}

String modeName(uint16_t raw) {
  switch (raw) {
    case 0: return "Intelligent";
    case 2: return "Economic";
    case 3: return "Hybrid";
    case 4: return "High Demand";
    case 7: return "Vacation";
    default: return "Unknown (" + String(raw) + ")";
  }
}

bool modeRawFromName(String name, uint16_t &raw) {
  name.trim();

  if (name == "Intelligent") {
    raw = 0;
    return true;
  }
  if (name == "Economic") {
    raw = 2;
    return true;
  }
  if (name == "Hybrid") {
    raw = 3;
    return true;
  }
  if (name == "High Demand") {
    raw = 4;
    return true;
  }
  if (name == "Vacation") {
    raw = 7;
    return true;
  }

  return false;
}

bool parseTargetTemperature(String message, float &temperatureC, uint16_t &raw) {
  message.trim();

  if (message.length() == 0) return false;

  char *endPtr = nullptr;
  float requested = strtof(message.c_str(), &endPtr);

  if (endPtr == message.c_str() || *endPtr != '\0' || !isfinite(requested)) {
    return false;
  }

  if (requested < TARGET_TEMP_MIN_C || requested > TARGET_TEMP_MAX_C) {
    return false;
  }

  float stepped = roundf(requested / TARGET_TEMP_STEP_C) * TARGET_TEMP_STEP_C;

  if (fabsf(requested - stepped) > 0.01f) {
    return false;
  }

  // HW211 TEMP1 encoding: C = (raw - 60) * 0.5
  // therefore raw = (C / 0.5) + 60.
  int32_t encoded = lroundf((stepped / 0.5f) + 60.0f);

  if (encoded < 0 || encoded > 65535) {
    return false;
  }

  temperatureC = stepped;
  raw = static_cast<uint16_t>(encoded);
  return true;
}

LegacyRegisterSensor *findSensorByCode(const char *protocolCode) {
  for (size_t i = 0; i < DIRECT_SENSOR_COUNT; i++) {
    if (strcmp(directSensors[i].protocolCode, protocolCode) == 0) {
      return &directSensors[i];
    }
  }
  return nullptr;
}

bool decodedByCode(const char *protocolCode, float &value) {
  LegacyRegisterSensor *sensor = findSensorByCode(protocolCode);
  if (sensor == nullptr || !sensor->valid) return false;

  value = decodeValue(sensor->raw, sensor->decode);
  return true;
}

// ============================================================================
// MQTT TOPICS / JSON
// ============================================================================

String legacySensorStateTopic(const String &slug) {
  return String(MQTT_BASE_TOPIC) + "/state/sensor/" + slug;
}

String binaryStateTopic(const String &slug) {
  return String(MQTT_BASE_TOPIC) + "/state/binary/" + slug;
}

String selectStateTopic(const String &slug) {
  return String(MQTT_BASE_TOPIC) + "/state/select/" + slug;
}

String selectCommandTopic(const String &slug) {
  return String(MQTT_BASE_TOPIC) + "/command/select/" + slug;
}

String climateStateTopic(const String &field) {
  return String(MQTT_BASE_TOPIC) + "/state/climate/" + field;
}

String climateCommandTopic(const String &field) {
  return String(MQTT_BASE_TOPIC) + "/command/climate/" + field;
}


String controlStateTopic(const String &slug) {
  return String(MQTT_BASE_TOPIC) + "/state/control/" + slug;
}

String controlCommandTopic(const String &slug) {
  return String(MQTT_BASE_TOPIC) + "/command/control/" + slug;
}

String discoveryTopic(const String &component, const String &objectId) {
  return "homeassistant/" + component + "/" + String(DEVICE_CODE) + "/" + objectId + "/config";
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  s.replace("\r", "\\r");
  return s;
}

String deviceJson() {
  String s;
  s.reserve(420);

  s += "\"device\":{";
  s += "\"identifiers\":[\"evo270_" + String(DEVICE_CODE) + "\"],";
  s += "\"name\":\"" + jsonEscape(String(DEVICE_NAME)) + "\",";
  s += "\"manufacturer\":\"EvoHeat\",";
  s += "\"model\":\"EVO270-1 / PASHW015-270LD-WG-NO / HW211\",";
  s += "\"serial_number\":\"A022112238161\",";
  s += "\"sw_version\":\"" + String(FW_VERSION) + "\"";
  s += "}";

  return s;
}

String originJson() {
  String s;
  s.reserve(220);

  s += "\"origin\":{";
  s += "\"name\":\"EVO270 Arduino MQTT\",";
  s += "\"sw_version\":\"" + String(FW_VERSION) + "\",";
  s += "\"support_url\":\"https://github.com/sjtrny/esphome-hw211\"";
  s += "}";

  return s;
}

bool publishRetained(const String &topic, const String &payload) {
  if (!mqtt.connected()) return false;

  bool ok = mqtt.publish(topic.c_str(), payload.c_str(), true);
  if (!ok) addLog("MQTT publish failed: " + topic);

  delay(1);
  return ok;
}

String entityPointName(const char *displayName) {
  // MQTT entities have has_entity_name=true in modern Home Assistant.
  // Supply only the data-point name; Home Assistant prefixes the device once.
  return String(displayName);
}

// ============================================================================
// MQTT DISCOVERY - LEGACY SENSORS
// ============================================================================

void publishLegacySensorDiscovery(
  const String &slug,
  const String &displayName,
  const String &unit,
  const String &deviceClass,
  bool diagnostic
) {
  String payload;
  payload.reserve(1500);

  payload += "{";
  payload += "\"name\":\"" + jsonEscape(entityPointName(displayName.c_str())) + "\",";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"default_entity_id\":\"sensor." + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"state_topic\":\"" + legacySensorStateTopic(slug) + "\",";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"payload_available\":\"online\",";
  payload += "\"payload_not_available\":\"offline\",";

  if (unit.length()) {
    payload += "\"unit_of_measurement\":\"" + jsonEscape(unit) + "\",";
  }

  if (deviceClass.length()) {
    payload += "\"device_class\":\"" + jsonEscape(deviceClass) + "\",";
  }

  if (diagnostic) {
    payload += "\"entity_category\":\"diagnostic\",";
  }

  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";

  publishRetained(discoveryTopic("sensor", slug), payload);
}

void publishClockDiagnosticDiscovery() {
  // V2.2.5 incorrectly treated the clock command mailbox as a live clock.
  // Remove those retained discovery configs so Home Assistant does not show
  // misleading "Controller Clock" or "Clock Drift" entities.
  publishRetained(discoveryTopic("sensor", "controller_clock"), "");
  publishRetained(discoveryTopic("sensor", "clock_drift_minutes"), "");
  publishRetained(legacySensorStateTopic("controller_clock"), "");
  publishRetained(legacySensorStateTopic("clock_drift_minutes"), "");

  publishLegacySensorDiscovery(
    "last_clock_command",
    "Last Clock Command",
    "",
    "",
    true
  );
  publishLegacySensorDiscovery(
    "clock_sync_status",
    "Clock Sync Status",
    "",
    "",
    true
  );
}

void publishAllLegacySensorDiscovery() {
  for (size_t i = 0; i < DIRECT_SENSOR_COUNT; i++) {
    LegacyRegisterSensor &s = directSensors[i];

    publishLegacySensorDiscovery(
      s.slug,
      s.displayName,
      s.unit,
      s.deviceClass,
      s.diagnostic
    );
  }

  for (size_t i = 0; i < BIT_SENSOR_COUNT; i++) {
    publishLegacySensorDiscovery(
      bitSensors[i].slug,
      bitSensors[i].displayName,
      "",
      "",
      true
    );
  }

  for (size_t i = 0; i < PLACEHOLDER_SENSOR_COUNT; i++) {
    publishLegacySensorDiscovery(
      placeholderSensors[i].slug,
      placeholderSensors[i].displayName,
      "",
      "",
      true
    );
  }
}

// ============================================================================
// MQTT DISCOVERY - BINARY SENSORS
// ============================================================================

void publishBinaryDiscovery(
  const String &slug,
  const String &displayName,
  const String &deviceClass,
  bool diagnostic
) {
  String payload;
  payload.reserve(1300);

  payload += "{";
  payload += "\"name\":\"" + jsonEscape(displayName) + "\",";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"default_entity_id\":\"binary_sensor." + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"state_topic\":\"" + binaryStateTopic(slug) + "\",";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"payload_available\":\"online\",";
  payload += "\"payload_not_available\":\"offline\",";
  payload += "\"payload_on\":\"ON\",";
  payload += "\"payload_off\":\"OFF\",";

  if (deviceClass.length()) {
    payload += "\"device_class\":\"" + deviceClass + "\",";
  }

  if (diagnostic) {
    payload += "\"entity_category\":\"diagnostic\",";
  }

  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";

  publishRetained(discoveryTopic("binary_sensor", slug), payload);
}

void publishLegacyBinaryDiscovery() {
  publishBinaryDiscovery("api_status", "API Status", "connectivity", true);
  publishBinaryDiscovery("fault", "Fault", "problem", false);
  publishBinaryDiscovery("status", "Status", "connectivity", true);
  publishBinaryDiscovery("power", "Power", "power", false);
}

// ============================================================================
// MQTT DISCOVERY - SELECTS
// ============================================================================

void publishTemperatureUnitSelectDiscovery() {
  String slug = "temperature_unit";
  String payload;
  payload.reserve(1500);

  payload += "{";
  payload += "\"name\":\"" + jsonEscape(String("Temperature Unit (read-only)")) + "\",";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "_temperature_unit\",";
  payload += "\"default_entity_id\":\"select." + String(DEVICE_CODE) + "_temperature_unit\",";
  payload += "\"state_topic\":\"" + selectStateTopic(slug) + "\",";
  payload += "\"command_topic\":\"" + selectCommandTopic(slug) + "\",";
  payload += "\"options\":[\"°C\",\"°F\"],";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"entity_category\":\"config\",";
  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";

  publishRetained(discoveryTopic("select", slug), payload);
}

void publishOperatingModeSelectDiscovery() {
  String slug = "operating_mode";
  String payload;
  payload.reserve(1700);

  payload += "{";
  payload += "\"name\":\"" + jsonEscape(String("Operating Mode")) + "\",";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "_operating_mode\",";
  payload += "\"default_entity_id\":\"select.back_fence_garden_evo270_ensuite_kitchen_" +
    String(DEVICE_CODE) + "_" + String(DEVICE_CODE) + "_operating_mode\",";
  payload += "\"state_topic\":\"" + selectStateTopic(slug) + "\",";
  payload += "\"command_topic\":\"" + selectCommandTopic(slug) + "\",";
  payload += "\"options\":[\"Intelligent\",\"Economic\",\"Hybrid\",\"High Demand\",\"Vacation\"],";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"entity_category\":\"config\",";
  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";

  publishRetained(discoveryTopic("select", slug), payload);
}


// ============================================================================
// MQTT DISCOVERY - VACATION / TIMER CONTROLS
// ============================================================================

void publishControlSwitchDiscovery(const String &slug, const String &displayName) {
  String payload;
  payload.reserve(1500);
  payload += "{";
  payload += "\"name\":\"" + jsonEscape(displayName) + "\",";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"default_entity_id\":\"switch." + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"state_topic\":\"" + controlStateTopic(slug) + "\",";
  payload += "\"command_topic\":\"" + controlCommandTopic(slug) + "\",";
  payload += "\"payload_on\":\"ON\",";
  payload += "\"payload_off\":\"OFF\",";
  payload += "\"state_on\":\"ON\",";
  payload += "\"state_off\":\"OFF\",";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"entity_category\":\"config\",";
  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";
  publishRetained(discoveryTopic("switch", slug), payload);
}

void publishControlTimeDiscovery(const String &slug, const String &displayName) {
  String payload;
  payload.reserve(1500);
  payload += "{";
  payload += "\"name\":\"" + jsonEscape(displayName) + "\",";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"default_entity_id\":\"time." + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"state_topic\":\"" + controlStateTopic(slug) + "\",";
  payload += "\"command_topic\":\"" + controlCommandTopic(slug) + "\",";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"entity_category\":\"config\",";
  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";
  publishRetained(discoveryTopic("time", slug), payload);
}

void publishControlDateDiscovery(const String &slug, const String &displayName) {
  String payload;
  payload.reserve(1500);
  payload += "{";
  payload += "\"name\":\"" + jsonEscape(displayName) + "\",";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"default_entity_id\":\"date." + String(DEVICE_CODE) + "_" + slug + "\",";
  payload += "\"state_topic\":\"" + controlStateTopic(slug) + "\",";
  payload += "\"command_topic\":\"" + controlCommandTopic(slug) + "\",";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"entity_category\":\"config\",";
  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";
  publishRetained(discoveryTopic("date", slug), payload);
}

void publishVacationTimerDiscovery() {
  publishControlSwitchDiscovery("vacation_date_enabled", "Vacation Date Enabled");
  publishControlDateDiscovery("vacation_date", "Vacation Date");

  publishControlSwitchDiscovery("timer_1_on_enabled", "Timer 1 ON Enabled");
  publishControlTimeDiscovery("timer_1_on", "Timer 1 ON Time");
  publishControlSwitchDiscovery("timer_1_off_enabled", "Timer 1 OFF Enabled");
  publishControlTimeDiscovery("timer_1_off", "Timer 1 OFF Time");

  publishControlSwitchDiscovery("timer_2_on_enabled", "Timer 2 ON Enabled");
  publishControlTimeDiscovery("timer_2_on", "Timer 2 ON Time");
  publishControlSwitchDiscovery("timer_2_off_enabled", "Timer 2 OFF Enabled");
  publishControlTimeDiscovery("timer_2_off", "Timer 2 OFF Time");
}

// ============================================================================
// MQTT DISCOVERY - CLIMATE
// ============================================================================

void publishClimateDiscovery() {
  String payload;
  payload.reserve(2600);

  payload += "{";
  payload += "\"name\":null,";
  payload += "\"unique_id\":\"" + String(DEVICE_CODE) + "\",";
  payload += "\"default_entity_id\":\"climate." + String(DEVICE_CODE) + "\",";
  payload += "\"availability_topic\":\"" + String(MQTT_AVAIL_TOPIC) + "\",";
  payload += "\"modes\":[\"off\",\"heat\"],";
  payload += "\"mode_state_topic\":\"" + climateStateTopic("mode") + "\",";
  payload += "\"mode_command_topic\":\"" + climateCommandTopic("mode") + "\",";
  payload += "\"current_temperature_topic\":\"" + climateStateTopic("current_temperature") + "\",";
  payload += "\"temperature_state_topic\":\"" + climateStateTopic("target_temperature") + "\",";
  payload += "\"temperature_command_topic\":\"" + climateCommandTopic("target_temperature") + "\",";
  payload += "\"action_topic\":\"" + climateStateTopic("action") + "\",";
  payload += "\"temperature_unit\":\"C\",";
  payload += "\"min_temp\":10.0,";
  payload += "\"max_temp\":60.0,";
  payload += "\"temp_step\":0.5,";
  payload += "\"precision\":0.5,";
  payload += "\"optimistic\":false,";
  payload += deviceJson() + ",";
  payload += originJson();
  payload += "}";

  publishRetained(discoveryTopic("climate", "main"), payload);
}

void publishAllDiscovery() {
  if (!mqtt.connected()) return;

  addLog("Publishing full legacy MQTT Discovery map...");

  publishLegacyBinaryDiscovery();
  publishClimateDiscovery();
  publishTemperatureUnitSelectDiscovery();
  publishOperatingModeSelectDiscovery();
  publishVacationTimerDiscovery();
  publishClockDiagnosticDiscovery();
  publishAllLegacySensorDiscovery();

  addLog(
    "Legacy MQTT Discovery published: " +
    String(DIRECT_SENSOR_COUNT + BIT_SENSOR_COUNT + PLACEHOLDER_SENSOR_COUNT + 16) +
    " entities."
  );
}

// ============================================================================
// REMOVE V2.0 TEST DISCOVERY
// ============================================================================

void removeDiscoveryConfig(const char *component, const char *objectId) {
  String topic =
    "homeassistant/" + String(component) + "/evo270_ensuite_kitchen_" +
    String(objectId) + "/config";

  mqtt.publish(topic.c_str(), "", true);
  delay(2);
}

void cleanupV20Discovery() {
  if (!REMOVE_V20_TEST_DISCOVERY || !mqtt.connected()) return;

  static const char *oldSensors[] = {
    "ambient_temperature",
    "bottom_tank_temperature",
    "top_tank_temperature",
    "water_temperature",
    "target_temperature",
    "operation_mode",
    "wifi_rssi",
    "modbus_successes",
    "modbus_failures",
    "status0_raw",
    "status1_raw",
    "fault0_raw"
  };

  static const char *oldBinary[] = {
    "power",
    "compressor",
    "electric_heater",
    "fan_high",
    "fan_low",
    "defrost",
    "hot_water_active",
    "shutdown",
    "fault_active",
    "fault_ambient_sensor",
    "fault_bottom_sensor",
    "fault_top_sensor",
    "fault_coil_sensor",
    "fault_suction_sensor",
    "fault_solar_sensor",
    "fault_high_pressure",
    "fault_low_pressure",
    "fault_antifreeze",
    "modbus_link"
  };

  for (const char *id : oldSensors) removeDiscoveryConfig("sensor", id);
  for (const char *id : oldBinary) removeDiscoveryConfig("binary_sensor", id);

  addLog("Removed retained MQTT Discovery configs from the temporary V2.0 test entity set.");
}

// ============================================================================
// MQTT STATE PUBLISHING
// ============================================================================

void publishLegacyNumericSensor(const LegacyRegisterSensor &s) {
  if (!mqtt.connected() || !s.valid) return;

  float value = decodeValue(s.raw, s.decode);

  // Aqua Temp represented these values as floats. One decimal preserves that
  // appearance and avoids integer/string churn in Home Assistant.
  publishRetained(
    legacySensorStateTopic(s.slug),
    String(value, static_cast<unsigned int>(1))
  );
}

void publishPlaceholderStates() {
  if (!mqtt.connected()) return;

  for (size_t i = 0; i < PLACEHOLDER_SENSOR_COUNT; i++) {
    // MQTT Sensor interprets the literal payload None as unknown.
    publishRetained(
      legacySensorStateTopic(placeholderSensors[i].slug),
      "None"
    );
  }
}

void publishLegacyBitSensorStates() {
  if (!mqtt.connected()) return;

  for (size_t i = 0; i < BIT_SENSOR_COUNT; i++) {
    LegacyBitSensor &s = bitSensors[i];

    RegisterValue *source = nullptr;

    if (s.sourceRegister == REG_STATUS0) source = &hws.status0;
    if (s.sourceRegister == REG_STATUS1) source = &hws.status1;

    if (source == nullptr || !source->valid) continue;

    publishRetained(
      legacySensorStateTopic(s.slug),
      bitIsSet(source->raw, s.bit) ? "1.0" : "0.0"
    );
  }
}

String currentHvacMode() {
  if (!hws.power.valid || hws.power.raw == 0) return "off";
  if (hws.actualMode.valid && hws.actualMode.raw == 7) return "off";
  return "heat";
}

String currentHvacAction() {
  if (currentHvacMode() == "off") return "off";

  if (hws.status0.valid) {
    bool compressor = bitIsSet(hws.status0.raw, 8);
    bool element = bitIsSet(hws.status0.raw, 9);

    if (compressor || element) return "heating";
  }

  return "idle";
}

String currentFanMode() {
  if (!hws.status0.valid) return "off";

  if (bitIsSet(hws.status0.raw, 11)) return "high";
  if (bitIsSet(hws.status0.raw, 12)) return "low";

  return "off";
}

void publishSpecialStates() {
  if (!mqtt.connected()) return;

  // Old API Status now means the local MQTT/network integration is online.
  publishRetained(binaryStateTopic("api_status"), "ON");

  // Modbus connectivity/status follows the known-good T01 read on each fast poll.
  LegacyRegisterSensor *ambient = findSensorByCode("T01");
  if (ambient != nullptr) {
    publishRetained(
      binaryStateTopic("status"),
      ambient->lastReadOk ? "ON" : "OFF"
    );
  }

  if (hws.power.valid) {
    publishRetained(
      binaryStateTopic("power"),
      hws.power.raw == 1 ? "ON" : "OFF"
    );
  }

  if (hws.fault0.valid) {
    publishRetained(
      binaryStateTopic("fault"),
      hws.fault0.raw != 0 ? "ON" : "OFF"
    );
  }

  if (hws.actualMode.valid) {
    publishRetained(
      selectStateTopic("operating_mode"),
      modeName(hws.actualMode.raw)
    );
  }

  LegacyRegisterSensor *h07 = findSensorByCode("H07");
  if (h07 != nullptr && h07->valid) {
    publishRetained(
      selectStateTopic("temperature_unit"),
      h07->raw == 1 ? "°F" : "°C"
    );
  }

  publishRetained(climateStateTopic("mode"), currentHvacMode());
  publishRetained(climateStateTopic("action"), currentHvacAction());

  float currentTemp = 0.0f;
  if (decodedByCode("T10", currentTemp)) {
    publishRetained(
      climateStateTopic("current_temperature"),
      String(currentTemp, static_cast<unsigned int>(1))
    );
  }

  float targetTemp = 0.0f;
  if (decodedByCode("R01", targetTemp)) {
    publishRetained(
      climateStateTopic("target_temperature"),
      String(targetTemp, static_cast<unsigned int>(1))
    );
  }

  publishLegacyBitSensorStates();
}


String vacationDateState() {
  DiagnosticRegister *year = findVacationTimerDiag(1130);
  DiagnosticRegister *month = findVacationTimerDiag(1131);
  DiagnosticRegister *day = findVacationTimerDiag(1132);
  if (!year || !month || !day || !year->valid || !month->valid || !day->valid) return "";
  if (year->raw > 99 || month->raw < 1 || month->raw > 12 || day->raw < 1 || day->raw > 31) return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "20%02u-%02u-%02u",
           static_cast<unsigned int>(year->raw),
           static_cast<unsigned int>(month->raw),
           static_cast<unsigned int>(day->raw));
  return String(buf);
}

String timerTimeState(uint16_t hourAddress, uint16_t minuteAddress) {
  DiagnosticRegister *hour = findVacationTimerDiag(hourAddress);
  DiagnosticRegister *minute = findVacationTimerDiag(minuteAddress);
  if (!hour || !minute || !hour->valid || !minute->valid) return "";
  if (hour->raw > 23 || minute->raw > 59) return "";
  char buf[12];
  snprintf(buf, sizeof(buf), "%02u:%02u:00",
           static_cast<unsigned int>(hour->raw),
           static_cast<unsigned int>(minute->raw));
  return String(buf);
}

void publishVacationTimerStates() {
  if (!mqtt.connected()) return;

  DiagnosticRegister *l01 = findVacationTimerDiag(1129);
  DiagnosticRegister *l05 = findVacationTimerDiag(1133);

  if (l01 && l01->valid) {
    publishRetained(controlStateTopic("vacation_date_enabled"), bitIsSet(l01->raw, 0) ? "ON" : "OFF");
  }

  String vacationDate = vacationDateState();
  if (vacationDate.length()) {
    publishRetained(controlStateTopic("vacation_date"), vacationDate);
  }

  if (l05 && l05->valid) {
    publishRetained(controlStateTopic("timer_1_on_enabled"),  bitIsSet(l05->raw, 0) ? "ON" : "OFF");
    publishRetained(controlStateTopic("timer_1_off_enabled"), bitIsSet(l05->raw, 1) ? "ON" : "OFF");
    publishRetained(controlStateTopic("timer_2_on_enabled"),  bitIsSet(l05->raw, 2) ? "ON" : "OFF");
    publishRetained(controlStateTopic("timer_2_off_enabled"), bitIsSet(l05->raw, 3) ? "ON" : "OFF");
  }

  String value;
  value = timerTimeState(1134, 1135); if (value.length()) publishRetained(controlStateTopic("timer_1_on"), value);
  value = timerTimeState(1136, 1137); if (value.length()) publishRetained(controlStateTopic("timer_1_off"), value);
  value = timerTimeState(1138, 1139); if (value.length()) publishRetained(controlStateTopic("timer_2_on"), value);
  value = timerTimeState(1140, 1141); if (value.length()) publishRetained(controlStateTopic("timer_2_off"), value);
}


void publishClockDiagnosticStates() {
  if (!mqtt.connected()) return;

  publishRetained(
    legacySensorStateTopic("last_clock_command"),
    lastClockCommandTarget
  );

  publishRetained(
    legacySensorStateTopic("clock_sync_status"),
    clockSyncStatus
  );
}

void publishAllKnownStates() {
  if (!mqtt.connected()) return;

  for (size_t i = 0; i < DIRECT_SENSOR_COUNT; i++) {
    if (directSensors[i].valid) publishLegacyNumericSensor(directSensors[i]);
  }

  publishPlaceholderStates();
  publishSpecialStates();
  publishVacationTimerStates();
  publishClockDiagnosticStates();
}

// ============================================================================
// POLLING
// ============================================================================

bool readCoreRegister(
  uint16_t address,
  RegisterValue &dest,
  const String &label
) {
  uint16_t raw = 0;
  bool ok = readHoldingRegister(address, raw);

  dest.lastReadOk = ok;

  if (ok) {
    dest.raw = raw;
    dest.valid = true;
    addLog("OK [" + String(address) + "] " + label + " raw=" + String(raw));
  }

  delay(12);
  return ok;
}

bool pollLegacySensor(LegacyRegisterSensor &s) {
  uint16_t raw = 0;
  bool ok = readHoldingRegister(s.address, raw);

  s.lastReadOk = ok;

  if (ok) {
    s.raw = raw;
    s.valid = true;

    float decoded = decodeValue(s.raw, s.decode);
    addLog(
      "OK [" + String(s.address) + "] " +
      String(s.protocolCode) + " " +
      String(decoded, static_cast<unsigned int>(1))
    );

    if (mqtt.connected()) publishLegacyNumericSensor(s);
  }

  delay(12);
  return ok;
}


bool pollDiagnosticRegister(DiagnosticRegister &d) {
  uint16_t raw = 0;
  bool ok = readHoldingRegister(d.address, raw);
  d.lastReadOk = ok;
  if (ok) {
    d.raw = raw;
    d.valid = true;
    addLog("DIAG OK [" + String(d.address) + "] " + String(d.code) +
           " " + String(d.name) + " raw=" + String(raw));
  } else {
    addLog("DIAG FAIL [" + String(d.address) + "] " + String(d.code) +
           " " + String(d.name));
  }
  delay(12);
  return ok;
}

bool pollCentralClockDiagnostic(DiagnosticRegister &d) {
  uint16_t raw = 0;
  bool ok = readHoldingRegisterFromSlave(CENTRAL_MODBUS_SLAVE, d.address, raw);
  d.lastReadOk = ok;
  if (ok) {
    d.raw = raw;
    d.valid = true;
    addLog("SLAVE1 DIAG OK [" + String(d.address) + "] " + String(d.code) +
           " " + String(d.name) + " raw=" + String(raw));
  } else {
    addLog("SLAVE1 DIAG FAIL [" + String(d.address) + "] " + String(d.code) +
           " " + String(d.name));
  }
  delay(12);
  return ok;
}

DiagnosticRegister *findCentralClockDiag(uint16_t address) {
  for (size_t i = 0; i < CENTRAL_CLOCK_DIAG_COUNT; i++) {
    if (centralClockDiagnostics[i].address == address) return &centralClockDiagnostics[i];
  }
  return nullptr;
}

String formatCentralControllerClock() {
  // HW211-map candidate on slave H30=1:
  // 1151 hour, 1152 minute, 1153 month, 1154 day, 1155 weekday, 1156 year.
  DiagnosticRegister *hour    = findCentralClockDiag(1151);
  DiagnosticRegister *minute  = findCentralClockDiag(1152);
  DiagnosticRegister *month   = findCentralClockDiag(1153);
  DiagnosticRegister *day     = findCentralClockDiag(1154);
  DiagnosticRegister *year    = findCentralClockDiag(1156);

  if (!hour || !minute || !month || !day || !year) return "No data";
  if (!hour->valid || !minute->valid || !month->valid || !day->valid || !year->valid) {
    return "No response / incomplete";
  }

  if (hour->raw > 23 || minute->raw > 59 ||
      month->raw < 1 || month->raw > 12 ||
      day->raw < 1 || day->raw > 31 ||
      year->raw > 99) {
    return "Response received but values not plausible as clock";
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u",
           static_cast<unsigned int>(2000 + year->raw),
           static_cast<unsigned int>(month->raw),
           static_cast<unsigned int>(day->raw),
           static_cast<unsigned int>(hour->raw),
           static_cast<unsigned int>(minute->raw));
  return String(buf);
}

void runCentralClockBootDiagnostic() {
  addLog("");
  addLog("========== CENTRAL-CONTROLLER CLOCK PROBE ==========");
  addLog("H30 is 1, so probing slave 1 registers 1150-1158 READ ONLY.");
  addLog("No function-06 writes are sent to slave 1.");

  for (size_t i = 0; i < CENTRAL_CLOCK_DIAG_COUNT; i++) {
    pollCentralClockDiagnostic(centralClockDiagnostics[i]);
  }

  addLog("Slave-1 decoded clock candidate: " + formatCentralControllerClock());
  addLog("========== END CENTRAL-CONTROLLER CLOCK PROBE ==========");
}

DiagnosticRegister *findClockDiag(uint16_t address) {
  for (size_t i = 0; i < CLOCK_DIAG_COUNT; i++) {
    if (clockDiagnostics[i].address == address) return &clockDiagnostics[i];
  }
  return nullptr;
}

DiagnosticRegister *findVacationTimerDiag(uint16_t address) {
  for (size_t i = 0; i < VACATION_TIMER_DIAG_COUNT; i++) {
    if (vacationTimerDiagnostics[i].address == address) return &vacationTimerDiagnostics[i];
  }
  return nullptr;
}

bool esp32LocalTime(struct tm &out) {
  time_t now = time(nullptr);
  // Jan 1 2024 UTC. Anything earlier means SNTP has not established time yet.
  if (now < 1704067200) return false;
  return localtime_r(&now, &out) != nullptr;
}

String formatEsp32LocalTime() {
  struct tm t;
  if (!esp32LocalTime(t)) return "NTP not synced";
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min, t.tm_sec);
  return String(buf);
}

String lastManualClockTest = "Not run";

bool readControllerClockForSync() {
  bool ok = true;
  for (size_t i = 0; i < CLOCK_DIAG_COUNT; i++) {
    uint16_t address = clockDiagnostics[i].address;
    if (address < 1151 || address > 1156) continue;
    if (!pollDiagnosticRegister(clockDiagnostics[i])) ok = false;
  }
  return ok;
}

bool controllerClockDriftSeconds(long &driftSeconds) {
  struct tm controller;
  struct tm local;
  if (!officialControllerClockTm(controller)) return false;
  if (!esp32LocalTime(local)) return false;

  time_t controllerEpoch = mktime(&controller);
  time_t localEpoch = mktime(&local);
  if (controllerEpoch == (time_t)-1 || localEpoch == (time_t)-1) return false;

  driftSeconds = static_cast<long>(difftime(controllerEpoch, localEpoch));
  return true;
}

bool verifyClockMailboxPayload(const uint16_t expected[5]) {
  bool allMatch = true;

  for (uint16_t i = 0; i < 5; i++) {
    uint16_t raw = 0;
    uint16_t address = 1152 + i;
    bool ok = readHoldingRegister(address, raw);

    DiagnosticRegister *d = findClockDiag(address);
    if (d) {
      d->lastReadOk = ok;
      if (ok) {
        d->raw = raw;
        d->valid = true;
      }
    }

    if (!ok) {
      addLog("CLOCK MAILBOX VERIFY FAIL [" + String(address) + "]: no response");
      allMatch = false;
    } else if (raw != expected[i]) {
      addLog(
        "CLOCK MAILBOX VERIFY FAIL [" + String(address) +
        "]: expected=" + String(expected[i]) +
        " actual=" + String(raw)
      );
      allMatch = false;
    } else {
      addLog(
        "CLOCK MAILBOX VERIFY OK [" + String(address) +
        "] = " + String(raw)
      );
    }

    delay(12);
  }

  return allMatch;
}

bool writeControllerClockFromNtp(const String &reason) {
  struct tm t;
  if (!esp32LocalTime(t)) {
    addLog("CLOCK WRITE REFUSED (" + reason + "): ESP32 NTP time is not valid");
    return false;
  }

  uint16_t fields[5] = {
    static_cast<uint16_t>(t.tm_min),
    static_cast<uint16_t>(t.tm_hour),
    static_cast<uint16_t>(t.tm_mday),
    static_cast<uint16_t>(t.tm_mon + 1),
    static_cast<uint16_t>((t.tm_year + 1900) % 100)
  };

  char target[32];
  snprintf(
    target, sizeof(target),
    "%04d-%02d-%02d %02d:%02d",
    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min
  );

  addLog("");
  addLog("========== CLOCK PUSH ==========");
  addLog("Reason: " + reason);
  addLog("NTP target: " + String(target));

  // Proven DTU/Wi-Fi clock command sequence on slave 99:
  //   1152 minute
  //   1153 hour
  //   1154 day
  //   1155 month
  //   1156 year
  // then M11/1151 = 1 to apply the clock command.
  if (!writeMultipleRegistersFrame(1152, fields, 5, "clock fields")) {
    addLog("CLOCK PUSH FAILED: FC16 fields 1152-1156 were not acknowledged");
    addLog("========== END CLOCK PUSH ==========");
    return false;
  }

  delay(150);

  // Confirm the command payload was stored exactly before applying it.
  if (!verifyClockMailboxPayload(fields)) {
    addLog("CLOCK PUSH FAILED: clock field read-back did not match NTP payload");
    addLog("========== END CLOCK PUSH ==========");
    return false;
  }

  uint16_t apply[1] = {1};
  if (!writeMultipleRegistersFrame(1151, apply, 1, "M11 clock apply flag")) {
    addLog("CLOCK PUSH FAILED: M11/1151 apply command was not acknowledged");
    addLog("========== END CLOCK PUSH ==========");
    return false;
  }

  // M11 may later clear when the controller consumes the command. That is normal
  // and is NOT used as a live-clock or drift indicator.
  lastClockCommandTarget = String(target);
  addLog("CLOCK PUSH VERIFIED: payload read-back matched and apply command was acknowledged");
  addLog("========== END CLOCK PUSH ==========");
  return true;
}

void maybeRunWeeklyClockSync() {
  if (!WEEKLY_CLOCK_SYNC_ENABLED) return;

  struct tm local;
  if (!esp32LocalTime(local)) return;

  if (local.tm_wday != WEEKLY_CLOCK_SYNC_WDAY ||
      local.tm_hour != WEEKLY_CLOCK_SYNC_HOUR) {
    return;
  }

  int year = local.tm_year + 1900;
  int yday = local.tm_yday;

  // One successful clock push completes this Monday.
  if (weeklyClockSuccessYear == year && weeklyClockSuccessYday == yday) {
    return;
  }

  // If a write/verification fails, retry at most once per 10-minute bucket
  // during 01:00-01:59.
  int bucket = local.tm_min / WEEKLY_CLOCK_RETRY_BUCKET_MINUTES;
  if (weeklyClockAttemptYear == year &&
      weeklyClockAttemptYday == yday &&
      weeklyClockAttemptBucket == bucket) {
    return;
  }

  weeklyClockAttemptYear = year;
  weeklyClockAttemptYday = yday;
  weeklyClockAttemptBucket = bucket;
  lastClockCheckLocal = formatEsp32LocalTime();

  addLog("");
  addLog("========== MONDAY 01:00 FORCED CLOCK SYNC ==========");
  addLog("Live controller time cannot be read from 1151-1156; pushing trusted NTP time.");

  if (writeControllerClockFromNtp("Monday 01:00 scheduled sync")) {
    clockSyncStatus =
      "Monday NTP clock push verified at " + formatEsp32LocalTime();
    lastClockCorrectionLocal = formatEsp32LocalTime();
    weeklyClockSuccessYear = year;
    weeklyClockSuccessYday = yday;
  } else {
    clockSyncStatus =
      "Monday NTP clock push FAILED at " + formatEsp32LocalTime() +
      "; retry scheduled within the 01:00 hour";
  }

  addLog(clockSyncStatus);
  if (mqtt.connected()) publishClockDiagnosticStates();
  addLog("========== END MONDAY FORCED CLOCK SYNC ==========");
}

bool manualClockSetFromNtp() {
  addLog("");
  addLog("========== MANUAL CLOCK WRITE TEST ==========");

  bool ok = writeControllerClockFromNtp("manual web test");
  if (ok) {
    lastManualClockTest =
      "VERIFIED - NTP payload read back correctly and apply command was acknowledged. Target " +
      lastClockCommandTarget;
    clockSyncStatus =
      "Manual NTP clock push verified at " + formatEsp32LocalTime();
    lastClockCorrectionLocal = formatEsp32LocalTime();
  } else {
    lastManualClockTest =
      "FAILED - see diagnostic log";
    clockSyncStatus =
      "Manual NTP clock push FAILED at " + formatEsp32LocalTime();
  }

  addLog("CLOCK TEST: " + lastManualClockTest);
  if (mqtt.connected()) publishClockDiagnosticStates();
  addLog("========== END MANUAL CLOCK WRITE TEST ==========");
  return ok;
}

String clockTestWebPage() {
  String out;
  out.reserve(2500);
  out += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  out += "<style>body{font-family:system-ui;max-width:780px;margin:30px auto;padding:0 18px}";
  out += ".box{padding:18px;border:1px solid #bbb;border-radius:14px;margin:16px 0}";
  out += ".go{display:inline-block;padding:14px 18px;border-radius:10px;background:#b71c1c;color:white;text-decoration:none;font-weight:700}";
  out += "code{background:#eee;padding:2px 5px;border-radius:5px}</style></head><body>";
  out += "<h1>EVO270 Manual Clock Test</h1>";
  out += "<div class='box'><b>ESP32 NTP time:</b> " + formatEsp32LocalTime() + "<br>";
  out += "<b>Last test:</b> " + lastManualClockTest + "</div>";
  out += "<p>Weekly Monday 01:00 forced NTP clock sync is ENABLED. This button remains as a manual one-shot override/test.</p>";
  out += "<p>The manual test writes only slave 99 DTU clock registers <code>1152-1156</code>, ";
  out += "then sets <code>1151 = 1</code> as the clock-apply flag. It does not touch ";
  out += "power, target temperature, operating mode, timers or vacation settings.</p>";
  out += "<p><b>Before clicking:</b> look at the physical EVO270 controller and note its displayed time.</p>";
  out += "<p><a class='go' href='/clock-set-now?confirm=YES'>SET EVO270 CLOCK TO NTP NOW</a></p>";
  out += "<p>After clicking, check the physical controller immediately. The Modbus clock block is a command mailbox, so /diag does not treat it as a live running clock.</p>";
  out += "</body></html>";
  return out;
}

bool officialControllerClockTm(struct tm &out) {
  DiagnosticRegister *minute = findClockDiag(1152);
  DiagnosticRegister *hour   = findClockDiag(1153);
  DiagnosticRegister *day    = findClockDiag(1154);
  DiagnosticRegister *month  = findClockDiag(1155);
  DiagnosticRegister *year   = findClockDiag(1156);

  if (!minute || !hour || !day || !month || !year) return false;
  if (!minute->valid || !hour->valid || !day->valid || !month->valid || !year->valid) return false;
  if (minute->raw > 59 || hour->raw > 23 || day->raw < 1 || day->raw > 31 ||
      month->raw < 1 || month->raw > 12 || year->raw > 99) return false;

  memset(&out, 0, sizeof(out));
  out.tm_year = (2000 + year->raw) - 1900;
  out.tm_mon = month->raw - 1;
  out.tm_mday = day->raw;
  out.tm_hour = hour->raw;
  out.tm_min = minute->raw;
  out.tm_sec = 0;
  out.tm_isdst = -1;
  return true;
}

String formatOfficialControllerClock() {
  struct tm t;
  if (!officialControllerClockTm(t)) return "Not plausible / not confirmed";
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min);
  return String(buf);
}

String controllerClockDriftText() {
  struct tm controller;
  struct tm local;
  if (!officialControllerClockTm(controller)) return "Unknown";
  if (!esp32LocalTime(local)) return "Waiting for NTP";

  time_t controllerEpoch = mktime(&controller);
  time_t localEpoch = mktime(&local);
  if (controllerEpoch == (time_t)-1 || localEpoch == (time_t)-1) return "Unknown";

  long driftSeconds = static_cast<long>(difftime(controllerEpoch, localEpoch));
  long driftMinutes = driftSeconds / 60;
  String sign = driftMinutes > 0 ? "+" : "";
  return sign + String(driftMinutes) + " min (controller - ESP32)";
}

String clockMapHint() {
  DiagnosticRegister *r1157 = findClockDiag(1157);
  DiagnosticRegister *r1158 = findClockDiag(1158);

  if (r1158 && r1158->valid && r1158->raw == 0x5AA5) {
    return "DTU/Wi-Fi map strongly indicated: register 1158 = 0x5AA5 heartbeat";
  }

  if (r1157 && r1157->valid && r1157->raw >= 1 && r1157->raw <= 247) {
    String hint = "HW211 map candidate: register 1157 looks like a Modbus unit address (" +
                  String(r1157->raw) + ")";
    if (r1158 && r1158->valid && r1158->raw <= 1) {
      hint += "; 1158 also looks like HW211 online-control mode (" + String(r1158->raw) + ")";
    }
    return hint;
  }

  return "Inconclusive - keep controller clock writes disabled";
}

String formatTimePair(uint16_t hourAddress, uint16_t minuteAddress) {
  DiagnosticRegister *h = findVacationTimerDiag(hourAddress);
  DiagnosticRegister *m = findVacationTimerDiag(minuteAddress);
  if (!h || !m || !h->valid || !m->valid) return "No data";
  if (h->raw > 23 || m->raw > 59) return "Invalid raw values";
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned int>(h->raw), static_cast<unsigned int>(m->raw));
  return String(buf);
}

void pollClockTimerVacationDiagnostics(bool includeOffsetCandidate) {
  addLog("");
  addLog("========== CLOCK / TIMER / VACATION DIAGNOSTICS ==========");

  for (size_t i = 0; i < VACATION_TIMER_DIAG_COUNT; i++) {
    pollDiagnosticRegister(vacationTimerDiagnostics[i]);
  }

  for (size_t i = 0; i < CLOCK_DIAG_COUNT; i++) {
    if (!includeOffsetCandidate && clockDiagnostics[i].address == 1150) continue;
    pollDiagnosticRegister(clockDiagnostics[i]);
  }

  addLog("ESP32 local time: " + formatEsp32LocalTime());
  addLog("DTU-map controller time: " + formatOfficialControllerClock());
  addLog("Clock drift: " + controllerClockDriftText());
  addLog("Clock map hint: " + clockMapHint());
  addLog("Clock diagnostic scan complete. Vacation/timer registers are controllable in V2.2.7; clock registers remain read-only.");
  if (mqtt.connected()) {
    publishVacationTimerStates();
    publishClockDiagnosticStates();
  }
}

void pollFast() {
  fastPollCount++;
  addLog("");
  addLog("========== FAST POLL #" + String(fastPollCount) + " ==========");

  // Always start with T01 because this is our directly proven test register.
  LegacyRegisterSensor *t01 = findSensorByCode("T01");
  bool ambientOk = false;

  if (t01 != nullptr) {
    ambientOk = pollLegacySensor(*t01);
  }

  readCoreRegister(REG_POWER, hws.power, "Power");
  readCoreRegister(REG_REQUESTED_MODE, hws.requestedMode, "Requested operation mode");
  readCoreRegister(REG_ACTUAL_MODE, hws.actualMode, "Actual operation mode");
  readCoreRegister(REG_STATUS0, hws.status0, "Status0");
  readCoreRegister(REG_STATUS1, hws.status1, "Status1");
  readCoreRegister(REG_FAULT0, hws.fault0, "Fault0");

  for (size_t i = 0; i < DIRECT_SENSOR_COUNT; i++) {
    LegacyRegisterSensor &s = directSensors[i];

    if (s.pollGroup != PollGroup::FAST) continue;
    if (strcmp(s.protocolCode, "T01") == 0) continue;

    pollLegacySensor(s);
  }

  if (mqtt.connected()) {
    publishSpecialStates();
  }

  lastFastPollMs = millis();

  addLog(
    "FAST complete. T01=" + String(ambientOk ? "OK" : "FAIL") +
    " Total OK=" + String(modbusSuccessCount) +
    " Fail=" + String(modbusFailureCount)
  );
}

void pollSlow() {
  slowPollCount++;
  addLog("");
  addLog("========== SLOW CONFIG POLL #" + String(slowPollCount) + " ==========");

  for (size_t i = 0; i < DIRECT_SENSOR_COUNT; i++) {
    LegacyRegisterSensor &s = directSensors[i];

    if (s.pollGroup != PollGroup::SLOW) continue;
    pollLegacySensor(s);
  }

  // Re-read the documented DTU/Wi-Fi vacation/timer/clock block. Register 1150
  // is an offset-candidate probe and is only read at boot to avoid recurring noise.
  pollClockTimerVacationDiagnostics(false);

  // H07 is now available for the Temperature Unit select.
  if (mqtt.connected()) {
    publishPlaceholderStates();
    publishSpecialStates();
  }

  lastSlowPollMs = millis();

  addLog(
    "SLOW complete. Total OK=" + String(modbusSuccessCount) +
    " Fail=" + String(modbusFailureCount)
  );
}

// ============================================================================
// MQTT COMMANDS - LIMITED, VERIFIED WRITES
// ============================================================================

void republishAfterCommand() {
  if (!mqtt.connected()) return;

  publishSpecialStates();

  LegacyRegisterSensor *r01 = findSensorByCode("R01");
  if (r01 != nullptr && r01->valid) {
    publishLegacyNumericSensor(*r01);
  }
  publishVacationTimerStates();
}


bool ensureVacationTimerDiag(uint16_t address) {
  DiagnosticRegister *d = findVacationTimerDiag(address);
  if (!d) return false;
  if (d->valid && d->lastReadOk) return true;
  return pollDiagnosticRegister(*d);
}

bool updateVacationTimerRegister(uint16_t address, uint16_t value, const String &label) {
  uint16_t verifiedRaw = 0;
  if (!writeRegisterVerified(address, value, label, verifiedRaw)) return false;
  DiagnosticRegister *d = findVacationTimerDiag(address);
  if (d) {
    d->raw = verifiedRaw;
    d->valid = true;
    d->lastReadOk = true;
  }
  return true;
}

bool parseOnOff(const String &messageRaw, bool &enabled) {
  String message = messageRaw;
  message.trim();
  if (message == "ON") { enabled = true; return true; }
  if (message == "OFF") { enabled = false; return true; }
  return false;
}

bool writeMaskBitVerified(uint16_t address, uint8_t bit, bool enabled, const String &label) {
  if (!ensureVacationTimerDiag(address)) {
    addLog("WRITE REJECT " + label + ": cannot read current mask");
    return false;
  }
  DiagnosticRegister *d = findVacationTimerDiag(address);
  uint16_t current = d->raw;
  uint16_t requested = enabled ? (current | (1U << bit)) : (current & ~(1U << bit));
  if (requested == current) {
    addLog("WRITE SKIP " + label + ": already " + String(enabled ? "ON" : "OFF"));
    return true;
  }
  return updateVacationTimerRegister(address, requested, label + (enabled ? " ON" : " OFF"));
}

bool leapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  static const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && leapYear(year)) return 29;
  return days[month];
}

bool parseVacationDate(const String &messageRaw, uint16_t &yy, uint16_t &month, uint16_t &day) {
  String message = messageRaw;
  message.trim();
  int y = 0, m = 0, d = 0;
  char extra = 0;
  int fields = sscanf(message.c_str(), "%d-%d-%d%c", &y, &m, &d, &extra);
  if (fields != 3) return false;
  if (y < 2000 || y > 2099 || m < 1 || m > 12) return false;
  if (d < 1 || d > daysInMonth(y, m)) return false;
  yy = static_cast<uint16_t>(y - 2000);
  month = static_cast<uint16_t>(m);
  day = static_cast<uint16_t>(d);
  return true;
}

bool parseTimerTime(const String &messageRaw, uint16_t &hour, uint16_t &minute) {
  String message = messageRaw;
  message.trim();
  int h = 0, m = 0, s = 0;
  char extra = 0;
  int fields = sscanf(message.c_str(), "%d:%d:%d%c", &h, &m, &s, &extra);
  if (fields == 2) s = 0;
  else if (fields != 3) return false;
  if (h < 0 || h > 23 || m < 0 || m > 59 || s != 0) return false;
  hour = static_cast<uint16_t>(h);
  minute = static_cast<uint16_t>(m);
  return true;
}

void bestEffortRestoreRegister(uint16_t address, uint16_t value, const String &label) {
  uint16_t ignored = 0;
  addLog("ROLLBACK " + label + " register=" + String(address) + " raw=" + String(value));
  if (writeRegisterVerified(address, value, "Rollback " + label, ignored)) {
    DiagnosticRegister *d = findVacationTimerDiag(address);
    if (d) { d->raw = ignored; d->valid = true; d->lastReadOk = true; }
  }
}

bool handleVacationDateEnabledCommand(const String &messageRaw) {
  bool enabled = false;
  if (!parseOnOff(messageRaw, enabled)) {
    addLog("WRITE REJECT vacation date enable: invalid payload [" + messageRaw + "]");
    return false;
  }
  return writeMaskBitVerified(1129, 0, enabled, "Vacation date schedule");
}

bool handleVacationDateCommand(const String &messageRaw) {
  uint16_t yy = 0, month = 0, day = 0;
  if (!parseVacationDate(messageRaw, yy, month, day)) {
    addLog("WRITE REJECT vacation date: expected YYYY-MM-DD in years 2000-2099");
    return false;
  }

  const uint16_t regs[] = {1129, 1130, 1131, 1132};
  for (uint16_t reg : regs) {
    if (!ensureVacationTimerDiag(reg)) {
      addLog("WRITE REJECT vacation date: cannot establish current register " + String(reg));
      return false;
    }
  }

  DiagnosticRegister *l01 = findVacationTimerDiag(1129);
  DiagnosticRegister *rYear = findVacationTimerDiag(1130);
  DiagnosticRegister *rMonth = findVacationTimerDiag(1131);
  DiagnosticRegister *rDay = findVacationTimerDiag(1132);
  uint16_t oldMask = l01->raw;
  uint16_t oldYear = rYear->raw;
  uint16_t oldMonth = rMonth->raw;
  uint16_t oldDay = rDay->raw;
  bool wasEnabled = bitIsSet(oldMask, 0);

  if (oldYear == yy && oldMonth == month && oldDay == day) {
    addLog("WRITE SKIP vacation date: already " + messageRaw);
    return true;
  }

  // Prevent the controller acting on a transient partially-written date.
  if (wasEnabled && !updateVacationTimerRegister(1129, oldMask & ~1U, "Temporarily disable vacation date schedule")) {
    return false;
  }

  bool ok = true;
  // Day=1 is a safe bridge between any two valid month/year combinations.
  if (rDay->raw != 1) ok = updateVacationTimerRegister(1132, 1, "Vacation date safe day") && ok;
  if (ok && rYear->raw != yy) ok = updateVacationTimerRegister(1130, yy, "Vacation year") && ok;
  if (ok && rMonth->raw != month) ok = updateVacationTimerRegister(1131, month, "Vacation month") && ok;
  if (ok && day != 1) ok = updateVacationTimerRegister(1132, day, "Vacation day") && ok;

  if (!ok) {
    addLog("Vacation date write failed; attempting rollback to previous date.");
    bestEffortRestoreRegister(1132, 1, "vacation safe day");
    bestEffortRestoreRegister(1130, oldYear, "vacation year");
    bestEffortRestoreRegister(1131, oldMonth, "vacation month");
    bestEffortRestoreRegister(1132, oldDay, "vacation day");
    if (wasEnabled) bestEffortRestoreRegister(1129, oldMask, "vacation enable mask");
    return false;
  }

  if (wasEnabled && !updateVacationTimerRegister(1129, oldMask, "Restore vacation date schedule")) {
    addLog("WARNING: date changed but vacation schedule enable could not be restored automatically.");
    return false;
  }

  return true;
}

bool handleTimerEnableCommand(const String &messageRaw, uint8_t bit, const String &label) {
  bool enabled = false;
  if (!parseOnOff(messageRaw, enabled)) {
    addLog("WRITE REJECT " + label + ": invalid payload [" + messageRaw + "]");
    return false;
  }
  return writeMaskBitVerified(1133, bit, enabled, label);
}

bool handleTimerTimeCommand(
  const String &messageRaw,
  uint16_t hourAddress,
  uint16_t minuteAddress,
  uint8_t enableBit,
  const String &label
) {
  uint16_t hour = 0, minute = 0;
  if (!parseTimerTime(messageRaw, hour, minute)) {
    addLog("WRITE REJECT " + label + ": expected HH:MM:00 at minute resolution");
    return false;
  }

  if (!ensureVacationTimerDiag(1133) ||
      !ensureVacationTimerDiag(hourAddress) ||
      !ensureVacationTimerDiag(minuteAddress)) {
    addLog("WRITE REJECT " + label + ": cannot establish current timer state");
    return false;
  }

  DiagnosticRegister *mask = findVacationTimerDiag(1133);
  DiagnosticRegister *rHour = findVacationTimerDiag(hourAddress);
  DiagnosticRegister *rMinute = findVacationTimerDiag(minuteAddress);
  uint16_t oldMask = mask->raw;
  uint16_t oldHour = rHour->raw;
  uint16_t oldMinute = rMinute->raw;
  bool wasEnabled = bitIsSet(oldMask, enableBit);

  if (oldHour == hour && oldMinute == minute) {
    addLog("WRITE SKIP " + label + ": already " + messageRaw);
    return true;
  }

  if (wasEnabled && !updateVacationTimerRegister(1133, oldMask & ~(1U << enableBit), "Temporarily disable " + label)) {
    return false;
  }

  bool ok = true;
  if (oldHour != hour) ok = updateVacationTimerRegister(hourAddress, hour, label + " hour") && ok;
  if (ok && oldMinute != minute) ok = updateVacationTimerRegister(minuteAddress, minute, label + " minute") && ok;

  if (!ok) {
    addLog(label + " write failed; attempting rollback.");
    bestEffortRestoreRegister(hourAddress, oldHour, label + " hour");
    bestEffortRestoreRegister(minuteAddress, oldMinute, label + " minute");
    if (wasEnabled) bestEffortRestoreRegister(1133, oldMask, label + " enable mask");
    return false;
  }

  if (wasEnabled && !updateVacationTimerRegister(1133, oldMask, "Restore " + label)) {
    addLog("WARNING: timer time changed but enable bit could not be restored automatically.");
    return false;
  }

  return true;
}


bool handlePowerCommand(const String &messageRaw) {
  String message = messageRaw;
  message.trim();

  uint16_t requestedRaw = 0;

  if (message == "heat") {
    requestedRaw = 1;
  } else if (message == "off") {
    requestedRaw = 0;
  } else {
    addLog("WRITE REJECT power: invalid climate mode payload [" + message + "]");
    return false;
  }

  uint16_t verifiedRaw = 0;

  if (!writeRegisterVerified(
        REG_POWER,
        requestedRaw,
        String("Power ") + (requestedRaw ? "ON" : "OFF"),
        verifiedRaw
      )) {
    return false;
  }

  hws.power.raw = verifiedRaw;
  hws.power.valid = true;
  hws.power.lastReadOk = true;

  // Let controller status registers settle, then refresh the actual output state.
  delay(300);
  readCoreRegister(REG_ACTUAL_MODE, hws.actualMode, "Actual operation mode after power write");
  readCoreRegister(REG_STATUS0, hws.status0, "Status0 after power write");
  readCoreRegister(REG_STATUS1, hws.status1, "Status1 after power write");

  return true;
}

bool handleOperatingModeCommand(const String &messageRaw) {
  String message = messageRaw;
  message.trim();

  uint16_t requestedRaw = 0;
  if (!modeRawFromName(message, requestedRaw)) {
    addLog("WRITE REJECT operating mode: invalid payload [" + message + "]");
    return false;
  }

  uint16_t verifiedRaw = 0;

  if (!writeRegisterVerified(
        REG_REQUESTED_MODE,
        requestedRaw,
        "Operating mode " + message,
        verifiedRaw
      )) {
    return false;
  }

  hws.requestedMode.raw = verifiedRaw;
  hws.requestedMode.valid = true;
  hws.requestedMode.lastReadOk = true;

  // 1012 is the requested mode; 1013 is the controller's actual mode.
  delay(300);
  readCoreRegister(REG_ACTUAL_MODE, hws.actualMode, "Actual operation mode after mode write");

  return true;
}

bool handleTargetTemperatureCommand(const String &messageRaw) {
  float requestedC = 0.0f;
  uint16_t requestedRaw = 0;

  if (!parseTargetTemperature(messageRaw, requestedC, requestedRaw)) {
    addLog(
      "WRITE REJECT target: payload [" + messageRaw +
      "] must be 10.0-60.0 C in 0.5 C steps"
    );
    return false;
  }

  uint16_t verifiedRaw = 0;

  if (!writeRegisterVerified(
        REG_TARGET_TEMP,
        requestedRaw,
        "Target " + String(requestedC, static_cast<unsigned int>(1)) + " C",
        verifiedRaw
      )) {
    return false;
  }

  LegacyRegisterSensor *r01 = findSensorByCode("R01");
  if (r01 != nullptr) {
    r01->raw = verifiedRaw;
    r01->valid = true;
    r01->lastReadOk = true;

    if (mqtt.connected()) {
      publishLegacyNumericSensor(*r01);
    }
  }

  return true;
}

void mqttCallback(char *topicRaw, byte *payload, unsigned int length) {
  String topic(topicRaw);
  String message;
  message.reserve(length);

  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }

  if (topic == "homeassistant/status" && message == "online") {
    addLog("Home Assistant birth message received. Republishing discovery/state.");
    publishAllDiscovery();
    publishAllKnownStates();
    return;
  }

  bool handled = false;
  bool ok = false;

  if (topic == climateCommandTopic("mode")) {
    handled = true;
    ok = handlePowerCommand(message);
  } else if (topic == climateCommandTopic("target_temperature")) {
    handled = true;
    ok = handleTargetTemperatureCommand(message);
  } else if (topic == selectCommandTopic("operating_mode")) {
    handled = true;
    ok = handleOperatingModeCommand(message);
  } else if (topic == controlCommandTopic("vacation_date_enabled")) {
    handled = true;
    ok = handleVacationDateEnabledCommand(message);
  } else if (topic == controlCommandTopic("vacation_date")) {
    handled = true;
    ok = handleVacationDateCommand(message);
  } else if (topic == controlCommandTopic("timer_1_on_enabled")) {
    handled = true;
    ok = handleTimerEnableCommand(message, 0, "Timer 1 ON");
  } else if (topic == controlCommandTopic("timer_1_on")) {
    handled = true;
    ok = handleTimerTimeCommand(message, 1134, 1135, 0, "Timer 1 ON");
  } else if (topic == controlCommandTopic("timer_1_off_enabled")) {
    handled = true;
    ok = handleTimerEnableCommand(message, 1, "Timer 1 OFF");
  } else if (topic == controlCommandTopic("timer_1_off")) {
    handled = true;
    ok = handleTimerTimeCommand(message, 1136, 1137, 1, "Timer 1 OFF");
  } else if (topic == controlCommandTopic("timer_2_on_enabled")) {
    handled = true;
    ok = handleTimerEnableCommand(message, 2, "Timer 2 ON");
  } else if (topic == controlCommandTopic("timer_2_on")) {
    handled = true;
    ok = handleTimerTimeCommand(message, 1138, 1139, 2, "Timer 2 ON");
  } else if (topic == controlCommandTopic("timer_2_off_enabled")) {
    handled = true;
    ok = handleTimerEnableCommand(message, 3, "Timer 2 OFF");
  } else if (topic == controlCommandTopic("timer_2_off")) {
    handled = true;
    ok = handleTimerTimeCommand(message, 1140, 1141, 3, "Timer 2 OFF");
  } else if (topic == selectCommandTopic("temperature_unit")) {
    handled = true;
    addLog(
      "READ-ONLY compatibility select: ignored Temperature Unit command payload=[" +
      message + "]"
    );
    ok = false;
  }

  if (handled) {
    if (!ok) {
      addLog("Command not applied. Republishing actual controller state.");
    }
    republishAfterCommand();
  }
}

// ============================================================================
// MQTT CONNECTION
// ============================================================================

bool placeholderCredential(const char *s) {
  if (s == nullptr) return true;
  String v(s);
  return v.length() == 0 || v.startsWith("PUT_");
}

void subscribeCommandTopics() {
  mqtt.subscribe("homeassistant/status");

  mqtt.subscribe(selectCommandTopic("temperature_unit").c_str());
  mqtt.subscribe(selectCommandTopic("operating_mode").c_str());

  mqtt.subscribe(climateCommandTopic("mode").c_str());
  mqtt.subscribe(climateCommandTopic("target_temperature").c_str());

  mqtt.subscribe(controlCommandTopic("vacation_date_enabled").c_str());
  mqtt.subscribe(controlCommandTopic("vacation_date").c_str());
  mqtt.subscribe(controlCommandTopic("timer_1_on_enabled").c_str());
  mqtt.subscribe(controlCommandTopic("timer_1_on").c_str());
  mqtt.subscribe(controlCommandTopic("timer_1_off_enabled").c_str());
  mqtt.subscribe(controlCommandTopic("timer_1_off").c_str());
  mqtt.subscribe(controlCommandTopic("timer_2_on_enabled").c_str());
  mqtt.subscribe(controlCommandTopic("timer_2_on").c_str());
  mqtt.subscribe(controlCommandTopic("timer_2_off_enabled").c_str());
  mqtt.subscribe(controlCommandTopic("timer_2_off").c_str());
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttAttemptMs < 5000) return;
  lastMqttAttemptMs = now;

  addLog("Connecting MQTT " + String(MQTT_HOST) + ":" + String(MQTT_PORT));

  bool connected = false;

  if (!placeholderCredential(MQTT_USERNAME)) {
    connected = mqtt.connect(
      MQTT_CLIENT_ID,
      MQTT_USERNAME,
      MQTT_PASSWORD,
      MQTT_AVAIL_TOPIC,
      0,
      true,
      "offline"
    );
  } else {
    connected = mqtt.connect(
      MQTT_CLIENT_ID,
      MQTT_AVAIL_TOPIC,
      0,
      true,
      "offline"
    );
  }

  if (!connected) {
    addLog("MQTT connection failed. State=" + String(mqtt.state()));
    return;
  }

  addLog("MQTT connected.");
  mqtt.publish(MQTT_AVAIL_TOPIC, "online", true);

  subscribeCommandTopics();
  cleanupV20Discovery();
  publishAllDiscovery();
  publishAllKnownStates();
}

// ============================================================================
// WEB HELPERS
// ============================================================================

String sensorDisplayValue(const char *code) {
  LegacyRegisterSensor *s = findSensorByCode(code);

  if (s == nullptr || !s->valid) return "No data";

  float value = decodeValue(s->raw, s->decode);
  String out = String(value, static_cast<unsigned int>(1));

  if (strlen(s->unit) > 0) {
    out += " ";
    out += s->unit;
  }

  return out;
}

String coreRaw(const RegisterValue &r) {
  return r.valid ? String(r.raw) : "No data";
}

String coreBitState(const RegisterValue &r, uint8_t bit) {
  if (!r.valid) return "No data";
  return bitIsSet(r.raw, bit) ? "ON" : "OFF";
}

String webHeader(const String &title) {
  String page;

  page += R"HTML(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>)HTML";

  page += title;

  page += R"HTML(</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;margin:0;background:#111827;color:#e5e7eb}
main{max-width:1180px;margin:auto;padding:18px}
h1{font-size:1.5rem;margin:0 0 4px}
h2{font-size:1.05rem;margin-top:24px}
small,.muted{color:#9ca3af}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}
.card{background:#1f2937;border:1px solid #374151;border-radius:10px;padding:12px}
.value{font-size:1.35rem;font-weight:700;margin-top:4px}
table{width:100%;border-collapse:collapse;background:#1f2937;border-radius:10px;overflow:hidden}
th,td{text-align:left;padding:8px;border-bottom:1px solid #374151;font-size:.86rem;vertical-align:top}
th{color:#9ca3af;position:sticky;top:0;background:#1f2937}
pre{white-space:pre-wrap;word-break:break-word;background:#030712;padding:12px;border-radius:10px;max-height:560px;overflow:auto;font-size:.76rem}
a.button{display:inline-block;background:#374151;color:#fff;text-decoration:none;padding:8px 12px;border-radius:7px;margin-right:6px}
.ok{color:#86efac}.bad{color:#fca5a5}.warn{color:#fde68a}
code{color:#c4b5fd}
</style>
</head>
<body>
<main>
)HTML";

  return page;
}

String mainWebPage() {
  String page = webHeader("EVO270 Ensuite & Kitchen");
  page.reserve(36000);

  page += "<h1>EVOHeat Ensuite &amp; Kitchen</h1>";
  page += "<div class='muted'>Arduino + MQTT • " + String(FW_VERSION) +
          " • LEGACY MAP • VERIFIED WRITES</div>";

  page += "<p><a class='button' href='/map'>Full legacy entity map</a>";
  page += "<a class='button' href='/diag'>Clock / timer diagnostics</a>";
  page += "<a class='button' href='/clock-test'>Manual clock test</a>";
  page += "<a class='button' href='/clear'>Clear browser log</a></p>";

  page += "<h2>System</h2><div class='grid'>";

  page += "<div class='card'><div class='muted'>Wi-Fi</div><div class='value'>";
  if (WiFi.status() == WL_CONNECTED) {
    page += "<span class='ok'>Connected</span></div><div>" +
            WiFi.localIP().toString() + " • " + String(WiFi.RSSI()) + " dBm</div>";
  } else {
    page += "<span class='bad'>Disconnected</span></div>";
  }
  page += "</div>";

  page += "<div class='card'><div class='muted'>MQTT</div><div class='value'>";
  page += mqtt.connected() ? "<span class='ok'>Connected</span>" : "<span class='bad'>Disconnected</span>";
  page += "</div><div>" + String(MQTT_HOST) + ":" + String(MQTT_PORT) + "</div></div>";

  page += "<div class='card'><div class='muted'>Modbus</div><div class='value'>Slave 99</div>";
  page += "<div>9600 8N1 • TX17 RX18 EN21</div></div>";

  page += "<div class='card'><div class='muted'>Modbus Statistics</div><div class='value'>";
  page += "R " + String(modbusSuccessCount) + "/" + String(modbusFailureCount);
  page += " • W " + String(modbusWriteSuccessCount) + "/" + String(modbusWriteFailureCount) + "</div>";
  page += "<div>Fast " + String(fastPollCount) + " • Slow " + String(slowPollCount) + "</div></div>";

  page += "<div class='card'><div class='muted'>Legacy Entity Map</div><div class='value'>";
  page += String(DIRECT_SENSOR_COUNT + BIT_SENSOR_COUNT + PLACEHOLDER_SENSOR_COUNT + 16);
  page += " entities</div><div>";
  page += String(DIRECT_SENSOR_COUNT) + " registers • " +
          String(BIT_SENSOR_COUNT) + " status bits • " +
          String(PLACEHOLDER_SENSOR_COUNT) + " placeholders</div></div>";

  page += "</div>";

  page += "<h2>Writable Controls</h2><div class='grid'>";
  page += "<div class='card'><div class='muted'>Whitelisted FC06 writes</div>";
  page += "<div><b>Power</b> 1011 • <b>Mode</b> 1012 • <b>Target</b> 1104</div>";
  page += "<div class='muted'>Every write is FC06-echo checked then FC03 read-back verified.</div></div>";

  page += "<div class='card'><div class='muted'>Last verified write</div><div class='value'>";
  page += htmlEscape(lastWriteSummary);
  page += "</div><div class='muted'>Home Assistant power is a controller command, not electrical isolation.</div></div>";
  page += "</div>";

  page += "<h2>Core HWS Values</h2>";
  page += "<table><tr><th>Source</th><th>Legacy entity/value</th><th>Current</th></tr>";
  page += "<tr><td>T01 / 2019</td><td>Ambient temperature</td><td>" + sensorDisplayValue("T01") + "</td></tr>";
  page += "<tr><td>T02 / 2020</td><td>Bottom temperature</td><td>" + sensorDisplayValue("T02") + "</td></tr>";
  page += "<tr><td>T03 / 2021</td><td>Top temperature</td><td>" + sensorDisplayValue("T03") + "</td></tr>";
  page += "<tr><td>T04 / 2022</td><td>Coil temperature</td><td>" + sensorDisplayValue("T04") + "</td></tr>";
  page += "<tr><td>T05 / 2023</td><td>Suction temperature</td><td>" + sensorDisplayValue("T05") + "</td></tr>";
  page += "<tr><td>T06 / 2024</td><td>Solar temperature</td><td>" + sensorDisplayValue("T06") + "</td></tr>";
  page += "<tr><td>T10 / 2025</td><td>APP/display temperature</td><td>" + sensorDisplayValue("T10") + "</td></tr>";
  page += "<tr><td>R01 / 1104</td><td>Target temperature</td><td>" + sensorDisplayValue("R01") + "</td></tr>";

  page += "<tr><td>1011</td><td>Power</td><td>";
  if (hws.power.valid) page += hws.power.raw == 1 ? "ON" : "OFF";
  else page += "No data";
  page += "</td></tr>";

  page += "<tr><td>1012</td><td>Requested operation mode</td><td>";
  if (hws.requestedMode.valid) page += modeName(hws.requestedMode.raw);
  else page += "No data";
  page += "</td></tr>";

  page += "<tr><td>1013</td><td>Actual operation mode</td><td>";
  if (hws.actualMode.valid) page += modeName(hws.actualMode.raw);
  else page += "No data";
  page += "</td></tr></table>";

  page += "<h2>Operating Status / Legacy Bits</h2>";
  page += "<table><tr><th>Source</th><th>Function</th><th>State</th></tr>";
  page += "<tr><td>2050 bit 8</td><td>Compressor</td><td>" + coreBitState(hws.status0, 8) + "</td></tr>";
  page += "<tr><td>2050 bit 9</td><td>Electrical heater</td><td>" + coreBitState(hws.status0, 9) + "</td></tr>";
  page += "<tr><td>2050 bit 10</td><td>4-way valve</td><td>" + coreBitState(hws.status0, 10) + "</td></tr>";
  page += "<tr><td>2050 bit 11</td><td>Fan high</td><td>" + coreBitState(hws.status0, 11) + "</td></tr>";
  page += "<tr><td>2050 bit 12</td><td>Fan low</td><td>" + coreBitState(hws.status0, 12) + "</td></tr>";
  page += "<tr><td>2051 bit 0</td><td>Legacy O12 / Status1 bit0 (semantic uncertain)</td><td>" + coreBitState(hws.status1, 0) + "</td></tr>";
  page += "<tr><td>2051 bit 1</td><td>DTU/Wi-Fi online</td><td>" + coreBitState(hws.status1, 1) + "</td></tr>";
  page += "<tr><td>2051 bit 2</td><td>Defrost</td><td>" + coreBitState(hws.status1, 2) + "</td></tr>";
  page += "<tr><td>2051 bit 3</td><td>Legacy O15 / current hot-water-function bit (not a disinfection flag)</td><td>" + coreBitState(hws.status1, 3) + "</td></tr>";
  page += "</table>";


page += "<h2>Clock / Timers / Vacation — Verification Only</h2>";
page += "<div class='card'><b>READ ONLY in this firmware.</b> ";
page += "These registers are being observed before clock/timer/vacation writes are enabled. ";
page += "DTU/Wi-Fi slave-99 map expects M11-M16 at 1151-1156; register 1150 is also probed once at boot as an offset candidate.</div>";

page += "<div class='grid'>";
page += "<div class='card'><div class='muted'>ESP32 / NTP local time</div><div class='value'>" +
        htmlEscape(formatEsp32LocalTime()) + "</div><div>Australia/Melbourne</div></div>";
page += "<div class='card'><div class='muted'>Controller time — DTU map</div><div class='value'>" +
        htmlEscape(formatOfficialControllerClock()) + "</div><div>1152 minute • 1153 hour • 1154 day • 1155 month • 1156 year</div></div>";
page += "<div class='card'><div class='muted'>Clock drift</div><div class='value'>" +
        htmlEscape(controllerClockDriftText()) + "</div><div>Controller minus ESP32</div></div>";
page += "</div>";

DiagnosticRegister *l01 = findVacationTimerDiag(1129);
DiagnosticRegister *l02 = findVacationTimerDiag(1130);
DiagnosticRegister *l03 = findVacationTimerDiag(1131);
DiagnosticRegister *l04 = findVacationTimerDiag(1132);
DiagnosticRegister *l05 = findVacationTimerDiag(1133);

page += "<h3>Vacation</h3><table><tr><th>Source</th><th>Meaning</th><th>Current</th></tr>";
page += "<tr><td>L01 / 1129</td><td>Vacation-date enable bit 0</td><td>";
if (l01 && l01->valid) page += bitIsSet(l01->raw, 0) ? "ENABLED" : "disabled"; else page += "No data";
page += "</td></tr>";
page += "<tr><td>L02-L04 / 1130-1132</td><td>Vacation date YY-MM-DD</td><td>";
if (l02 && l03 && l04 && l02->valid && l03->valid && l04->valid) {
  char vbuf[20];
  snprintf(vbuf, sizeof(vbuf), "20%02u-%02u-%02u", static_cast<unsigned int>(l02->raw), static_cast<unsigned int>(l03->raw), static_cast<unsigned int>(l04->raw));
  page += String(vbuf);
} else page += "No data";
page += "</td></tr></table>";

page += "<h3>Built-in timers</h3><table><tr><th>Timer</th><th>Enable bit</th><th>Time</th></tr>";
page += "<tr><td>Timer 1 ON</td><td>";
if (l05 && l05->valid) page += bitIsSet(l05->raw, 0) ? "ENABLED" : "disabled"; else page += "No data";
page += "</td><td>" + formatTimePair(1134, 1135) + "</td></tr>";
page += "<tr><td>Timer 1 OFF</td><td>";
if (l05 && l05->valid) page += bitIsSet(l05->raw, 1) ? "ENABLED" : "disabled"; else page += "No data";
page += "</td><td>" + formatTimePair(1136, 1137) + "</td></tr>";
page += "<tr><td>Timer 2 ON</td><td>";
if (l05 && l05->valid) page += bitIsSet(l05->raw, 2) ? "ENABLED" : "disabled"; else page += "No data";
page += "</td><td>" + formatTimePair(1138, 1139) + "</td></tr>";
page += "<tr><td>Timer 2 OFF</td><td>";
if (l05 && l05->valid) page += bitIsSet(l05->raw, 3) ? "ENABLED" : "disabled"; else page += "No data";
page += "</td><td>" + formatTimePair(1140, 1141) + "</td></tr></table>";

  page += "<h2>Fault0</h2><div class='card'>";
  if (hws.fault0.valid) {
    if (hws.fault0.raw == 0) {
      page += "<div class='value ok'>No Fault0 bits active</div>";
    } else {
      page += "<div class='value bad'>FAULT ACTIVE</div>";
    }

    page += "<div>Raw decimal " + String(hws.fault0.raw) +
            " • raw hex 0x" + String(static_cast<unsigned int>(hws.fault0.raw), static_cast<unsigned char>(HEX)) + "</div>";
  } else {
    page += "<div class='value'>No data</div>";
  }
  page += "</div>";

  page += "<h2>Browser Log</h2>";
  page += "<pre>" + renderLog() + "</pre>";

  page += "</main></body></html>";
  return page;
}

String mapWebPage() {
  String page = webHeader("EVO270 Legacy Entity Map");
  page.reserve(56000);

  page += "<h1>Legacy Home Assistant Entity Map</h1>";
  page += "<div class='muted'>" + String(DEVICE_NAME) + " • " + String(FW_VERSION) + "</div>";
  page += "<p><a class='button' href='/'>Back to monitor</a></p>";

  page += "<div class='card'><b>LIMITED VERIFIED WRITES ENABLED.</b> ";
  page += "Function 06 is whitelisted only for 1011 Power, 1012 requested mode and 1104/R01 target temperature. ";
  page += "All writes are followed by function 03 read-back verification. All other mapped registers remain read-only. ";
  page += "L01-L13 (1129-1141) are verified writable vacation/timer controls. Clock candidates 1150-1156 remain read-only because both physical units returned zeroes. ";
  page += "Old O/S sensor entity IDs are preserved even where current HW211 numbering differs. ";
  page += "T11/T12 remain unknown because no reliable local mapping exists.</div>";

  page += "<h2>Direct register sensors</h2>";
  page += "<table><tr><th>Legacy entity</th><th>Code</th><th>Register</th><th>Current</th><th>Read</th></tr>";

  for (size_t i = 0; i < DIRECT_SENSOR_COUNT; i++) {
    LegacyRegisterSensor &s = directSensors[i];

    page += "<tr><td><code>sensor." + String(DEVICE_CODE) + "_" + String(s.slug) + "</code></td>";
    page += "<td>" + String(s.protocolCode) + "</td>";
    page += "<td>" + String(s.address) + "</td><td>";

    if (s.valid) {
      page += String(decodeValue(s.raw, s.decode), static_cast<unsigned int>(1));
      if (strlen(s.unit) > 0) page += " " + String(s.unit);
    } else {
      page += "No data";
    }

    page += "</td><td>";
    page += s.lastReadOk ? "<span class='ok'>OK</span>" : "<span class='warn'>not confirmed</span>";
    page += "</td></tr>";
  }

  page += "</table>";

  page += "<h2>Status-bit legacy sensors</h2>";
  page += "<table><tr><th>Legacy entity</th><th>Code</th><th>Source</th><th>Current</th></tr>";

  for (size_t i = 0; i < BIT_SENSOR_COUNT; i++) {
    LegacyBitSensor &s = bitSensors[i];

    RegisterValue *source = nullptr;
    if (s.sourceRegister == REG_STATUS0) source = &hws.status0;
    if (s.sourceRegister == REG_STATUS1) source = &hws.status1;

    page += "<tr><td><code>sensor." + String(DEVICE_CODE) + "_" + String(s.slug) + "</code></td>";
    page += "<td>" + String(s.legacyCode) + "</td>";
    page += "<td>" + String(s.sourceRegister) + " bit " + String(s.bit) + "</td><td>";

    if (source != nullptr && source->valid) {
      page += bitIsSet(source->raw, s.bit) ? "1.0 / ON" : "0.0 / OFF";
    } else {
      page += "No data";
    }

    page += "</td></tr>";
  }

  page += "</table>";

  page += "<h2>Legacy placeholders</h2>";
  page += "<table><tr><th>Legacy entity</th><th>Code</th><th>State</th></tr>";

  for (size_t i = 0; i < PLACEHOLDER_SENSOR_COUNT; i++) {
    LegacyPlaceholderSensor &s = placeholderSensors[i];

    page += "<tr><td><code>sensor." + String(DEVICE_CODE) + "_" + String(s.slug) + "</code></td>";
    page += "<td>" + String(s.legacyCode) + "</td><td>unknown (intentional)</td></tr>";
  }

  page += "</table>";
  page += "</main></body></html>";
  return page;
}


String diagnosticsText() {
  String out;
  out.reserve(7000);
  out += "EVO270 CLOCK / TIMER / VACATION DIAGNOSTICS\n";
  out += "Device: " + String(DEVICE_NAME) + "\n";
  out += "Firmware: " + String(FW_VERSION) + "\n";
  out += "MODE: VERIFIED VACATION/TIMER WRITES; MONDAY 01:00 FORCED NTP CLOCK SYNC ENABLED\n\n";

  out += "ESP32 local time: " + formatEsp32LocalTime() + "\n";
  out += "Controller clock readback: NOT AVAILABLE - registers 1151-1156 are a clock command/apply mailbox\n";
  out += "Weekly clock schedule: Monday 01:00 local (AEST/AEDT), forced NTP push\n";
  out += "Clock sync status: " + clockSyncStatus + "\n";
  out += "Last scheduled/manual attempt: " + lastClockCheckLocal + "\n";
  out += "Last successful clock push: " + lastClockCorrectionLocal + "\n";
  out += "Last clock command target: " + lastClockCommandTarget + "\n\n";

  out += "VACATION / TIMERS (DTU Wi-Fi map)\n";
  for (size_t i = 0; i < VACATION_TIMER_DIAG_COUNT; i++) {
    DiagnosticRegister &d = vacationTimerDiagnostics[i];
    out += String(d.code) + " reg=" + String(d.address) + " " + String(d.name) + " = ";
    out += d.valid ? String(d.raw) : String("NO DATA");
    out += d.lastReadOk ? " [OK]\n" : " [not confirmed]\n";
  }

  out += "\nCLOCK COMMAND MAILBOX (NOT A LIVE CLOCK)\n";
  for (size_t i = 0; i < CLOCK_DIAG_COUNT; i++) {
    DiagnosticRegister &d = clockDiagnostics[i];
    out += String(d.code) + " reg=" + String(d.address) + " " + String(d.name) + " = ";
    out += d.valid ? String(d.raw) : String("NO DATA");
    out += d.lastReadOk ? " [OK]\n" : " [not confirmed]\n";
  }

  out += "\nLEGACY SLAVE-1 CLOCK PROBE (previously tested; no response)\n";
  out += "Decoded HW211-map time: " + formatCentralControllerClock() + "\n";
  for (size_t i = 0; i < CENTRAL_CLOCK_DIAG_COUNT; i++) {
    DiagnosticRegister &d = centralClockDiagnostics[i];
    out += String(d.code) + " slave=1 reg=" + String(d.address) + " " + String(d.name) + " = ";
    out += d.valid ? String(d.raw) : String("NO DATA");
    out += d.lastReadOk ? " [OK]\n" : " [not confirmed]\n";
  }

  out += "\nTimer enable-mask interpretation (L05/1133):\n";
  DiagnosticRegister *l05 = findVacationTimerDiag(1133);
  if (l05 && l05->valid) {
    out += "  bit0 Timer1 ON : " + String(bitIsSet(l05->raw, 0) ? "enabled" : "disabled") + "\n";
    out += "  bit1 Timer1 OFF: " + String(bitIsSet(l05->raw, 1) ? "enabled" : "disabled") + "\n";
    out += "  bit2 Timer2 ON : " + String(bitIsSet(l05->raw, 2) ? "enabled" : "disabled") + "\n";
    out += "  bit3 Timer2 OFF: " + String(bitIsSet(l05->raw, 3) ? "enabled" : "disabled") + "\n";
  } else {
    out += "  No data\n";
  }

  out += "\nDecoded timer times:\n";
  out += "  Timer1 ON : " + formatTimePair(1134, 1135) + "\n";
  out += "  Timer1 OFF: " + formatTimePair(1136, 1137) + "\n";
  out += "  Timer2 ON : " + formatTimePair(1138, 1139) + "\n";
  out += "  Timer2 OFF: " + formatTimePair(1140, 1141) + "\n";

  out += "\nVACATION/TIMER WRITES ARE ENABLED WITH READ-BACK VERIFICATION. MONDAY 01:00 FORCED NTP CLOCK PUSH IS ENABLED. CLOCK MAILBOX VALUES MUST NOT BE INTERPRETED AS LIVE TIME. MANUAL OVERRIDE: /clock-test\n";
  return out;
}

// ============================================================================
// WEB SERVER
// ============================================================================

void setupWeb() {
  web.on("/", HTTP_GET, []() {
    web.send(200, "text/html; charset=utf-8", mainWebPage());
  });

  web.on("/map", HTTP_GET, []() {
    web.send(200, "text/html; charset=utf-8", mapWebPage());
  });

  web.on("/diag", HTTP_GET, []() {
    web.send(200, "text/plain; charset=utf-8", diagnosticsText());
  });

  web.on("/clock-test", HTTP_GET, []() {
    web.send(200, "text/html; charset=utf-8", clockTestWebPage());
  });

  web.on("/clock-set-now", HTTP_GET, []() {
    if (!web.hasArg("confirm") || web.arg("confirm") != "YES") {
      web.send(400, "text/plain; charset=utf-8",
               "Clock write refused. Open /clock-test and use the confirmation button.");
      return;
    }

    bool ok = manualClockSetFromNtp();
    String body = ok
      ? "Clock test command was acknowledged. Check the physical EVO270 clock now.\\n\\n" + lastManualClockTest
      : "Clock test FAILED or was refused.\\n\\n" + lastManualClockTest;

    web.send(ok ? 200 : 500, "text/plain; charset=utf-8", body);
  });

  web.on("/clear", HTTP_GET, []() {
    clearLog();
    web.sendHeader("Location", "/", true);
    web.send(302, "text/plain", "");
  });

  web.on("/health", HTTP_GET, []() {
    String status = "{";
    status += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    status += "\"mqtt\":" + String(mqtt.connected() ? "true" : "false") + ",";
    status += "\"modbus_successes\":" + String(modbusSuccessCount) + ",";
    status += "\"modbus_failures\":" + String(modbusFailureCount) + ",";
    status += "\"modbus_write_successes\":" + String(modbusWriteSuccessCount) + ",";
    status += "\"modbus_write_failures\":" + String(modbusWriteFailureCount) + ",";
    status += "\"fast_polls\":" + String(fastPollCount) + ",";
    status += "\"slow_polls\":" + String(slowPollCount) + ",";
    status += "\"last_clock_command\":\"" + jsonEscape(lastClockCommandTarget) + "\",";
    status += "\"clock_sync_status\":\"" + jsonEscape(clockSyncStatus) + "\"";
    status += "}";
    web.send(200, "application/json", status);
  });

  web.begin();
  addLog("Web monitor started.");
}

// ============================================================================
// WI-FI / OTA
// ============================================================================

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  addLog("Connecting Wi-Fi: " + String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    addLog("Wi-Fi connected: " + WiFi.localIP().toString());
    addLog("RSSI: " + String(WiFi.RSSI()) + " dBm");
  } else {
    addLog("Wi-Fi not connected yet. Auto-reconnect remains enabled.");
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);

  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    addLog("Arduino OTA starting...");
  });

  ArduinoOTA.onEnd([]() {
    addLog("Arduino OTA complete.");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    addLog("Arduino OTA error: " + String(static_cast<int>(error)));
  });

  ArduinoOTA.begin();
  addLog("Arduino OTA ready: " + String(HOSTNAME));
}

// ============================================================================
// SETUP / LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" EVO270 ENSUITE / KITCHEN - V2.2.7 LEGACY MAP - VERIFIED WRITES + CLOCK DIAGNOSTICS");
  Serial.println("============================================================");

  pinMode(RS485_EN_PIN, OUTPUT);
  rs485ReceiveMode();

  RS485.begin(
    MODBUS_BAUD,
    SERIAL_8N1,
    RS485_RX_PIN,
    RS485_TX_PIN
  );

  addLog("RS485 started: TX17 RX18 EN21 / 9600 8N1 / slave 99");
  addLog("Verified writes enabled for Power 1011, Mode 1012, Target 1104 and vacation/timer L01-L13 1129-1141.");
  addLog("Vacation/timer L01-L13 1129-1141 are writable. Proven controller clock block 1151-1156 is now enabled for controlled NTP correction.");
  addLog("V2.2.6 treats 1151-1156 as a clock command mailbox and pushes trusted NTP time once each Monday during the 01:00 local hour.");

  connectWiFi();
  configTzTime(LOCAL_TZ, NTP_SERVER_1, NTP_SERVER_2);
  addLog("SNTP configured for Australia/Melbourne; Monday 01:00 forced clock push enabled.");
  setupOTA();
  setupWeb();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(8192);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);

  // Prove the dynamic/local registers before doing the larger config scan.
  pollFast();

  connectMqtt();

  // Build the full legacy register picture immediately at boot.
  pollSlow();

  // One boot-only probe reads register 1150, the one-register-earlier clock
  // candidate seen in another workbook sheet. No writes are performed.
  addLog("Boot-only clock offset candidate probe (1150) - READ ONLY");
  pollDiagnosticRegister(clockDiagnostics[0]);

  // Slave-1/H30 probing was completed during V2.2.3 and returned no response.
  // Final operation remains on the proven DTU/Wi-Fi slave 99.

  if (mqtt.connected()) {
    publishAllKnownStates();
  }

  addLog("Monitor: http://" + WiFi.localIP().toString() + "/");
  addLog("Map:     http://" + WiFi.localIP().toString() + "/map");
  addLog("Diag:    http://" + WiFi.localIP().toString() + "/diag");
  addLog("Clock test: http://" + WiFi.localIP().toString() + "/clock-test");
  addLog("mDNS:    http://" + String(HOSTNAME) + ".local/");
}

void loop() {
  ArduinoOTA.handle();
  web.handleClient();

  connectMqtt();

  if (mqtt.connected()) {
    mqtt.loop();
  }

  unsigned long now = millis();

  if (now - lastFastPollMs >= FAST_POLL_INTERVAL_MS) {
    pollFast();
  }

  if (now - lastSlowPollMs >= SLOW_POLL_INTERVAL_MS) {
    pollSlow();
  }

  maybeRunWeeklyClockSync();

  delay(2);
}
