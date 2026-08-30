# Project history

## Phase 1 — protocol research

Two community projects established that the EVO270-1 HW211 controller could be accessed locally over RS485:

- sjtrny/esphome-hw211
- echopin664/EVO270-1-HWS

The sjtrny protocol JSON became the main structured register reference.

## Phase 2 — ESPHome attempt

ESPHome 2026.8.1 was configured on the Waveshare ESP32-S3-RS485-CAN.

It consistently produced one-byte `FE` partial responses and Modbus timeouts.

## Phase 3 — raw Arduino commissioning

A minimal Function 03 sketch was written to remove abstraction layers. Explicit GPIO21 direction control produced the first reliable response from register 2019.

## Phase 4 — OTA and browser diagnostics

Wi-Fi, Arduino OTA and a browser log were added without changing the proven Modbus transaction sequence.

## Phase 5 — MQTT / Home Assistant

The working Modbus reader was expanded into a read-only MQTT bridge with Home Assistant Discovery.

## Phase 6 — Aqua Temp entity/register mapping

The old cloud integration's entity model was mapped back onto local HW211 registers/status bits. Fast and slow polling groups were introduced, and T11/T12 were intentionally left unknown because no reliable local mapping was found.

The useful entity/register suffix mapping is retained as documentation, but the public firmware no longer depends on the removed Aqua Temp Wi-Fi module's MAC/device code.

## Phase 7 — two-unit deployment

The same architecture was applied to two EVO270-1 units. Each replacement Waveshare controller now derives its own identity automatically from its ESP32 Wi-Fi MAC, producing a unique MQTT namespace and Home Assistant device identity.

## Firmware naming during development

Historical names retained for traceability:

- `EVO270_Laundry_Bathroom_Arduino_OTA.ino`
- `EVO270_Laundry_Bathroom_Arduino_OTA_WebMonitor.ino`
- `EVO270_Laundry_Bathroom_V2_ReadOnly.ino`
- `EVO270_Laundry_Bathroom_V2_ReadOnly_v2.0.1.ino`
- `EVO270_Laundry_Bathroom_V2_1_LegacyMap_ReadOnly.ino`
- `EVO270_Laundry_Bathroom_V2_1_1.ino`
- `EVO270_Laundry_Bathroom_V2_1_2.ino`
- `EVO270_Ensuite_Kitchen_V2_1_2.ino`

The public `firmware/current` directory contains a consolidated, public-safe V2.1.2-style reference implementation rather than claiming byte-for-byte identity with every intermediate local sketch.
