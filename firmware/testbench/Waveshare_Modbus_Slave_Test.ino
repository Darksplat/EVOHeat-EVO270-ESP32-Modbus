#include <Arduino.h>

HardwareSerial RS485(1);
constexpr int TX_PIN=17, RX_PIN=18, EN_PIN=21;
constexpr uint8_t SLAVE=1;
constexpr uint16_t REG=0x0001, VALUE=1234;

uint16_t crc16(const uint8_t *d,size_t n){
  uint16_t c=0xFFFF; for(size_t i=0;i<n;i++){c^=d[i];for(int b=0;b<8;b++) c=(c&1)?(c>>1)^0xA001:c>>1;} return c;
}
void rxMode(){digitalWrite(EN_PIN,LOW);}
void txMode(){digitalWrite(EN_PIN,HIGH);}

void setup(){
  Serial.begin(115200); pinMode(EN_PIN,OUTPUT); rxMode();
  RS485.begin(9600,SERIAL_8N1,RX_PIN,TX_PIN);
  Serial.println("WAVESHARE ESP32-S3 MODBUS SLAVE TEST");
  Serial.println("Slave 1 / Register 0x0001 / Value 1234");
}

void loop(){
  static uint8_t f[8]; static size_t n=0; static unsigned long last=0;
  while(RS485.available()){ if(n<sizeof(f))f[n++]=RS485.read(); last=millis(); }
  if(n && millis()-last>10){
    if(n==8 && f[0]==SLAVE && f[1]==0x03 && f[2]==0 && f[3]==1 && f[4]==0 && f[5]==1){
      uint16_t got=f[6]|((uint16_t)f[7]<<8);
      if(got==crc16(f,6)){
        uint8_t r[7]={SLAVE,0x03,0x02,(uint8_t)(VALUE>>8),(uint8_t)VALUE,0,0};
        uint16_t c=crc16(r,5); r[5]=c&0xFF; r[6]=c>>8;
        txMode(); delayMicroseconds(200); RS485.write(r,7); RS485.flush(); delayMicroseconds(200); rxMode();
      }
    }
    n=0;
  }
}
