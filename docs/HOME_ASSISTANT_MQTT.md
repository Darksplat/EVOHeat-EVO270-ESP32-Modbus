# Home Assistant / MQTT design

## Goals

- local operation with no cloud dependency for monitoring
- identify each local controller from the replacement Waveshare ESP32 hardware
- expose climate/select state without allowing unsafe writes during validation
- keep raw Modbus troubleshooting available via serial/browser logs

## Device identity

The original Aqua Temp wireless module's MAC-derived device code is **not** used by the public firmware.

Instead, the firmware reads the **Waveshare ESP32 Wi-Fi MAC address automatically**, removes the colons and lower-cases it. That 12-hex-character value becomes the local device identifier.

Example only:

```text
Waveshare Wi-Fi MAC: AA:BB:CC:DD:EE:FF
Device ID:           aabbccddeeff
MQTT client ID:      evo270-aabbccddeeff
MQTT root:           evo270/aabbccddeeff
```

Every Waveshare therefore gets its own MQTT client ID, availability topic and Home Assistant unique-ID namespace without the user having to type a hardware ID into the sketch.

## Discovery

The project design publishes Home Assistant MQTT Discovery for the mapped EVO270/HW211 state exposed by the firmware, using the Waveshare-derived device ID as the namespace.

The register/entity **suffixes** retain the useful Aqua Temp/HW211 naming where it helps map old entities back to local registers, but a previous Aqua Temp wireless-module MAC is not required.

## Read-only command handling

The public implementation must not contain Function 06 or Function 16 write code.

If Home Assistant sends a command to a read-only climate/select command topic, the firmware should:

1. log that the command was ignored;
2. not transmit any Modbus write;
3. republish the real state.

## MQTT polling tiers

- dynamic state: every 20 seconds
- slow configuration values: every 5 minutes

This avoids continuously hammering the controller with a full map read.

## Fresh Home Assistant installations

A fresh installation is preferred. Flash the local firmware, allow MQTT Discovery to create the new Waveshare-based EVO270 device, then build/import the dashboard using the entity IDs Home Assistant creates for that device.
