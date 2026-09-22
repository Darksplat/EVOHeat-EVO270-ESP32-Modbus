# EVOHeat EVO270-1 local ESP32 / Modbus control

Local, cloud-independent monitoring and **field-verified local control** of an **EVOHeat EVO270-1** heat-pump hot-water system using the **HW211-family controller**, a **Waveshare ESP32-S3-RS485-CAN**, MQTT and Home Assistant.

> **Current status:** V2.2.7 is deployed and validated on two EVO270-1 units. Monitoring, target temperature, power, operating mode, Vacation scheduling, Timer 1/2 scheduling and controller-clock setting are local. Writes are deliberately restricted to a tested allow-list.

## Current firmware

Two exact deployment sketches are kept under `firmware/current/`:

- `EVO270_Laundry_Bathroom_V2_2_7_ClockTestLink/`
- `EVO270_Ensuite_Kitchen_V2_2_7_ClockTestLink/`

Each Arduino sketch folder also contains `evo270_types.h` and a safe `secrets.example.h`. Copy the example to `secrets.h` locally and **never commit credentials**.

V2.2.7 adds a main-page link to the manual clock-test page and retains the validated V2.2.6 weekly forced NTP clock synchronization.

## Hardware used / where to get it

| Part | Source | Notes |
|---|---|---|
| Waveshare ESP32-S3-RS485-CAN | [Waveshare product page](https://www.waveshare.com/esp32-s3-rs485-can.htm) | Exact controller board used. It accepts 7–36 V DC at the screw-terminal input and has isolated RS485 onboard. |
| EVO270 replacement connector / pigtail | [Tempero Systems – 5-pin male/female JST-SM locking pigtail](https://temperosystems.com.au/products/5pin-male-female-jst-sm-locking-pigtail/) | Physically suitable pigtail. **Check and rearrange the supplied pins/wires to the EVO270 mapping before use.** |
| EVO270-1 | [EvoHeat EVO270-1 product page](https://evoheat.com.au/hot-water-heat-pump/evo-270/) | Manufacturer reference/manual source. |

See `docs/KNOWN_WORKING_HARDWARE.md` and `docs/HARDWARE_AND_WIRING.md`.

## Wiring

Observed on the tested installation:

| EVO270 harness wire | Function | Waveshare connection |
|---|---|---|
| Red | +12 V DC | DC+ |
| Black | Ground | DC- |
| White | RS485 A | A+ |
| Yellow | RS485 B | B- |
| Orange | Shield / earth | Not connected to the Waveshare in this installation |

**Verify connector position, function, polarity and continuity before applying power.** The Waveshare may be powered from the EVO270 12 V accessory supply through its DC screw terminals. Do not apply 12 V to a bare ESP32 5 V or 3.3 V pin.

## Proven transport

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
| Fast poll | 20 s |
| Slow/config poll | 5 min |

Known-good Function 03 commissioning capture:

```text
TX: 63 03 07 E3 00 01 7C CA
RX: 63 03 02 00 61 80 64
raw 97 -> 18.5 °C
```

## Verified local writes

V2.2.7 does **not** provide unrestricted Modbus writing. The production sketches allow only the registers and ranges verified on the installed units.

| Register(s) | Function |
|---|---|
| 1011 | Power |
| 1012 | Requested operating mode |
| 1104 | Target water temperature |
| 1129–1132 | Vacation date enable/date |
| 1133–1141 | Timer 1 / Timer 2 enable mask and times |
| 1151–1156 | Controller clock command/apply mailbox |

Normal single-register controls use Function 06 with response validation and Function 03 readback. Schedule/date changes protect the active timer mask while changing related fields and attempt rollback if validation fails.

## Controller clock

The clock work uncovered an important protocol detail: registers **1151–1156 are not a continuously readable controller clock**. They behave as a clock command/apply mailbox.

The proven sequence writes minute/hour/day/month/year to 1152–1156, verifies the payload, then applies it with M11 at register 1151.

Both deployed ESP32s obtain Melbourne/Victoria time from NTP using DST rules and force a clock push once each **Monday during the 01:00 local hour**. A failed scheduled attempt can retry in ten-minute buckets during that hour.

The embedded web UI also provides `/clock-test` for an explicit manual NTP clock push.

## Home Assistant

MQTT Discovery exposes the monitoring and control entities. The current two-unit dashboard is:

[`home-assistant/dashboard/hot-water-dashboard.yaml`](home-assistant/dashboard/hot-water-dashboard.yaml)

It uses **Mushroom Cards** and **card-mod** and includes:

- main temperature, target and operating-mode controls;
- tank and heat-pump temperatures;
- compressor, booster, defrost, fan, valve and pump state;
- Modbus/API/power/fault health;
- Vacation return date;
- Timer 1 and Timer 2 controls;
- disinfection settings/status; and
- local clock-sync status and last clock command.

See `home-assistant/README.md`.

## Solar register warning

The N01–N11 / 1080–1090 register block appears to describe **solar-thermal collector/pump control**, not rooftop PV curtailment or PV-surplus control. It remains monitoring-only in this project unless the relevant solar-thermal hardware and write behavior are independently verified.

## Why Arduino rather than ESPHome here?

ESPHome 2026.8.1 was the first approach. On this exact Waveshare ESP32-S3-RS485-CAN + EVO270/HW211 installation, it repeatedly timed out after partial one-byte `FE` responses. A direct Arduino transaction became reliable when GPIO21 direction control was driven explicitly.

See `docs/ESPHOME_FAILURE.md`. This is an installation-specific interoperability finding, not a claim that ESPHome Modbus is universally broken.

## Repository layout

- `firmware/current/` – exact V2.2.7 deployment sketches
- `firmware/commissioning/` – early known-good single-register OTA/browser-monitor sketches
- `firmware/testbench/` – RS485 master/slave bench sketches
- `home-assistant/` – current dashboard and Home Assistant notes
- `data/` – register, mode and status-bit mapping tables
- `docs/` – wiring, protocol, commissioning, MQTT and project history
- `diagnostics/` – sanitized commissioning/failure excerpts
- `references/` – upstream projects and licensing notes

## Quick start

1. Wire the Waveshare to the EVO270 low-voltage RS485/accessory connector and verify polarity.
2. Open the V2.2.7 sketch folder for the unit being commissioned.
3. Copy `secrets.example.h` to `secrets.h` and enter Wi-Fi, MQTT and optional OTA credentials locally.
4. In Arduino IDE select **ESP32S3 Dev Module**.
5. Build/flash, then verify the embedded web page and `/diag`.
6. Confirm MQTT Discovery creates the expected Home Assistant entities.
7. Install Mushroom Cards and card-mod if using the supplied dashboard.
8. Import `home-assistant/dashboard/hot-water-dashboard.yaml`.

## Credits

This project builds on published community work from:

- **[sjtrny/esphome-hw211](https://github.com/sjtrny/esphome-hw211)** — structured HW211 protocol data and ESPHome implementation reference.
- **[echopin664/EVO270-1-HWS](https://github.com/echopin664/EVO270-1-HWS)** — earlier practical EVO270-1 local-control work.

See `ACKNOWLEDGEMENTS.md` and `NOTICE.md`.

## Safety / scope

This is an independent community project and is not affiliated with or endorsed by EvoHeat, Aqua Temp, Waveshare or Home Assistant.

Disconnect/isolate mains power before opening the EVO270 enclosure. The low-voltage Wi-Fi/RS485 harness is inside equipment that also contains mains-voltage wiring.

Heat-pump hot-water controllers contain compressor protection, disinfection, anti-freeze and other safety-related parameters. Do not extend the write allow-list without validating the register, valid range and side effects on the exact controller.
