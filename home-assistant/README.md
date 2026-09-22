# Home Assistant

The canonical dashboard for the deployed EVO270 controllers is:

- `dashboard/hot-water-dashboard.yaml`

This is the current two-unit dashboard for the Bathroom & Laundry and Ensuite & Kitchen V2.2.7 controllers.

## Frontend requirements

- **Mushroom Cards**
- **card-mod**
- Home Assistant built-in Sections layout, Tile cards, headings, grids and stacks

## Local control

MQTT Discovery from V2.2.7 provides the verified local controls used by the dashboard:

- climate target temperature;
- operating mode;
- Vacation-date enable and return date;
- Timer 1 ON/OFF enables and times;
- Timer 2 ON/OFF enables and times;
- clock-sync status and last clock command.

The dashboard also exposes tank/heat-pump temperatures, compressor, booster, defrost, fans, valve/pump states, system health and disinfection diagnostics.

## Controller clock

The old Aqua Temp `aqua_temp.sync_clock` service is no longer used.

Registers 1151–1156 are a **command/apply mailbox**, not a live readable clock. Consequently the dashboard shows the local firmware's **clock sync status** and **last clock command** rather than claiming to display the controller's continuously advancing time.

The firmware forces an NTP-derived clock command each Monday during the 01:00 local Melbourne time hour. The embedded controller page also offers `/clock-test` for a manual push.

## Installing

1. Flash the appropriate V2.2.7 sketch.
2. Confirm the EVO270 device is online in MQTT/Home Assistant.
3. Install Mushroom Cards and card-mod.
4. Import `dashboard/hot-water-dashboard.yaml`.
5. Confirm all entities resolve before using schedule or temperature controls.

No Aqua Temp cloud/custom integration is required for these local controls.
