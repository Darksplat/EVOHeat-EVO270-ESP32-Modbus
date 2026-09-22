#pragma once
#include <Arduino.h>

enum class DecodeType : uint8_t {
  RAW,
  TEMP1,
  TEMP,
  DIGI2,
  DIGI4,
  DIGI7
};

enum class PollGroup : uint8_t {
  FAST,
  SLOW
};

struct LegacyRegisterSensor {
  const char *slug;
  const char *displayName;
  const char *protocolCode;
  uint16_t address;
  DecodeType decode;
  const char *unit;
  const char *deviceClass;
  PollGroup pollGroup;
  bool diagnostic;
  bool valid;
  bool lastReadOk;
  uint16_t raw;
};

struct LegacyBitSensor {
  const char *slug;
  const char *displayName;
  const char *legacyCode;
  uint16_t sourceRegister;
  uint8_t bit;
};

struct LegacyPlaceholderSensor {
  const char *slug;
  const char *displayName;
  const char *legacyCode;
};

struct RegisterValue {
  bool valid = false;
  bool lastReadOk = false;
  uint16_t raw = 0;
};

struct HwsState {
  RegisterValue power;
  RegisterValue actualMode;
  RegisterValue status0;
  RegisterValue status1;
  RegisterValue fault0;
};
