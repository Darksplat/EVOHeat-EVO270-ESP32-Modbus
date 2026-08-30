# EVOHeat EVO270-1 local ESP32 / Modbus monitor

Local, cloud-independent monitoring of an **EVOHeat EVO270-1** heat-pump hot-water system using the **HW211-family controller** and a **Waveshare ESP32-S3-RS485-CAN**.

This repository records the field work that took the installation from protocol research and ESPHome commissioning attempts to a reliable, **read-only Arduino + MQTT + Home Assistant** implementation.

> **Status:** field-tested read-only monitoring. No Modbus write function is included in the current public firmware.

## Hardware used / where to get it

| Part | Source | Notes |
|---|---|---|
| Waveshare ESP32-S3-RS485-CAN | [Waveshare product page](https://www.waveshare.com/esp32-s3-rs485-can.htm) | Exact controller board used. It accepts 7–36 V DC at the screw-terminal input and has isolated RS485 onboard. |
| EVO270 replacement connector / pigtail | [Tempero Systems – 5-pin male/female JST-SM locking pigtail](https://temperosystems.com.au/products/5pin-male-female-jst-sm-locking-pigtail/) | Exact replacement connector/pigtail used. **The supplied pins/wires must be checked and rearranged to the EVO270 mapping before use.** |
| EVO270-1 | [EvoHeat EVO270-1 product page](https://evoheat.com.au/hot-water-heat-pump/evo-270/) | Manufacturer reference/manual source. |

See [`docs/KNOWN_WORKING_HARDWARE.md`](docs/KNOWN_WORKING_HARDWARE.md) and [`docs/HARDWARE_AND_WIRING.md`](docs/HARDWARE_AND_WIRING.md).

## Important connector warning

The Tempero 5-pin JST-SM locking pigtail physically fits the job, but **do not assume its supplied pin/wire order matches the EVO270**.

For a neat plug-in harness, compare it with the original EVO270 Wi-Fi/RS485 connector, release the crimp terminals with a small pick/terminal tool and **re-insert them into the correct connector cavities**.

If you do not want to de-pin/re-pin the pigtail, the alternative is to **snip and solder the lead function-for-function**, heat-shrink every conductor individually and protect the finished splice mechanically.

Observed on the tested installation:

| EVO270 harness wire | Function | Waveshare connection |
|---|---|---|
| Red | +12 V DC | DC+ |
| Black | Ground | DC- |
| White | RS485 A | A+ |
| Yellow | RS485 B | B- |
| Orange | Shield / earth | Not connected to the Waveshare in this installation |

**Verify connector position, function, polarity and continuity before applying power. Do not trust an aftermarket pigtail's colour order.**

The Waveshare board can be powered from the EVO270's 12 V accessory supply through its DC screw terminals. Do **not** apply 12 V to a bare ESP32 5 V or 3.3 V pin.

## Device identity: use the Waveshare, not the old Wi-Fi module

The original Aqua Temp wireless module used a MAC-derived device code. That removed wireless module is **not** the identity of the local controller in this project.

The public firmware now automatically reads the replacement **Waveshare ESP32 Wi-Fi MAC address**, removes the colons and uses the resulting 12 hexadecimal digits as the local device ID.

Example only:

```text
Waveshare Wi-Fi MAC: AA:BB:CC:DD:EE:FF
Device ID:           aabbccddeeff
MQTT client ID:      evo270-aabbccddeeff
MQTT root:           evo270/aabbccddeeff
```

There is nothing for the user to type or copy from the old Aqua Temp module. Each Waveshare creates its own unique MQTT/Home Assistant namespace automatically.

A fresh Home Assistant device/entity set is recommended rather than trying to preserve old cloud-integration history.

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

Known-good Function 03 commissioning capture:

```text
TX: 63 03 07 E3 00 01 7C CA
RX: 63 03 02 00 61 80 64
raw 97 -> 18.5 °C
```

## Why Arduino rather than ESPHome here?

ESPHome was the first approach. On this exact Waveshare ESP32-S3-RS485-CAN + EVO270/HW211 installation, ESPHome 2026.8.1 repeatedly timed out after partial one-byte `FE` responses even with the correct 9600 8N1/slave settings.

A direct Arduino test became reliable when the RS485 transceiver direction was driven explicitly:

1. GPIO21 HIGH
2. write the Modbus RTU frame
3. `RS485.flush()`
4. short guard delay
5. GPIO21 LOW
6. receive and validate the response

See [`docs/ESPHOME_FAILURE.md`](docs/ESPHOME_FAILURE.md). This is an **installation-specific interoperability finding**, not a claim that ESPHome Modbus is universally broken.

## Home Assistant

The local firmware publishes through MQTT and uses the Waveshare-derived device ID as its namespace.

The public dashboard template is included at:

[`home-assistant/dashboard/evo270-hot-water-dashboard.yaml`](home-assistant/dashboard/evo270-hot-water-dashboard.yaml)

It uses **Mushroom Cards** and **card-mod**. The template contains `replace_with_unit1_id` and `replace_with_unit2_id` placeholders because every user's Waveshare MAC will be different.

The old Aqua Temp **Controller Clocks** cards have been removed from the public dashboard because they depended on the previous cloud/custom integration rather than the local Arduino/MQTT implementation.

See [`home-assistant/README.md`](home-assistant/README.md) for installation instructions.

## For first-time builders

The documentation is being built so somebody can reproduce the project without already understanding RS485 or connector pin extraction. The photo walkthrough covers:

1. the EVO270 and controller area;
2. the original Wi-Fi/RS485 connector;
3. the Tempero 5-pin JST-SM pigtail;
4. original vs replacement connector;
5. releasing/de-pinning a JST-SM terminal;
6. the correctly re-pinned connector;
7. Waveshare DC+/DC-/A+/B- connections;
8. the completed harness;
9. the Waveshare installed inside the EVO270;
10. a successful Arduino Modbus response; and
11. the final Home Assistant device/dashboard.

## Repository layout

- `firmware/current/` – current public-safe read-only reference firmware
- `firmware/commissioning/` – known-good single-register OTA/browser-monitor sketches
- `firmware/testbench/` – RS485 master/slave bench sketches
- `home-assistant/` – dashboard template and installation/dependency notes
- `data/` – register, status-bit, mode and legacy entity/register mapping tables
- `docs/` – wiring, protocol, MQTT, commissioning, ESPHome failure analysis and project history
- `diagnostics/` – sanitized commissioning/failure excerpts
- `references/` – upstream projects and licensing notes

## Quick start

1. Obtain the Waveshare board and Tempero **5-pin male/female JST-SM locking pigtail**.
2. De-pin/re-pin the JST-SM pigtail to match the EVO270 connector, or splice it correctly by soldering.
3. Verify +12 V, GND, RS485 A and RS485 B before connecting the Waveshare.
4. Start with `firmware/commissioning/EVO270_Laundry_Bathroom_Arduino_OTA.ino` and confirm register 2019 reads correctly.
5. In `firmware/current/EVO270_ReadOnly_Reference/`, copy `secrets.example.h` to `secrets.h` and enter Wi-Fi/MQTT credentials. Keep `secrets.h` in the same Arduino sketch folder and never commit it.
6. Flash the current read-only firmware. The Waveshare's own Wi-Fi MAC automatically becomes its local device identity.
7. Confirm MQTT Discovery creates the EVO270 device/entities in Home Assistant.
8. Install Mushroom Cards and card-mod if using the supplied dashboard.
9. Import the dashboard YAML and replace the unit-ID placeholders with the entity IDs created on your Home Assistant system.
10. Leave Modbus writes disabled until every target register, range and side effect is independently verified.

## Credits

This work would have taken much longer without:

- **[sjtrny/esphome-hw211](https://github.com/sjtrny/esphome-hw211)** — especially the machine-readable HW211 Modbus protocol work and ESPHome component. That repository is MIT licensed.
- **[echopin664/EVO270-1-HWS](https://github.com/echopin664/EVO270-1-HWS)** — valuable prior work demonstrating local EVO270-1/HW211 RS485 control with ESP32 + Home Assistant and providing an EVO270 Modbus map.

See [`ACKNOWLEDGEMENTS.md`](ACKNOWLEDGEMENTS.md) and [`NOTICE.md`](NOTICE.md).

## Safety / scope

This is an independent community project and is not affiliated with or endorsed by EvoHeat, Aqua Temp, Waveshare or Home Assistant.

Disconnect/isolate mains power before opening the EVO270 enclosure. The low-voltage Wi-Fi/RS485 harness is inside equipment that also contains mains-voltage wiring.

Heat-pump hot-water controllers can have compressor protection, disinfection, anti-freeze and safety-related parameters. This repository intentionally defaults to **read-only** operation. Any future write support should be narrowly allow-listed and range-checked.
