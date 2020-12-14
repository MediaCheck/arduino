#include <SoftwareSerial.h>
// software serial #1: TX = digital pin 10, RX = digital pin 11
SoftwareSerial portOne(4, 5);

void setup()
{
  portOne.begin(115200);
  Serial.begin(115200);
  Serial.println("Hello, world from Arduino");
}

void loop() // run over and over
{
  if (portOne.available())
    Serial.write(portOne.read());
  if (Serial.available())
    portOne.write(Serial.read());
    //delayMicroseconds(100);
    delay(1);
}

