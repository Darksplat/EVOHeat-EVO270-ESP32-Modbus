# Commissioning procedure

## 1. Verify hardware before Home Assistant

Flash:

`firmware/commissioning/EVO270_Laundry_Bathroom_Arduino_OTA.ino`

Expected serial header:

```text
RS485 TX : GPIO17
RS485 RX : GPIO18
RS485 EN : GPIO21
Baud     : 9600 8N1
Slave    : 99
Function : 03
Register : 2019
```

A successful response should be seven bytes for a one-register Function 03 read.

## 2. Validate ambient temperature

Register 2019 / T01 decodes as:

```text
(raw - 60) × 0.5 °C
```

If the returned value is sensible for ambient conditions, the bus direction, A/B polarity, baud, slave and CRC handling are all strongly indicated to be correct.

## 3. Add the browser monitor

Flash:

`firmware/commissioning/EVO270_Laundry_Bathroom_Arduino_OTA_WebMonitor.ino`

Use the local hostname or IP shown on serial to view the live log.

## 4. Prepare the current firmware secrets

In:

`firmware/current/EVO270_ReadOnly_Reference/`

copy:

`secrets.example.h` → `secrets.h`

Set Wi-Fi, broker and MQTT credentials locally. Keep `secrets.h` in that same Arduino sketch folder and never commit it.

## 5. Move to the canonical read-only bridge

Use `firmware/current/EVO270_ReadOnly_Reference/`.

Select the friendly/location profile in `device_profile.h` if required. The actual device identity does **not** need to be typed in: the firmware derives it automatically from the Waveshare ESP32 Wi-Fi MAC.

## 6. Home Assistant validation

After MQTT Discovery, confirm:

- a fresh EVO270 device appears using the Waveshare-derived identity;
- device availability is online;
- T01, T02, T03 and T10 are plausible;
- R01 matches the displayed set point;
- power and fault state are sensible;
- operating mode is correct;
- status bits change appropriately during heating/idle/defrost cycles.

Do not add writes until the read-only map is stable.
