# Home Assistant / MQTT design

## Goals

- local operation with no cloud dependency for monitoring
- preserve legacy Aqua Temp-style Home Assistant entity IDs where practical
- expose climate/select state without allowing unsafe writes during validation
- keep raw Modbus troubleshooting available via serial/browser logs

## Per-device MQTT roots

Example development profiles:

```text
evo270/34eae7b41fea
evo270/34eae79f4bce
```

Each unit must have a unique MQTT client ID, availability topic and Home Assistant unique ID namespace.

## Discovery

The installed V2.1.2 firmware publishes Home Assistant MQTT Discovery for:

- mapped numeric sensors
- status binary sensors
- power/fault/link indicators
- operating mode
- temperature unit
- climate current temperature
- climate target temperature
- HVAC mode/action/fan state

Legacy IDs are preserved deliberately even where the old Aqua Temp spelling/numbering is odd.

## Read-only command handling

The public implementation must not contain Function 06 or Function 16 write code.

If Home Assistant sends a command to a read-only climate/select command topic, the firmware should:

1. log that the command was ignored;
2. not transmit any Modbus write;
3. republish the real state.

## MQTT polling tiers

- dynamic state: every 20 seconds
- slow configuration values: every 5 minutes

This avoids continuously hammering the controller with a full map read.
