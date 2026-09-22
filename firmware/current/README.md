# Current firmware

The current field-tested release is **V2.2.7-clock-test-link**.

Exact deployed sketches:

- `EVO270_Laundry_Bathroom_V2_2_7_ClockTestLink/`
- `EVO270_Ensuite_Kitchen_V2_2_7_ClockTestLink/`

Each sketch folder contains:

- the matching `.ino`;
- `evo270_types.h`;
- `secrets.example.h`.

Copy `secrets.example.h` to `secrets.h` locally. The real `secrets.h` is ignored by git and must not be committed.

## Field-proven behavior

- Waveshare ESP32-S3-RS485-CAN
- GPIO17 TX / GPIO18 RX / GPIO21 RS485 direction
- 9600 8N1, Modbus slave 99
- Function 03 monitoring
- narrowly allow-listed Function 06 controls
- Function 03 exact readback validation after normal writes
- protected Vacation/Timer 1/Timer 2 writes
- controller clock command through the 1151–1156 mailbox
- NTP with Melbourne/Victoria DST rules
- forced Monday 01:00 local clock push
- manual `/clock-test`
- OTA on port 3232
- browser diagnostics
- MQTT Discovery for Home Assistant

The old V2.1.2 read-only reference implementation was removed from `firmware/current/` when V2.2.7 became the validated deployment baseline. Historical commissioning/testbench material remains elsewhere in the repository because it is still useful for troubleshooting.
