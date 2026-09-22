# Legacy entity / HW211 register map

The full historical monitoring mapping remains in `data/evo270_legacy_entity_map.csv`.

## Frequently used monitoring registers

| Code | Register | Purpose | Decode |
|---|---:|---|---|
| R01 | 1104 | Target temperature | TEMP1 |
| T01 | 2019 | Ambient temperature | TEMP1 |
| T02 | 2020 | Bottom tank temperature | TEMP1 |
| T03 | 2021 | Top tank temperature | TEMP1 |
| T10 | 2025 | Display/app temperature | TEMP1 |
| O07 | 2060 | EEV current position | RAW |
| O08 | 2061 | Compressor accumulated run time | RAW |
| O09 | 2062 | Booster accumulated run time | RAW |

## Verified local-control registers

| Register | Purpose |
|---:|---|
| 1011 | Power |
| 1012 | Requested mode |
| 1104 | Target temperature |
| 1129 | Vacation date enable |
| 1130 | Vacation year |
| 1131 | Vacation month |
| 1132 | Vacation day |
| 1133 | Timer enable mask |
| 1134–1137 | Timer 1 ON/OFF hour/minute |
| 1138–1141 | Timer 2 ON/OFF hour/minute |
| 1151 | Clock apply/modify flag |
| 1152–1156 | Clock minute/hour/day/month/year mailbox |

Timer-enable mask bits in register 1133:

- bit 0: Timer 1 ON
- bit 1: Timer 1 OFF
- bit 2: Timer 2 ON
- bit 3: Timer 2 OFF

The 1151–1156 clock fields are a command mailbox, not a live clock readback.

## Status bitfields

Register 2050 contains S01–S06 input/status bits and O01–O11-style output states used by the legacy entity model. Register 2051 contains shutdown, DTU/Wi-Fi online, defrost and high-temperature hot-water stage flags.

See `data/status_bits.csv` for bit positions.
