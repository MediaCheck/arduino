#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

#define Yoda Serial
#define Debug Serial1

const char* ssid = "TARA";
const char* password = "TaraNet147";

const char* host = "192.168.65.179";
const int httpPort = 1881;

// jak dlouha musi byt mezera mezi znakama (v ms), aby program vyhodnotil, ze prisel cely string
#define CHARACTER_TIME_DIFF         100

char c = 0;

//String  prichozi_data = "";

char data_from_yoda[2048];
int data_from_yoda_len = 0;

int i = 0;

String  receive_string = "";

void setup() {
  Yoda.begin(115200);
  Debug.begin(921600);

//  prichozi_data.reserve(512);
  receive_string.reserve(512);
  
  delay(10);

  // We start by connecting to a WiFi network

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
}

void loop() {

  pripojit_wifi:
  
  // OTA handle nezpomaluje seriovku
  ArduinoOTA.handle();

  Debug.print("connecting to ");
  Debug.println(host);
  
  // Use WiFiClient class to create TCP connections
  WiFiClient client;

  if (!client.connect(host, httpPort)) {
    Debug.println("connection failed");
    return;
  }

  prijem_dat:

  // OTA handle nezpomaluje seriovku
  ArduinoOTA.handle();

/*
 * PRIJEM DAT Z YODY
 */

  data_from_yoda_len = 0;

  unsigned long time_last = 0;
  unsigned long time_diff = 0;
  unsigned long time_begin = 0;

  time_last = millis();
  time_begin = time_last;
  
  while(time_diff<CHARACTER_TIME_DIFF){
  
    while(Yoda.available()){
      time_last = millis();
      data_from_yoda[data_from_yoda_len] = Yoda.read();
      data_from_yoda_len++;
    }
    
    time_diff = millis() - time_last;
  }

  // OTA handle nezpomaluje seriovku
  ArduinoOTA.handle();  

  if(data_from_yoda_len){
    Debug.printf("z yody prislo %d znaku za %d ms, preposilam do serveru\n", data_from_yoda_len-2, (millis()-time_begin));    
    int odeslano = 0;
    odeslano = client.write((uint8_t *)data_from_yoda+2, data_from_yoda_len-2);
    if(odeslano!=(data_from_yoda_len-2)){
      Debug.printf("NEJAKE DATA SE NEPREPOSLALY!!!\n");    
    }
  }

  /*
   * PRIJEM DAT SE SERVERU
   */
  receive_string = "";

  // Read all the lines of the reply from server and print them to Serial
  while(client.available()){
    c = client.read();
    receive_string+=c;
  }
  
  if(receive_string.length()){
    Debug.printf("ze serveru prislo %d znaku, preposilam yodovi\n", receive_string.length());
    Yoda.print('M');
    Yoda.print('0');
    Yoda.write((uint8_t *)receive_string.c_str(), receive_string.length());
    receive_string = "";
  }

  goto prijem_dat;

}

