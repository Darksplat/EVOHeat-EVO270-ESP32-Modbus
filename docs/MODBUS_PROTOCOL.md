# EVO270 / HW211 Modbus notes

## Transport

| Parameter | Value |
|---|---|
| Protocol | Modbus RTU |
| Baud | 9600 |
| Data | 8 |
| Parity | none |
| Stop bits | 1 |
| DTU/Wi-Fi slave | 99 |
| CRC | CRC-16/MODBUS, low byte first |

The upstream HW211 protocol describes Function 03, 06 and 16 support. **This project currently implements Function 03 only.**

## Data types used here

| Type | Decode |
|---|---|
| RAW | `x` |
| TEMP | signed `int16(x) * 0.1` °C |
| TEMP1 | `(x - 60) * 0.5` °C |
| DIGI2 | `x * 10` |
| DIGI4 | `x * 5` |
| DIGI7 | `x * 0.5` |

## Known-good register

Ambient temperature:

- code: T01
- register: 2019 decimal / `0x07E3`
- type: TEMP1

Example:

```text
TX 63 03 07 E3 00 01 7C CA
RX 63 03 02 00 61 80 64
```

Data bytes `00 61` = raw 97.

`(97 - 60) × 0.5 = 18.5 °C`

## Core registers

See `data/core_registers.csv`. The public firmware treats all of them as read-only.

## Legacy numbering

The old Aqua Temp entity names do not always line up one-for-one with the newer HW211 protocol labels. The repository preserves those old IDs where practical so existing Home Assistant dashboards can continue to work. See `data/evo270_legacy_entity_map.csv`.
