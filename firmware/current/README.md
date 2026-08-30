# Current firmware

`EVO270_ReadOnly_Reference/` is a consolidated public-safe reference implementation that captures the proven V2.1.2 architecture.

It is intentionally labelled **reference** because the two locally-installed V2.1.2 sketches evolved through several commissioning edits and this repository package does not claim a byte-for-byte export of those private local files.

The important field-proven behaviour is retained:

- GPIO17 TX
- GPIO18 RX
- GPIO21 explicit RS485 direction
- 9600 8N1
- slave 99
- Function 03 reads only
- TEMP1/DIGI decoding
- fast/slow polling
- OTA
- browser diagnostics
- MQTT
- Home Assistant Discovery
- legacy entity ID preservation

For exact development filenames and history see `../docs/PROJECT_HISTORY.md`.
