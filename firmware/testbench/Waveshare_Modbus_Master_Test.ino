#include <Arduino.h>

HardwareSerial RS485(1);
constexpr int TX_PIN=17, RX_PIN=18, EN_PIN=21;

uint16_t crc16(const uint8_t *d,size_t n){
  uint16_t c=0xFFFF; for(size_t i=0;i<n;i++){c^=d[i];for(int b=0;b<8;b++) c=(c&1)?(c>>1)^0xA001:c>>1;} return c;
}
void rxMode(){digitalWrite(EN_PIN,LOW);}
void txMode(){digitalWrite(EN_PIN,HIGH);}
void ph(const uint8_t*d,size_t n){for(size_t i=0;i<n;i++){if(d[i]<16)Serial.print('0');Serial.print(d[i],HEX);Serial.print(' ');}Serial.println();}

void setup(){
  Serial.begin(115200); pinMode(EN_PIN,OUTPUT); rxMode();
  RS485.begin(9600,SERIAL_8N1,RX_PIN,TX_PIN);
  Serial.println("WAVESHARE ESP32-S3 MODBUS MASTER TEST");
}
void loop(){
  uint8_t q[8]={1,3,0,1,0,1,0,0}; uint16_t c=crc16(q,6);q[6]=c&0xFF;q[7]=c>>8;
  while(RS485.available())RS485.read();
  Serial.print("TX: ");ph(q,8);
  txMode();delayMicroseconds(200);RS485.write(q,8);RS485.flush();delayMicroseconds(200);rxMode();
  uint8_t r[16];size_t n=0;unsigned long s=millis(),lb=s;bool any=false;
  while(millis()-s<1000){while(RS485.available()){if(n<sizeof(r))r[n++]=RS485.read();any=true;lb=millis();}if(any&&millis()-lb>10)break;}
  if(!n)Serial.println("RX: NO RESPONSE"); else {Serial.print("RX: ");ph(r,n);}
  delay(2000);
}
