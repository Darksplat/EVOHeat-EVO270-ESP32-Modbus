# Home Assistant / MQTT design

## Goals

- local operation without the Aqua Temp cloud
- preserve useful EVO270/HW211 entity naming
- expose detailed monitoring
- expose only field-verified control writes
- retain browser/serial diagnostics

## Discovery and identity

The current V2.2.7 deployment sketches use explicit per-unit identities so the local MQTT entities remain stable for the two migrated controllers. MQTT Discovery creates the climate, select, switch, time, date, status and diagnostic entities consumed by the current dashboard.

## Verified command handling

Commands are accepted only for tested functions:

- power;
- requested operating mode;
- target temperature;
- Vacation enable/date;
- Timer 1 and Timer 2 enable/time fields.

The controller clock uses a dedicated mailbox/apply sequence rather than a normal live-clock entity.

Unknown or non-allow-listed controller parameters are not exposed as writable controls.

## Polling

- dynamic state: approximately every 20 seconds
- slow/configuration state: approximately every 5 minutes

## Clock state

Home Assistant receives:

- `sensor.<device_id>_clock_sync_status`
- `sensor.<device_id>_last_clock_command`

It intentionally does not expose a fake live controller clock or drift measurement because registers 1151–1156 are command/mailbox registers.

## Dashboard

Use `home-assistant/dashboard/hot-water-dashboard.yaml`.
