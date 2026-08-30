# Creating the GitHub repository

Recommended repository name:

`EVOHeat-EVO270-ESP32-Modbus`

Recommended description:

> Local read-only EVOHeat EVO270-1 / HW211 Modbus monitoring with Waveshare ESP32-S3-RS485-CAN, MQTT and Home Assistant.

If creating it manually on GitHub, create it **empty** (do not add another README, licence or .gitignore), then upload/push this tree.

Example local commands:

```bash
git init
git branch -M main
git add .
git commit -m "Initial EVO270 local Modbus documentation and firmware"
git remote add origin git@github.com:Darksplat/EVOHeat-EVO270-ESP32-Modbus.git
git push -u origin main
```
