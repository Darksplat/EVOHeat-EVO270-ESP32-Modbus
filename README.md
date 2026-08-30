# EVOHeat EVO270-1 local ESP32 / Modbus monitor

Local, cloud-independent monitoring of an **EVOHeat EVO270-1** heat-pump hot-water system using the **HW211-family controller** and a **Waveshare ESP32-S3-RS485-CAN**.

This repository records the field work that took the installation from protocol research and ESPHome commissioning attempts to a reliable, **read-only Arduino + MQTT + Home Assistant** implementation.

> **Status:** field-tested read-only monitoring. No Modbus write function is included in the current public firmware.

## Hardware used / where to get it

The build was developed around the following hardware:

| Part | Source | Notes |
|---|---|---|
| Waveshare ESP32-S3-RS485-CAN | [Waveshare product page](https://www.waveshare.com/esp32-s3-rs485-can.htm) | Exact board used for the working installation. It accepts 7–36 V DC at the screw-terminal input and has isolated RS485 onboard. |
| EVO270 replacement connector / pigtail | [Tempero Systems – 5-pin male/female JST-SM locking pigtail](https://temperosystems.com.au/products/5pin-male-female-jst-sm-locking-pigtail/) | This is the exact connector/pigtail used for the replacement EVO270 harness. The pins/wires must be rearranged to the correct EVO270 mapping before use. |
| EVO270-1 | [EvoHeat EVO270-1 product page](https://evoheat.com.au/hot-water-heat-pump/evo-270/) | Manufacturer reference/manual source. |

More detail is in [`docs/KNOWN_WORKING_HARDWARE.md`](docs/KNOWN_WORKING_HARDWARE.md) and [`docs/HARDWARE_AND_WIRING.md`](docs/HARDWARE_AND_WIRING.md).

### Important connector warning

Buying the correct connector **does not mean the pins will already be in the correct electrical order**.

The Tempero 5-pin JST-SM locking pigtail physically gives you the connector you need, but the supplied wire/pin order should **not** be assumed to match the EVO270.

If you want a neat plug-in replacement harness, check every cavity against the original EVO270 Wi-Fi/RS485 lead and **extract the crimp terminals from the new housing and re-insert them in the correct positions before plugging it into the heat pump**. A small pick or fine terminal-release tool can lift the plastic retaining tang so the crimp terminal slides back out of the housing.

For this installation the functions are:

| EVO270 harness wire | Function | Waveshare connection |
|---|---|---|
| Red | +12 V DC | DC+ |
| Black | Ground | DC- |
| White | RS485 A | A+ |
| Yellow | RS485 B | B- |
| Orange | Shield / earth | Not connected to the Waveshare in this installation |

**Do not trust the colour order or pin order of the Tempero pigtail as supplied. Verify function, cavity position and continuity before applying power.**

If you do not want to de-pin and rearrange the JST-SM pigtail, the other practical method is to **snip the lead and solder the required wires to the EVO270 harness**, matching the functions above. Insulate each soldered joint individually with heat-shrink and then protect the completed harness mechanically.

The Waveshare board can be powered from the EVO270's 12 V supply through its DC screw terminals; do **not** put 12 V directly onto a bare ESP32 5 V/3.3 V pin.

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

### Dashboard

The actual Home Assistant dashboard used during development is included at:

[`home-assistant/dashboard/evo270-hot-water-dashboard.yaml`](home-assistant/dashboard/evo270-hot-water-dashboard.yaml)

It uses **Mushroom Cards** and **card-mod** for the frontend presentation. See [`home-assistant/README.md`](home-assistant/README.md) for installation notes, entity-ID guidance and the legacy Controller Clocks caveat.

The bottom **Controller Clocks** section of that dashboard still uses the earlier Aqua Temp `aqua_temp.sync_clock` service and legacy unit-time sensors. Those clock cards are not a dependency of the Arduino + MQTT monitoring firmware and should be removed on a clean MQTT-only installation.

## For first-time builders

The code is only half of this project. A beginner should be able to identify the correct plug and wire the board before ever opening the Arduino IDE.

The photo walkthrough will therefore show, in order:

1. the original EVO270 Wi-Fi/RS485 connector;
2. the Tempero 5-pin JST-SM locking pigtail;
3. how a terminal is released from the JST-SM housing;
4. the corrected connector pin/wire arrangement;
5. the Waveshare DC+/DC-/A+/B- terminals;
6. the completed harness connected to the Waveshare;
7. the Waveshare mounted inside the EVO270 enclosure;
8. the Arduino browser monitor showing a valid Modbus response; and
9. the final Home Assistant device/entities.

See [`docs/KNOWN_WORKING_HARDWARE.md`](docs/KNOWN_WORKING_HARDWARE.md) for the hardware/photo checklist.

## Repository layout

- `firmware/current/` – canonical V2.1.2-style public-safe reference implementation
- `firmware/commissioning/` – known-good single-register OTA and browser-monitor sketches
- `firmware/testbench/` – RS485 master/slave bench sketches
- `home-assistant/` – real dashboard YAML plus frontend/dependency and migration notes
- `data/` – register, status-bit, mode and legacy-entity mapping tables
- `docs/` – wiring, protocol, MQTT, commissioning, ESPHome failure analysis and project history
- `diagnostics/` – sanitized failure/commissioning excerpts
- `references/` – upstream projects and licensing notes
- `firmware/current/EVO270_ReadOnly_Reference/secrets.example.h` – example credentials file for the reference firmware; copy it **in that same sketch folder** to `secrets.h`

## Quick start

1. Obtain the Waveshare board and the Tempero **5-pin male/female JST-SM locking pigtail**, or prepare to splice the original lead.
2. If using the JST-SM pigtail, **de-pin/re-pin it to match the EVO270 wiring before connecting it**.
3. Verify Red = +12 V, Black = GND, White = RS485 A and Yellow = RS485 B by connector position/function. Do not rely solely on colours from the replacement pigtail.
4. Wire the ESP32 to the HW211 RS485/DTU interface. Treat **terminal function** as authoritative.
5. Start with `firmware/commissioning/EVO270_Laundry_Bathroom_Arduino_OTA.ino`.
6. Confirm register 2019 returns a sensible ambient temperature.
7. In `firmware/current/EVO270_ReadOnly_Reference/`, copy `secrets.example.h` to `secrets.h` and fill in Wi-Fi/MQTT credentials. The `secrets.h` file must stay in the **same Arduino sketch folder** as `EVO270_ReadOnly_Reference.ino`.
8. Move to the canonical read-only firmware.
9. Confirm Home Assistant MQTT Discovery entities are created.
10. Install Mushroom Cards and card-mod if using the supplied dashboard, then import the dashboard YAML and adjust entity IDs for your installation if required.
11. Leave writes disabled until every target register, range and side effect is independently verified.

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

Disconnect/isolate mains power before opening the EVO270 enclosure. The low-voltage Wi-Fi/RS485 harness discussed here is inside equipment that also contains mains-voltage wiring.

Heat-pump hot-water controllers can have compressor protection, disinfection, anti-freeze and safety-related parameters. This repository intentionally defaults to **read-only** operation. If write support is added later it should be narrowly allow-listed and range-checked.
