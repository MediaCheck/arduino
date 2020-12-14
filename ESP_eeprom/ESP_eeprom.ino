#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>

#define Yoda Serial
#define Debug Serial1

const char* ssid = "TARA";
const char* password = "TaraNet147";

const char* host = "192.168.65.179";
const int httpPort = 1881;

#define NUMBER_OF_TESTBYTES 4096

void setup(){
  Yoda.begin(921600);
  Debug.begin(921600);

  Debug.println();
  Debug.println();
  Debug.print("Connecting to ");
  Debug.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Debug.print(".");
  }

  ArduinoOTA.setHostname("byzance-esp");

  // No authentication by default
  //ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Debug.println("Start update\n");
  });
  
  ArduinoOTA.onEnd([]() {
    Debug.println("\nEnd update\n");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Debug.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Debug.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Debug.print("Auth Failed\n");
    else if (error == OTA_BEGIN_ERROR) Debug.print("Begin Failed\n");
    else if (error == OTA_CONNECT_ERROR) Debug.print("Connect Failed\n");
    else if (error == OTA_RECEIVE_ERROR) Debug.print("Receive Failed\n");
    else if (error == OTA_END_ERROR) Debug.print("End Failed\n");
  });
  
  ArduinoOTA.begin();

  Debug.println("");
  Debug.println("WiFi connected");  
  Debug.println("IP address: ");
  Debug.println(WiFi.localIP());

  EEPROM.begin(NUMBER_OF_TESTBYTES);
  Debug.println("EEPROM ready\n");  

  uint32_t i;
  byte val;
  int addr;

  for(addr=0; addr<NUMBER_OF_TESTBYTES; addr++){
    val = addr;
    EEPROM.write(addr, val);
  }

  EEPROM.commit();
  
  Debug.print("Data vygenerovana a zapsana\n");
  
  for(addr=0; addr<NUMBER_OF_TESTBYTES; addr++){
    val = EEPROM.read(addr);
    if((addr%256)!=val){
      Debug.printf("error: addr = %d, val = %d\n", addr, val);
    }
  }

  Debug.print("Konec cteni; pokud vyse nejsou chyby, vse je OK\n");
  
}

void loop(){
    ArduinoOTA.handle();
}
