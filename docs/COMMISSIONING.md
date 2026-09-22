# Commissioning procedure

## 1. Verify hardware

Confirm the Waveshare ESP32-S3-RS485-CAN wiring and the known-good bus parameters:

- GPIO17 TX
- GPIO18 RX
- GPIO21 direction/EN
- 9600 8N1
- slave 99

Historical single-register commissioning sketches remain under `firmware/commissioning/`.

## 2. Prepare V2.2.7

Choose the matching folder under `firmware/current/`.

Copy:

`secrets.example.h` → `secrets.h`

Enter Wi-Fi, MQTT and optional OTA credentials locally. Never commit `secrets.h`.

In Arduino IDE use **ESP32S3 Dev Module**. The field-tested installation used Espressif's ESP32 Arduino core 3.3.11.

## 3. Validate monitoring

After flashing, confirm:

- the embedded web page loads;
- `/diag` reports V2.2.7;
- Modbus responses are succeeding;
- T01/T02/T03/T10 are plausible;
- power, mode and status bits match the physical unit;
- MQTT Discovery entities are online in Home Assistant.

## 4. Validate local controls

Confirm target temperature and operating mode only within the ranges already enforced by firmware. Confirm Timer/Vacation values through `/diag` and the Home Assistant entities.

## 5. Validate clock

Open `/clock-test` and perform one manual NTP clock push. Confirm the physical controller clock changes to the expected local time and `/diag` reports a verified push.

Normal operation then uses the forced Monday 01:00 local NTP push.

## OTA note

Arduino IDE network discovery can be unreliable even when the device's mDNS service is healthy. If required, export the compiled application `.ino.bin` and use Espressif's `espota.py` directly against the device IP on port 3232. Use your OTA password locally; never paste or commit it.
