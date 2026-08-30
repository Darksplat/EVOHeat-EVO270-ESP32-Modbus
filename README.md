# EVOHeat EVO270-1 local ESP32 / Modbus monitor

Local, cloud-independent monitoring of an **EVOHeat EVO270-1** heat-pump hot-water system using the **HW211-family controller** and a **Waveshare ESP32-S3-RS485-CAN**.

This repository records the field work that took the installation from protocol research and ESPHome commissioning attempts to a reliable, **read-only Arduino + MQTT + Home Assistant** implementation.

> **Status:** field-tested read-only monitoring. No Modbus write function is included in the current public firmware.

## What is proven on this installation

| Item | Value |
|---|---|
| Modbus | RTU |
| Baud | 9600 |
| Format | 8N1 |
| DTU/Wi-Fi slave | 99 (`0x63`) |
| ESP32 UART TX | GPIO17 |
| ESP32 UART RX | GPIO18 |
| RS485 direction / EN | GPIO21 |
| EN HIGH | transmit |
| EN LOW | receive |
| Known-good test register | 2019 / T01 ambient temperature |
| Temperature decode | `(raw - 60) * 0.5` °C |
| Fast poll | 20 s |
| Slow/config poll | 5 min |

A known-good Function 03 request/response captured during commissioning:

```text
TX: 63 03 07 E3 00 01 7C CA
RX: 63 03 02 00 61 80 64
raw 97 -> 18.5 °C
```

## Why Arduino rather than ESPHome here?

ESPHome was the first approach. On this exact Waveshare ESP32-S3-RS485-CAN + EVO270/HW211 installation, ESPHome 2026.8.1 repeatedly timed out after partial one-byte `FE` responses even with the correct 9600 8N1/slave settings.

A direct Arduino test immediately became reliable when the transceiver direction was driven explicitly:

1. GPIO21 HIGH
2. write the Modbus RTU frame
3. `RS485.flush()`
4. short guard delay
5. GPIO21 LOW
6. receive and validate the response

See [`docs/ESPHOME_FAILURE.md`](docs/ESPHOME_FAILURE.md). This is documented as an **installation-specific interoperability finding**, not a claim that ESPHome Modbus is universally broken.

## Home Assistant

The current design publishes MQTT Discovery and deliberately preserves the old Aqua Temp-style entity naming so existing dashboards can be migrated with less churn.

Two installations used during development:

| Location | Legacy device code | MQTT base |
|---|---|---|
| Bathroom & Laundry | `34eae7b41fea` | `evo270/34eae7b41fea` |
| Ensuite & Kitchen | `34eae79f4bce` | `evo270/34eae79f4bce` |

Climate/select command topics are intentionally read-only in the public implementation: commands are rejected/logged and actual state is republished.

## Repository layout

- `firmware/current/` – canonical V2.1.2-style public-safe reference implementation
- `firmware/commissioning/` – known-good single-register OTA and browser-monitor sketches
- `firmware/testbench/` – RS485 master/slave bench sketches
- `data/` – register, status-bit, mode and legacy-entity mapping tables
- `docs/` – wiring, protocol, MQTT, commissioning, ESPHome failure analysis and project history
- `diagnostics/` – sanitized failure/commissioning excerpts
- `references/` – upstream projects and licensing notes
- `secrets.example.h` – example credentials file; copy locally to `secrets.h`

## Quick start

1. Wire the ESP32 to the HW211 RS485/DTU interface. Treat **terminal function** as authoritative; wire colours below are only what was observed on one installation.
2. Start with `firmware/commissioning/EVO270_Laundry_Bathroom_Arduino_OTA.ino`.
3. Confirm register 2019 returns a sensible ambient temperature.
4. Copy `secrets.example.h` to `secrets.h` and fill in Wi-Fi/MQTT credentials.
5. Move to the canonical read-only firmware.
6. Confirm Home Assistant MQTT Discovery entities are created.
7. Leave writes disabled until every target register, range and side effect is independently verified.

## Observed harness colours

| Observed colour | Function |
|---|---|
| Red | DC+ |
| Black | DC- |
| White | RS485 A+ |
| Yellow | RS485 B- |
| Orange | shield/earth; not terminated at the Waveshare in this installation |

**Do not assume another EVO270 harness uses the same colours. Verify the connector/terminal function before energising anything.**

## Credits

This work would have taken much longer without:

- **sjtrny/esphome-hw211** – especially the machine-readable HW211 Modbus protocol work and ESPHome component. That repository is MIT licensed.
- **echopin664/EVO270-1-HWS** – valuable prior work demonstrating local EVO270-1/HW211 RS485 control with ESP32 + Home Assistant and providing an EVO270 Modbus map.

See [`ACKNOWLEDGEMENTS.md`](ACKNOWLEDGEMENTS.md) and [`NOTICE.md`](NOTICE.md).

## Safety / scope

This is an independent community project and is not affiliated with or endorsed by EvoHeat, Aqua Temp, Waveshare or Home Assistant.

Heat-pump hot-water controllers can have compressor protection, disinfection, anti-freeze and safety-related parameters. This repository intentionally defaults to **read-only** operation. If write support is added later it should be narrowly allow-listed and range-checked.
