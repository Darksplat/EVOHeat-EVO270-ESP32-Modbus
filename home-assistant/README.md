# Home Assistant

This folder contains the Home Assistant dashboard used with the two EVOHeat EVO270-1 installations developed alongside this project.

## Dashboard

`dashboard/hot-water-dashboard-current.yaml` is a snapshot of the real dashboard in use during development.

It covers:

- Bathroom & Laundry EVO270
- Ensuite & Kitchen EVO270
- current water temperature and heating state
- target temperature
- operating mode
- top, bottom and ambient temperatures
- API/network status
- fault status
- controller clock display/synchronisation from the earlier Aqua Temp integration

## Frontend requirements

The dashboard uses two HACS/frontend additions:

1. **Mushroom Cards** — the dashboard uses `custom:mushroom-template-card`.
2. **card-mod** — used to control card height, border radius, icon size and typography.

These are Home Assistant frontend resources only. They are not firmware components and do not run on the ESP32.

## Important: current dashboard vs public Arduino + MQTT firmware

The supplied dashboard is intentionally preserved as the real installation dashboard, but it contains a few deployment-specific items.

### Controller Clocks

The `Controller Clocks` section calls:

```text
aqua_temp.sync_clock
```

and uses the old Aqua Temp integration's `unit_time` sensors.

Those entities/services are **not created by the Arduino + MQTT firmware in this repository**. If you are installing the local MQTT firmware without the old Aqua Temp custom integration, remove the `Controller Clocks` section.

### Operating mode controls

The public firmware is currently deliberately **read-only**. It may expose the operating mode state for compatibility, but Modbus write functions are not enabled. Do not expect a dashboard mode selection to change the heat-pump controller while using the public read-only firmware.

### Entity IDs

The YAML contains the entity IDs from the development Home Assistant installation, including the two original device codes:

- Bathroom & Laundry: `34eae7b41fea`
- Ensuite & Kitchen: `34eae79f4bce`

Home Assistant entity IDs can vary depending on existing Entity Registry entries, device names and migration history. Check Developer Tools -> States after MQTT Discovery and adjust the dashboard YAML if your entity IDs differ.

## Do I need the Aqua Temp custom component?

**No, not for the Arduino + MQTT implementation.**

The ESP32 talks Modbus locally and publishes directly to Home Assistant through MQTT Discovery. The old Aqua Temp custom component is only relevant if you want to retain its cloud/service-specific features such as the clock synchronisation action used in the historical dashboard.

## Do I need the ESPHome HW211 custom component?

**No, not for the working Arduino firmware.**

The ESPHome approach is retained in this repository for technical history and experimentation, but the field-proven implementation documented here uses Arduino + MQTT because explicit RS485 direction/turnaround control was reliable on the tested Waveshare ESP32-S3-RS485-CAN hardware.

See `../docs/ESPHOME_FAILURE.md` for the details.

## Recommended public dashboard

For a clean new installation using only this repository's read-only Arduino + MQTT firmware:

1. install Mushroom Cards and card-mod through HACS;
2. confirm MQTT Discovery has created the EVO270 entities;
3. copy the supplied dashboard YAML;
4. remove the `Controller Clocks` section unless the old Aqua Temp integration is also installed;
5. verify/replace entity IDs for your own EVO270 device;
6. treat mode/target controls as display-only until write support is deliberately implemented and tested.
