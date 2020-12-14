#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

#define Yoda Serial
#define Debug Serial1

const char* ssid = "TARA";
const char* password = "TaraNet147";

void setup() {
  Yoda.begin(115200);
  Debug.begin(921600);

  Debug.print("\n\nBooting\n");
  Debug.print("Hello from ESP_bridge\n");
  
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

  // vsechno co prijde z Yody se pouze preposle do debug seriovky
  while(Yoda.available()) {
    Debug.write(Yoda.read());
  }

}
