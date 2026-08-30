# ESPHome commissioning failure on the Waveshare board

## Scope

This documents what happened on **this specific installation** using:

- ESPHome 2026.8.1
- Waveshare ESP32-S3-RS485-CAN
- EVOHeat EVO270-1 / HW211
- 9600 8N1
- slave 99

It should **not** be read as a blanket claim that ESPHome Modbus is defective.

## Symptom

ESPHome could start, connect to Home Assistant and send Modbus requests, but the HWS transactions repeatedly failed. The log pattern was:

```text
[EVO270 RS485] <<< FE
[modbus] Clearing buffer of 1 bytes - timeout after partial response
[modbus_controller] Modbus command to device=99 register=... no response received
```

The single `FE` byte repeated while valid multi-byte Modbus RTU replies did not materialise.

## What made Arduino work

A bare Arduino sketch using the same:

- TX GPIO17
- RX GPIO18
- EN GPIO21
- 9600 8N1
- slave 99
- register 2019

worked when direction control was handled explicitly:

```text
GPIO21 HIGH
write request
RS485.flush()
~200 µs guard time
GPIO21 LOW
receive
```

That test produced a valid response immediately.

## Working hypothesis

The practical fault was an **RS485 transceiver direction / turnaround interoperability issue** between the generic ESPHome UART/Modbus flow-control behaviour and the Waveshare board/HW211 timing requirements.

The evidence supports that conclusion because:

1. the same UART pins and serial parameters were used;
2. the same slave/register were used;
3. physical wiring did not change;
4. explicit DE/RE control produced valid CRC-checked replies;
5. ESPHome showed repeatable one-byte partial responses/timeouts.

## Decision

The project moved to Arduino for the local HWS bridge while still integrating with Home Assistant through MQTT Discovery.

That gives full control over the critical direction sequence and keeps the HWS communication path easy to inspect at raw-frame level.

## Future ESPHome retest

A future ESPHome version could be retested if it allows deterministic control equivalent to:

```cpp
EN = HIGH;
uart.write(frame);
uart.flush();
delayMicroseconds(200);
EN = LOW;
```

Any retest should first use T01/register 2019 only, with all write functions disabled.
