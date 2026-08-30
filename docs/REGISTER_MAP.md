# Legacy entity / HW211 register map

The authoritative full mapping is maintained in [`data/evo270_legacy_entity_map.csv`](../data/evo270_legacy_entity_map.csv).

It contains all 83 legacy Aqua Temp-style entities captured during migration, including:

- direct HW211 register mappings;
- decode type (`RAW`, `TEMP1`, `DIGI2`, `DIGI4`, `DIGI7`);
- fast vs slow polling tier;
- status-bit mappings from registers 2050/2051;
- legacy naming/numbering differences;
- T11/T12 placeholders where no reliable local DTU/Wi-Fi mapping was found.

## Frequently used registers

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

## Status bitfields

Register `2050` contains the S01-S06 input/status bits and O01-O11-style output states used by the legacy entity model. Register `2051` contains shutdown, DTU/Wi-Fi online, defrost and high-temperature hot-water stage flags.

See [`data/status_bits.csv`](../data/status_bits.csv) for the exact bit positions.
