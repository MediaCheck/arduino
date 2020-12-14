#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

#define Yoda Serial
#define Debug Serial1

const char* ssid = "TARA";
const char* password = "TaraNet147";

char data_from_yoda[2048];
int data_from_yoda_len = 0;

// jak dlouha musi byt mezera mezi znakama (v ms), aby program vyhodnotil, ze prisel cely string
#define CHARACTER_TIME_DIFF         50

int i = 0;

void setup() {
  Yoda.begin(115200);
  Debug.begin(921600);

  Debug.print("\n\nBooting\n");
  Debug.print("Hello from ESP_incomming_parser\n");
  
  WiFi.mode(WIFI_STA);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Debug.print("Connection Failed! Rebooting...\n");
    delay(5000);
    ESP.restart();
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

  Debug.print("Connected to ");
  Debug.println(ssid);
  Debug.print("IP address: ");
  Debug.println(WiFi.localIP());

}

void loop() {
  
  // OTA handle nezpomaluje seriovku
  ArduinoOTA.handle();

  data_from_yoda_len = 0;

  unsigned long time_last = 0;
  unsigned long time_diff = 0;
  unsigned long time_begin = 0;

  time_last = millis();
  time_begin = time_last;

  int cnt = 0;
  //i = 0;
  
  while(time_diff<CHARACTER_TIME_DIFF){
  
    while(Yoda.available()){
      time_last = millis();
      data_from_yoda[data_from_yoda_len] = Yoda.read();
      data_from_yoda_len++;
    }
    
    time_diff = millis() - time_last;
  }

  if(data_from_yoda_len){
    data_from_yoda[data_from_yoda_len] = 0x00;
    
    switch(data_from_yoda[0]){
      case 'C':
        Debug.print("CMND");
        break;
      case 'H':
        Debug.print("HTTP");
        break;
      case 'M':
        Debug.print("MQTT");
        break;
      default:
         Debug.print("????");
    }

    Debug.printf(" (CRC=0x%02X)", data_from_yoda[1]);
    Debug.printf(" len=%d\n", data_from_yoda_len-2);
    
  }

}
