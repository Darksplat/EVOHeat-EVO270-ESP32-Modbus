# Home Assistant

This folder contains the Home Assistant dashboard template for the EVO270 local Modbus/MQTT project.

## Dashboard

Use:

- [`dashboard/evo270-hot-water-dashboard.yaml`](dashboard/evo270-hot-water-dashboard.yaml)

The public dashboard is a **fresh-install template**. It does not need the MAC/device codes of the removed Aqua Temp wireless modules.

The firmware automatically derives each new device identity from the replacement **Waveshare ESP32 Wi-Fi MAC**. After MQTT Discovery creates the devices in Home Assistant, replace the dashboard placeholders with the IDs Home Assistant created for your two units.

## Frontend requirements

The dashboard uses:

1. **Mushroom Cards** — required for `custom:mushroom-template-card`.
2. **card-mod** — required for the `card_mod:` styling blocks.
3. Home Assistant's built-in **Sections** dashboard/view layout, Tile cards, headings, stacks and Select Options tile feature.

Mushroom Cards and card-mod are normally installed through HACS Frontend.

No custom card source code is copied into this repository; install the maintained upstream packages in Home Assistant/HACS.

## Device IDs

The public firmware uses the Waveshare Wi-Fi MAC with the colons removed and lower-cased.

Example only:

```text
Waveshare MAC: AA:BB:CC:DD:EE:FF
Device ID:     aabbccddeeff
```

The dashboard uses these placeholders:

```text
replace_with_unit1_id
replace_with_unit2_id
```

After flashing both Waveshare controllers and allowing MQTT Discovery to run, use Home Assistant **Developer Tools -> States** or the device/entity pages to identify each unit's actual entity IDs and replace the placeholders.

## Operating-mode Select entities

The two operating-mode cards use deliberately generic placeholders:

```text
select.replace_with_unit1_operating_mode
select.replace_with_unit2_operating_mode
```

Home Assistant Select entity IDs can vary depending on firmware/discovery naming and the Entity Registry. Replace these with the actual operating-mode Select entities on your system.

The public firmware remains **read-only**. A displayed or selectable mode must not be treated as proof that the HWS controller setting was changed unless write support is deliberately implemented and tested later.

## Controller Clocks

The old dashboard contained a **Controller Clocks** section using the previous Aqua Temp integration's `aqua_temp.sync_clock` service and `unit_time` sensors.

That section is not part of the public MQTT-only dashboard. It is not required for local Modbus monitoring and is not provided by the current read-only Arduino firmware.

## Installing the dashboard

1. Flash the local Arduino/MQTT firmware onto each Waveshare controller.
2. Confirm both devices appear in Home Assistant through MQTT Discovery.
3. Install Mushroom Cards and card-mod.
4. Copy the contents of `dashboard/evo270-hot-water-dashboard.yaml` into a Home Assistant dashboard/raw configuration.
5. Replace `replace_with_unit1_id` and `replace_with_unit2_id` with the actual Waveshare-derived IDs/entities on your system.
6. Replace the two operating-mode Select placeholders if required.

## Custom components

The Arduino + MQTT implementation does **not** require an ESPHome custom component or a Home Assistant custom integration for normal monitoring.

The historical ESPHome/HW211 work is documented separately under [`../esphome/`](../esphome/). That path is useful for protocol research and experimentation, but it is not a dependency of the working Arduino + MQTT firmware.
