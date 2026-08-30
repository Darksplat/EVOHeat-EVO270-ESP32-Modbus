# Home Assistant

This folder contains the Home Assistant dashboard used with the EVO270 local Modbus/MQTT project.

## Dashboard

The current dashboard as used on the development installation is:

- [`dashboard/evo270-hot-water-dashboard.yaml`](dashboard/evo270-hot-water-dashboard.yaml)

It contains two EVO270-1 units:

- Bathroom & Laundry — device code `34eae7b41fea`
- Ensuite & Kitchen — device code `34eae79f4bce`

The dashboard is preserved as actually used rather than silently replacing entity IDs with generic placeholders.

## Frontend requirements

The main HWS cards use:

1. **Mushroom Cards** — required for `custom:mushroom-template-card`.
2. **card-mod** — required for the `card_mod:` styling blocks.
3. Home Assistant's built-in **Sections** dashboard/view layout, Tile cards, headings, stacks and Select Options tile feature.

Mushroom Cards and card-mod are normally installed through HACS Frontend.

No custom card source code is copied into this repository; install the maintained upstream packages in Home Assistant/HACS.

## MQTT/read-only entities used

The main Bathroom & Laundry section expects these core entities:

```text
climate.34eae7b41fea
binary_sensor.34eae7b41fea_api_status
binary_sensor.34eae7b41fea_fault
sensor.34eae7b41fea_top_temperature_t03
sensor.34eae7b41fea_bottom_temperature_t02
sensor.34eae7b41fea_ambient_temperature_t01
```

The main Ensuite & Kitchen section expects:

```text
climate.34eae79f4bce
binary_sensor.34eae79f4bce_api_status
binary_sensor.34eae79f4bce_fault
sensor.34eae79f4bce_top_temperature_t03
sensor.34eae79f4bce_bottom_temperature_t02
sensor.34eae79f4bce_ambient_temperature_t01
```

The firmware deliberately preserves many of the original Aqua Temp-style entity identifiers through MQTT Discovery to reduce dashboard migration work.

## Operating-mode Select entities

The supplied dashboard currently references the entity IDs created on the development Home Assistant instance:

```text
select.backyard_evo270_bathroom_laundry_34eae7b41fea_34eae7b41fea_operating_mode
select.back_fence_garden_evo270_ensuite_kitchen_34eae79f4bce_34eae79f4bce_operating_mode
```

Home Assistant entity IDs can vary depending on device naming and prior entity history. A new installation may need to replace these two IDs with the operating-mode Select entities created on that Home Assistant system.

The public firmware is currently **read-only**. Any command sent to a read-only climate/select topic must not be treated as proof that the HWS setting was changed.

## Important: Controller Clocks section is legacy Aqua Temp

The bottom **Controller Clocks** section of the current dashboard is not provided by the Arduino + MQTT read-only firmware.

It references legacy Aqua Temp integration entities and the service:

```text
aqua_temp.sync_clock
```

and these installation-specific unit-time sensors:

```text
sensor.backyard_evo270_bathroom_laundry_34eae7b41fea_evo270_bathroom_laundry_unit_time
sensor.back_fence_garden_evo270_ensuite_kitchen_34eae79f4bce_evo270_ensuite_kitchen_unit_time
```

If the old Aqua Temp custom integration is no longer installed, remove the entire **Controller Clocks** section from the dashboard. It is retained in the supplied YAML because this file records the real dashboard from the development installation.

A future local implementation could expose/controller-sync HW211 clock registers directly, but that is outside the current read-only firmware scope.

## Installing the dashboard

For a YAML dashboard, copy the contents of `dashboard/evo270-hot-water-dashboard.yaml` into the appropriate Home Assistant dashboard/raw configuration.

Before saving it on another system:

1. Install Mushroom Cards and card-mod.
2. Confirm the EVO270 MQTT Discovery entities exist.
3. Replace the two device codes if your units use different legacy IDs.
4. Replace the operating-mode Select entity IDs with the IDs on your system if required.
5. Remove the Controller Clocks section unless the legacy Aqua Temp integration and its `aqua_temp.sync_clock` service are present.

## Custom components

The current Arduino + MQTT implementation does **not** require an ESPHome custom component or a Home Assistant custom integration for normal monitoring.

The historical ESPHome/HW211 work is documented separately under [`../esphome/`](../esphome/). That path is useful for protocol research and experimentation, but it is not a dependency of the working Arduino + MQTT firmware.
