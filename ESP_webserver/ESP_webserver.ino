#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <string>

#define Yoda Serial
#define Debug Serial1

// jak dlouha musi byt mezera mezi znakama (v ms), aby program vyhodnotil, ze prisel cely string
#define CHARACTER_TIME_DIFF         300

// vypisovani prichozich pozadavku z WIFI cipu
#define SHOW_INCOMMING_REQUESTS     1

// vypisovani prichozich odpovedi z YODY
#define SHOW_INCOMMING_RESPONSES    1

// vypisovani flashovacich informaci
#define SHOW_FLASHING_INFO          1

const char* ssid = "TARA";
const char* password = "TaraNet147";

WiFiServer server(80);

char c = 0;

//String  prichozi_data = "";
char data_from_yoda[2048];
int data_from_yoda_len = 0;

void setup(void){
 
  //pinMode(LED_BUILTIN, OUTPUT);
  //digitalWrite(LED_BUILTIN, HIGH);

  Yoda.begin(115200);
  Debug.begin(115200);

  //prichozi_data.reserve(512);
  
  Debug.print("\n\nBooting\n");
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, password);

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

  server.begin();
  Debug.print("HTTP server started\n");

}

void loop(void){

  // OTA handle nezpomaluje seriovku
  ArduinoOTA.handle();

  // zkontroluje nove spojeni
  WiFiClient client_html = server.available();  

  // pokud neni spojeni, skoci na zacatek
  if (client_html) { 

    // vycisti se seriovka, pokud v ni jeste neco vysi
    /*
    while(Yoda.available()) {
      c = Yoda.read();
    }
    */
  
    // pokud je spojeni, precte si ho
    String request = client_html.readString(); 
  
    //#if SHOW_INCOMMING_REQUESTS
    // posle spojeni do debugovaci seriovky
    //Debug.print("Toto prislo ze serveru**********\n");
    //Debug.print((char*)request.c_str());
    //Debug.print("posem toto prislo**************\n");
    //#endif
    Debug.printf("HTML server poslal %d bytu, preposilam Yodovi\n", request.length());
  
    // posle spojeni yodovi
    Yoda.print('H');
    Yoda.print('0');
    Yoda.print((char*)request.c_str());
  
    // vycisti se prichozi buffer
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
  
    if(data_from_yoda_len){
     
      //Debug.write(data_from_yoda+2, data_from_yoda_len-2);
      //Debug.print("konec odpovedi **********************\n");
      if(data_from_yoda[0]=='H'){
        Debug.printf("Yoda poslal %d bytu za %d ms do HTTP, preposilam serveru\n", data_from_yoda_len-2, millis()-time_begin);
        int odeslano = 0;
        odeslano = client_html.write((uint8_t *)data_from_yoda+2, data_from_yoda_len-2);
      
        if(odeslano!=(data_from_yoda_len-2)){
          Debug.printf("NEJAKE DATA SE NEPREPOSLALY!!!\n");    
        }
      } else {
        Debug.printf("Yoda poslal %d bytu za %d ms do JINAM, nedelam nic\n", data_from_yoda_len-2, millis()-time_begin);
      }
    }    
  } 
}
