/*
  EVOHeat EVO270-1 + Waveshare ESP32-S3-RS485-CAN
  Bare-bones Modbus RTU test with Wi-Fi + Arduino OTA

  Waveshare RS485:
    TX  = GPIO17
    RX  = GPIO18
    EN  = GPIO21

  EVO270 / HW211:
    Modbus slave address = 99
    Baud                 = 9600
    Format               = 8N1
    Test register        = 2019 (Ambient Temperature)
    Function             = 0x03 Read Holding Registers

  Wiring observed on this installation:
    Red    -> DC+
    Black  -> DC-
    White  -> A+
    Yellow -> B-
    Orange -> earth/shield, not terminated at Waveshare
*/

#include <WiFi.h>
#include <ArduinoOTA.h>

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* OTA_HOSTNAME  = "evo270-laundry-bathroom";

constexpr int RS485_TX_PIN = 17;
constexpr int RS485_RX_PIN = 18;
constexpr int RS485_EN_PIN = 21;
HardwareSerial RS485(1);

constexpr uint8_t  EVO_SLAVE_ADDRESS = 99;
constexpr uint16_t AMBIENT_REGISTER  = 2019;
constexpr unsigned long POLL_INTERVAL_MS = 5000;
unsigned long lastPoll = 0;

uint16_t modbusCRC(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) { crc >>= 1; crc ^= 0xA001; }
      else crc >>= 1;
    }
  }
  return crc;
}

void rs485ReceiveMode() { digitalWrite(RS485_EN_PIN, LOW); delayMicroseconds(100); }
void rs485TransmitMode() { digitalWrite(RS485_EN_PIN, HIGH); delayMicroseconds(100); }

void printHexBuffer(const uint8_t* buffer, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (buffer[i] < 0x10) Serial.print('0');
    Serial.print(buffer[i], HEX);
    if (i + 1 < length) Serial.print(' ');
  }
  Serial.println();
}

bool readHoldingRegister(uint8_t slave, uint16_t reg, uint16_t& value) {
  while (RS485.available()) RS485.read();

  uint8_t request[8];
  request[0] = slave;
  request[1] = 0x03;
  request[2] = highByte(reg);
  request[3] = lowByte(reg);
  request[4] = 0x00;
  request[5] = 0x01;
  uint16_t crc = modbusCRC(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  Serial.println();
  Serial.println("----------------------------------------");
  Serial.print("TX: ");
  printHexBuffer(request, sizeof(request));

  rs485TransmitMode();
  RS485.write(request, sizeof(request));
  RS485.flush();
  delayMicroseconds(200);
  rs485ReceiveMode();

  uint8_t response[32];
  size_t responseLength = 0;
  unsigned long startTime = millis();
  unsigned long lastByteTime = startTime;
  bool receivedAnyByte = false;

  while ((millis() - startTime) < 1000) {
    ArduinoOTA.handle();
    while (RS485.available()) {
      uint8_t b = RS485.read();
      if (responseLength < sizeof(response)) response[responseLength++] = b;
      receivedAnyByte = true;
      lastByteTime = millis();
    }
    if (receivedAnyByte && (millis() - lastByteTime > 10)) break;
    delay(1);
  }

  if (responseLength == 0) { Serial.println("RX: NO RESPONSE"); return false; }

  Serial.print("RX: ");
  printHexBuffer(response, responseLength);

  if (responseLength < 7) return false;

  uint16_t receivedCRC = response[responseLength - 2] |
      (static_cast<uint16_t>(response[responseLength - 1]) << 8);
  uint16_t calculatedCRC = modbusCRC(response, responseLength - 2);
  if (receivedCRC != calculatedCRC) return false;
  if (response[0] != slave || response[1] != 0x03 || response[2] != 0x02) return false;

  value = (static_cast<uint16_t>(response[3]) << 8) | response[4];
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if ((millis() - start) > 30000) return;
  }
  Serial.print("IP address: "); Serial.println(WiFi.localIP());
}

void setupOTA() {
  if (WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();
  Serial.print("Arduino OTA ready as: "); Serial.println(OTA_HOSTNAME);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("======================================");
  Serial.println("EVOHEAT EVO270 MODBUS TEST");
  Serial.println("WAVESHARE ESP32-S3-RS485-CAN");
  Serial.println("======================================");
  Serial.println("RS485 TX : GPIO17");
  Serial.println("RS485 RX : GPIO18");
  Serial.println("RS485 EN : GPIO21");
  Serial.println("Baud     : 9600 8N1");
  Serial.println("Slave    : 99");
  Serial.println("Function : 03");
  Serial.println("Register : 2019 (0x07E3)");

  pinMode(RS485_EN_PIN, OUTPUT);
  rs485ReceiveMode();
  RS485.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  connectWiFi();
  setupOTA();
}

void loop() {
  ArduinoOTA.handle();

  if (millis() - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = millis();
    uint16_t rawValue = 0;
    if (readHoldingRegister(EVO_SLAVE_ADDRESS, AMBIENT_REGISTER, rawValue)) {
      float ambientTemperature = (static_cast<float>(rawValue) - 60.0f) * 0.5f;
      Serial.print("RAW REGISTER VALUE: "); Serial.println(rawValue);
      Serial.print("AMBIENT TEMPERATURE: "); Serial.print(ambientTemperature, 1); Serial.println(" C");
    } else {
      Serial.println("No valid EVO270 Modbus response.");
    }
  }
  delay(10);
}
