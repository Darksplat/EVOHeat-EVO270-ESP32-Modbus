# ESPHome notes

ESPHome was investigated first because it is normally an excellent Home Assistant fit.

On the tested Waveshare ESP32-S3-RS485-CAN installation, ESPHome 2026.8.1 did not produce reliable HW211 replies. It repeatedly logged a single `FE` receive byte followed by a partial-response timeout.

The known-good direct Arduino implementation uses explicit direction timing on GPIO21.

See:

- `../docs/ESPHOME_FAILURE.md`
- `../diagnostics/esphome-partial-response-fe-excerpt.txt`

No non-working ESPHome YAML is presented as a recommended configuration here. The upstream projects remain linked in `../references/README.md` for anyone wishing to retest future ESPHome versions.
