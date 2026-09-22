# Project history

## Phase 1 — protocol research

Community work from `sjtrny/esphome-hw211` and `echopin664/EVO270-1-HWS` established the HW211/EVO270 local RS485 path.

## Phase 2 — ESPHome attempt

ESPHome 2026.8.1 on the Waveshare ESP32-S3-RS485-CAN repeatedly produced partial `FE` responses and Modbus timeouts on this installation.

## Phase 3 — raw Arduino commissioning

Explicit GPIO21 direction control produced reliable Function 03 communication with the EVO270 on slave 99.

## Phase 4 — OTA, browser diagnostics and MQTT

OTA, a browser monitor, MQTT and Home Assistant Discovery were added while retaining the proven RS485 transaction sequence.

## Phase 5 — legacy entity/register mapping

Aqua Temp/HW211 entities were mapped to local registers/status bits. Fast and slow poll groups were introduced.

## Phase 6 — V2.1.2 read-only deployment

Both units reached a stable read-only baseline with detailed telemetry and local Home Assistant entities.

## Phase 7 — V2.2.1 verified writes

Narrowly scoped Function 06 write support was validated for power, requested operating mode and target temperature, with exact Function 03 readback. Vacation and Timer 1/2 registers were then verified with protected schedule-mask handling and rollback behavior.

## Phase 8 — clock protocol investigation

Several diagnostic builds tested conflicting published HW211/DTU clock maps. Slave 1 did not respond to the candidate clock block. On slave 99, the sequence using 1152–1156 plus M11 at 1151 successfully changed the physical controller clock.

A later diagnostic proved those registers are a **command/apply mailbox**, not a continuously advancing clock, so drift cannot be calculated from them.

## Phase 9 — V2.2.6 weekly forced NTP sync

The false live-clock/drift model was removed. Both ESP32s now push NTP-derived Melbourne time once each Monday during the 01:00 local hour, with bounded retry behavior after a failed attempt.

## Phase 10 — V2.2.7 clock-test link and dashboard

The embedded landing page gained a direct **Manual clock test** button before **Clear browser log**.

The Home Assistant dashboard was consolidated around the validated local controls while retaining detailed heat-pump telemetry: timers, Vacation return date, temperatures, compressor/booster/defrost, fan/valve/pump state, system health, disinfection data and local clock-sync status.

## Current release

`V2.2.7-clock-test-link` is the field-tested baseline for both installed EVO270-1 units.
