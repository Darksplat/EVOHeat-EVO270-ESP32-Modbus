# Known-working hardware

This page records the hardware actually used to make the EVO270-1/HW211 local Modbus monitor work reliably.

## Controller board

### Waveshare ESP32-S3-RS485-CAN

Product page:

https://www.waveshare.com/esp32-s3-rs485-can.htm

Why this board worked well for the project:

- ESP32-S3 with Wi-Fi
- isolated RS485 onboard
- 7–36 V DC screw-terminal input, so the EVO270's 12 V accessory supply can power the module
- DIN-style enclosed form factor
- USB-C programming/debugging
- enough control over the UART/RS485 direction timing to reproduce the HW211 communication reliably in Arduino

Working project connections:

| Waveshare terminal/function | EVO270 harness |
|---|---|
| DC+ | Red / +12 V |
| DC- | Black / GND |
| RS485 A+ | White / RS485 A |
| RS485 B- | Yellow / RS485 B |
| No Waveshare connection used | Orange / shield-earth on this installation |

The firmware uses GPIO17 TX, GPIO18 RX and GPIO21 RS485 direction internally.

## Replacement EVO270 connector

Connector parts used were sourced from **Tempero Systems**:

https://temperosystems.com.au/products/jst-xh-series-crimp-style-2-5mm/

Select:

- **5-way JST-XH-style 2.5 mm housing**
- matching **2.5 mm crimp pins**

### The important part: re-pin the connector

The physical connector fitting the EVO270 does **not** guarantee that the supplied or preassembled wire order is correct for the heat pump.

Before plugging a replacement connector into the EVO270:

1. Put the replacement housing beside the original EVO270 connector in the same orientation.
2. Identify each original conductor by function: +12 V, GND, RS485 A, RS485 B and shield/earth.
3. Check the new connector cavity order.
4. If a terminal is in the wrong cavity, use a fine pick/terminal-release tool to gently lift the plastic retaining tang.
5. Pull the crimp terminal out from the rear of the housing.
6. Reinsert it into the correct cavity until the retaining tang clicks/locks.
7. Repeat until the replacement connector matches the EVO270 harness function-for-function.
8. Check continuity and polarity before applying power.

Do **not** plug a prewired connector into the heat pump simply because the housing fits.

## Alternative: snip and solder

If you do not want to de-pin/re-pin a connector, the practical alternative is to splice the harness:

1. isolate power to the EVO270;
2. cut the lead with enough length left to work on;
3. solder the replacement lead conductors function-for-function;
4. heat-shrink every conductor individually;
5. add an outer protective sleeve/heat-shrink for strain relief;
6. verify continuity and polarity before reconnecting the heat pump.

For the tested installation:

```text
EVO270          Waveshare
----------------------------
Red    +12 V -> DC+
Black  GND   -> DC-
White  485-A -> A+
Yellow 485-B -> B-
Orange shield -> not connected
```

## Beginner photo walkthrough

Photos are intentionally part of the documentation plan because the connector and wire ordering are much easier to understand visually than from a table alone.

The recommended photo set is:

1. **Original EVO270 Wi-Fi/RS485 connector** — close-up before anything is disconnected.
2. **Original wire colours** — Red, Black, White, Yellow and Orange visible entering the connector.
3. **Tempero connector parts** — 5-way housing and loose crimp pins.
4. **Terminal release** — fine pick lifting the retaining tang while a terminal is withdrawn.
5. **Corrected replacement connector** — rear/wire side clearly showing the final arrangement.
6. **Waveshare terminals** — DC+, DC-, A+ and B- visible.
7. **Completed harness** — connector at one end and Waveshare terminal connections at the other.
8. **Installed Waveshare** — board mounted inside the EVO270 enclosure clear of likely water collection areas.
9. **Arduino commissioning result** — browser/serial monitor with valid TX/RX Modbus frames.
10. **Home Assistant result** — final EVO270 device/entities after MQTT Discovery.

When adding photos, avoid publishing Wi-Fi passwords, MQTT credentials, serial numbers or unrelated household/network information visible in screenshots.

## Manufacturer/reference links

- EVOHeat EVO270-1: https://evoheat.com.au/hot-water-heat-pump/evo-270/
- Waveshare ESP32-S3-RS485-CAN wiki: https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN

## Safety

The EVO270 enclosure contains mains-voltage equipment. Isolate power before opening the unit or changing the low-voltage connector/harness.

The wire colours above are what was observed on the tested EVO270-1 installations. Always verify the actual connector/terminal function on the unit being modified rather than assuming a third-party replacement lead uses the same colour order.
