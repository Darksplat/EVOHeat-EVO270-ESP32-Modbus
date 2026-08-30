#pragma once

#define EVO270_PROFILE_LAUNDRY_BATHROOM 1
#define EVO270_PROFILE_ENSUITE_KITCHEN  2

#ifndef EVO270_PROFILE
#define EVO270_PROFILE EVO270_PROFILE_LAUNDRY_BATHROOM
#endif

#if EVO270_PROFILE == EVO270_PROFILE_LAUNDRY_BATHROOM
static const char *HOSTNAME       = "evo270-laundry-bathroom";
static const char *DEVICE_CODE    = "34eae7b41fea";
static const char *DEVICE_NAME    = "EVO270 - Bathroom & Laundry 34EAE7B41FEA";
static const char *MQTT_CLIENT_ID = "evo270-34eae7b41fea";
static const char *MQTT_BASE_TOPIC = "evo270/34eae7b41fea";
#elif EVO270_PROFILE == EVO270_PROFILE_ENSUITE_KITCHEN
static const char *HOSTNAME       = "evo270-ensuite-kitchen";
static const char *DEVICE_CODE    = "34eae79f4bce";
static const char *DEVICE_NAME    = "EVO270 - Ensuite & Kitchen 34EAE79F4BCE";
static const char *MQTT_CLIENT_ID = "evo270-34eae79f4bce";
static const char *MQTT_BASE_TOPIC = "evo270/34eae79f4bce";
#else
#error "Unknown EVO270_PROFILE"
#endif
