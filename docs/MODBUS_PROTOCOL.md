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

## Functions used by V2.2.7

- **03** — holding-register reads and write verification
- **06** — allow-listed single-register control writes
- **16** — controller-clock mailbox block/apply sequence

The firmware is not a general Modbus write console. Writes are constrained to tested registers/ranges.

## Data types

| Type | Decode |
|---|---|
| RAW | `x` |
| TEMP | signed `int16(x) * 0.1` °C |
| TEMP1 | `(x - 60) * 0.5` °C |
| DIGI2 | `x * 10` |
| DIGI4 | `x * 5` |
| DIGI7 | `x * 0.5` |

## Known-good read

Ambient T01 is register 2019 decimal / `0x07E3`, TEMP1.

```text
TX 63 03 07 E3 00 01 7C CA
RX 63 03 02 00 61 80 64
```

Raw 97 decodes to 18.5 °C.

## Verified write registers

See `data/core_registers.csv` for the current allow-list and read-only core/status registers.

The production write path checks the Modbus response and performs Function 03 readback where appropriate. Timer/date updates temporarily protect affected schedule-enable bits and restore them after successful field updates; rollback is attempted on failure.

## Clock mailbox

The deployed controller accepts the following clock command map on slave 99:

| Register | Purpose |
|---:|---|
| 1151 | M11 clock modify/apply flag |
| 1152 | minute |
| 1153 | hour |
| 1154 | day |
| 1155 | month |
| 1156 | year (two digit) |

These registers do **not** provide a continuously advancing readable clock. They retain command/mailbox values. V2.2.7 therefore pushes NTP-derived time on schedule rather than calculating drift from those registers.

## Legacy numbering

Aqua Temp entity names do not always line up one-for-one with newer HW211 protocol labels. The repository preserves useful legacy suffixes where practical. See `data/evo270_legacy_entity_map.csv`.
