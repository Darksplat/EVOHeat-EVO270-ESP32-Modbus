# Hardware and wiring

## Tested hardware

- EVOHeat EVO270-1 heat-pump hot-water system
- HW211-family controller
- Waveshare ESP32-S3-RS485-CAN
- ESP32 Arduino Core 3.3.x during the final commissioning work

## Waveshare UART / direction pins

| Signal | ESP32-S3 GPIO |
|---|---:|
| RS485 TX | 17 |
| RS485 RX | 18 |
| RS485 direction / EN | 21 |

Direction behaviour verified on the installation:

- GPIO21 **HIGH** = transmit
- GPIO21 **LOW** = receive

The sequence that proved reliable was:

```cpp
digitalWrite(RS485_EN_PIN, HIGH);
delayMicroseconds(200);
RS485.write(frame, frame_len);
RS485.flush();
delayMicroseconds(200);
digitalWrite(RS485_EN_PIN, LOW);
```

## HWS harness observed during commissioning

| Observed wire | Function |
|---|---|
| Red | DC+ |
| Black | DC- |
| White | RS485 A+ |
| Yellow | RS485 B- |
| Orange | shield/earth; left unterminated at the Waveshare |

These colours are **installation-specific observations**, not a universal EVOHeat colour standard. Verify terminal labels/pinout before connection.

## Serial settings

- Modbus RTU
- 9600 baud
- 8 data bits
- no parity
- 1 stop bit
- DTU/Wi-Fi slave 99 (`0x63`)
