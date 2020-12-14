#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <string>

#define Yoda Serial
#define Debug Serial1

#define MQTT_PACKET_CONNECT     0b00010000
#define MQTT_PACKET_CONNACK     0b00100000
#define MQTT_PACKET_PUBLISH     0b00110000
#define MQTT_PACKET_PUBACK      0b01000000
#define MQTT_PACKET_PUBREC      0b01010000
#define MQTT_PACKET_PUBREL      0b01100000
#define MQTT_PACKET_PUBCOMP     0b01110000
#define MQTT_PACKET_SUBSCRIBE   0b10000000
#define MQTT_PACKET_SUBACK      0b10010000
#define MQTT_PACKET_UNSUBSCRIBE 0b10100000
#define MQTT_PACKET_UNSUBACK    0b10110000
#define MQTT_PACKET_PINGREQ     0b11000000
#define MQTT_PACKET_PINGRESP    0b11010000
#define MQTT_PACKET_DISCONNECT  0b11100000

#define WIFI_LOG(...);      Debug.printf(__VA_ARGS__);
#define WIFI_ERROR(...);    Debug.printf(__VA_ARGS__);
#define WIFI_WARNING(...);  Debug.printf(__VA_ARGS__);
#define WIFI_INFO(...);     Debug.printf(__VA_ARGS__);
#define WIFI_DEBUG(...);    Debug.printf(__VA_ARGS__);
#define WIFI_TRACE(...);    Debug.printf(__VA_ARGS__);

/*
   Hlavičky funkcí
*/
bool write_to_eeprom(int addr, int len, void* data);
bool read_from_eeprom(int addr, int len, void* data);

// koresponduje s definici v ESP82566.h v Yodovi
typedef enum
{
  SOCKET_UNKNOWN  = (char)0x00,
  SOCKET_COMMAND  = (char)'C',
  SOCKET_HTTP     = (char)'H',
  SOCKET_MQTT     = (char)'M',
  SOCKET_ACK      = (char)'A'
} socket_t;

#define FIRMWARE_PREFIX             (char)'W'
#define FIRMWARE_MAJOR              1
#define FIRMWARE_MINOR              2
#define FIRMWARE_SUBMINOR           4

// jak dlouha musi byt mezera mezi znakama (v ms), aby program vyhodnotil, ze prisel cely string
#define CHARACTER_TIME_DIFF_INCOMMING         10
#define CHARACTER_TIME_DIFF_OUTCOMMING        10

// pri baud 115200 staci 200ms (cca 191ms)
#define ACK_TIMEOUT                           200

// jak dlouho se ceka na HTML odpoved od Yody (v ms)
#define HTML_TIMEOUT                200

// kolik bytu se maximalne zapise do EEPROM, 0-4096
#define EEPROM_MAX_BYTES            1024

/*
   MAPA EEPROM PAMETI
*/
#define EEPROM_OFFSET_APMODE              0
#define EEPROM_SIZEOF_APMODE              4

#define EEPROM_OFFSET_CREDENTIALS_CHANGED 4
#define EEPROM_SIZEOF_CREDENTIALS_CHANGED 4

// od adresy 4 do adresy 8 zatim neni nic

#define EEPROM_OFFSET_FULLID              8
#define EEPROM_SIZEOF_FULLID              32 // 24+\0+rezerva

// od adresy 40 do adresy 64 zatim neni nic

#define EEPROM_OFFSET_WIFI_SSID           64
#define EEPROM_SIZEOF_WIFI_SSID           32

#define EEPROM_OFFSET_WIFI_PASS           96
#define EEPROM_SIZEOF_WIFI_PASS           64

// od adresy 160 do adresy 224 zatim neni nic

#define EEPROM_OFFSET_BROKER_HOSTNAME     224
#define EEPROM_SIZEOF_BROKER_HOSTNAME     64

#define EEPROM_OFFSET_BROKER_PORT         288
#define EEPROM_SIZEOF_BROKER_PORT         4

#define EEPROM_OFFSET_HTTP_PORT           292
#define EEPROM_SIZEOF_HTTP_PORT           4

// TODO: pri zapsani si funkce prvni precte, jestli uz tam nahodou neni zapsano to same
// aby se pripadne neprepisovaly porad udaje
bool write_to_eeprom(int addr, int len, void* data) {
  int i = 0;
  int cnt = 0;

  char tmp_data[len];

  read_from_eeprom(addr, len, tmp_data);

  if (strncmp((char*)data, tmp_data, len) == 0) {
    //Debug.printf("nezapisuju do flashky neco, co tam uz je\n");
  } else {

    //Debug.printf("zapisuju do flashky neco\n");

    for (i = 0; i < len; i++) {
      EEPROM.write(addr + i, *((uint8_t*)(data + cnt)));
      //Debug.printf("%d - %c\n", i, data[i]);
      cnt++;
    }

    EEPROM.commit();
  }
}

bool read_from_eeprom(int addr, int len, void* data) {

  int i = 0;
  int cnt = 0;

  for (i = 0; i < len; i++) {
    *((uint8_t*)(data + cnt)) = EEPROM.read(addr + i);
    cnt++;
  }

}

WiFiServer        *server_http;
ESP8266WebServer  *server_ap;

WiFiClient client_mqtt;
WiFiClient client_http;

// jak dlouho to trvalo, nez prisla HTML odpoved z Yody
unsigned long html_time = 0;

char c = 0;

char          data_from_yoda[4096];
char*         data_from_yoda_ptr;
int           data_from_yoda_len = 0;
socket_t      data_from_yoda_socket = SOCKET_UNKNOWN;
unsigned long data_from_yoda_duration = 0;

char          cmd_buffer[256];

char          tmp_buffer[256];

bool          wifi_enabled = 0;
bool          mqtt_enabled = 0;
bool          http_enabled = 0;

bool          mqtt_connected = 0;
bool          http_running = 0;

uint32_t      tmp32;
uint32_t      apmode;

uint32_t      cnt_outcomming = 0;

char module_name[32];

// pred kolika MS se client zkousel naposledy pripojit
unsigned long time_connect_last = 0;

// pokud se uspesne odesle nejaka zprava, do teto promenne se nahraje odpovadajici casove razitko
unsigned long last_sent_msg_timestamp = 0;

char eeprom_write[100];
char eeprom_read[100];

/*
   Pri kazdem startu se zkontroluje, jestli je wifi prazdna a pokud jo, nastavi se do ni defaultni hodnoty
*/
void set_defaults() {

  read_from_eeprom(EEPROM_OFFSET_APMODE, EEPROM_SIZEOF_APMODE, &apmode);
  if (apmode == 0xFF) {
    Debug.printf("* DEFAULT: APmode is unknown, setting to 1\n");
    apmode = 1;
    write_to_eeprom(EEPROM_OFFSET_APMODE, EEPROM_SIZEOF_APMODE, &apmode);
  }

}


void setup() {

  Yoda.begin(921600);
  Debug.begin(921600);

  WiFi.setAutoConnect(false);

  EEPROM.begin(EEPROM_MAX_BYTES);

  Debug.print("\n\nINFO: Booting\n");

  set_defaults();

  //apmode = 0;

  Debug.print("*************************\n");
  if (apmode) {
    Debug.printf("* REZIM AP\n");
  } else {
    Debug.printf("* REZIM CLIENT\n");
  }
  Debug.print("*************************\n");

  char tmp_fullid[EEPROM_SIZEOF_FULLID];

  // musi se nacist vzdycky
  read_from_eeprom(EEPROM_OFFSET_FULLID, EEPROM_SIZEOF_FULLID, tmp_fullid);
  Debug.printf("* INFO: ESP FULLID=%s\n", tmp_fullid);

  if (!apmode) {
    pinMode(0, INPUT);

    Debug.print("**** Pocatecni konfigurace ****\n");

    char tmp_ssid[EEPROM_SIZEOF_WIFI_SSID];
    char tmp_pass[EEPROM_SIZEOF_WIFI_PASS];

    char tmp_mqtt_host[EEPROM_SIZEOF_BROKER_HOSTNAME];
    uint32_t tmp_mqtt_port = 0;

    uint32_t tmp_http_port = 0;

    read_from_eeprom(EEPROM_OFFSET_WIFI_SSID, EEPROM_SIZEOF_WIFI_SSID, tmp_ssid);
    Debug.printf("* INFO: WIFI SSID=%s\n", tmp_ssid);

    read_from_eeprom(EEPROM_OFFSET_WIFI_PASS, EEPROM_SIZEOF_WIFI_PASS, tmp_pass);
    Debug.printf("* INFO: WIFI PASS=%s\n", tmp_pass);

    read_from_eeprom(EEPROM_OFFSET_BROKER_HOSTNAME, EEPROM_SIZEOF_BROKER_HOSTNAME, tmp_mqtt_host);
    Debug.printf("* INFO: MQTT HOSTNAME=%s\n", tmp_mqtt_host);

    read_from_eeprom(EEPROM_OFFSET_BROKER_PORT, EEPROM_SIZEOF_BROKER_PORT, &tmp_mqtt_port);
    Debug.printf("* INFO: MQTT PORT=%d\n", tmp_mqtt_port);

    read_from_eeprom(EEPROM_OFFSET_HTTP_PORT, EEPROM_SIZEOF_HTTP_PORT, &tmp_http_port);
    Debug.printf("* INFO: HTTP PORT=%d\n", tmp_http_port);

    Debug.print("**** KONEC ****\n");
    
  } else {

    server_ap = new ESP8266WebServer(80);

    char tmp_fullid[EEPROM_SIZEOF_FULLID];

    // musi se nacist vzdycky
    read_from_eeprom(EEPROM_OFFSET_FULLID, EEPROM_SIZEOF_FULLID, tmp_fullid);

    sprintf(module_name, "yoda_%s", tmp_fullid);
    WIFI_INFO("Starting AP = %s\n", module_name);
    /* You can remove the password parameter if you want the AP to be open. */
    WiFi.softAP(module_name);

    IPAddress myIP = WiFi.softAPIP();
    Debug.print("AP IP address: ");
    Debug.println(myIP);
    server_ap->on("/", handleRoot);
    server_ap->on("/manual_credentials", handleManualCredentials);
    server_ap->on("/scan_networks", handleScanNetworks);
    server_ap->on("/save_credentials", handleCredentials);
    server_ap->on("/restart", handleRestart);
    server_ap->onNotFound(handleError);
    server_ap->begin();
    Debug.println("AP + HTTP server started");
  }

  //sprintf(module_name, "yoda_%s", esp_fullid);
  sprintf(module_name, "BORYS_ESP");
  ArduinoOTA.setHostname(module_name);
  Debug.printf("OTA NAME set to = %s\n", module_name);
  

  // No authentication by default
  //ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Debug.println("FLSH: Start update\n");

    sprintf(cmd_buffer, "FLASH/STATE:RUNNING");
    send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

  });

  ArduinoOTA.onEnd([]() {
    Debug.println("\nFLSH: End update\n");

    sprintf(cmd_buffer, "FLASH/STATE:END");
    send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Debug.printf("FLSH: Progress: %u%%\r", (progress / (total / 100)));
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
  Debug.printf("INFO: OTA nastartovano\n");

}



void receive_from_yoda(void) {

  data_from_yoda_len            = 0;
  data_from_yoda_duration       = 0;
  data_from_yoda_socket         = SOCKET_UNKNOWN;

  if (Yoda.available()) {
    unsigned long time_last = 0;
    unsigned long time_diff = 0;
    unsigned long time_begin = 0; // vyuziva se jako vystup z funkce

    time_last = millis();
    time_begin = time_last;

    while (time_diff < CHARACTER_TIME_DIFF_INCOMMING) {

      while (Yoda.available()) {
        time_last = millis();
        data_from_yoda[data_from_yoda_len] = Yoda.read();
        data_from_yoda_len++;
      }

      time_diff = millis() - time_last;
    }

    if (data_from_yoda_len > 8) {
      data_from_yoda_socket = (socket_t)data_from_yoda[0];

      Debug.printf("receive_from_yoda: received %d characters, socket=%c\n", data_from_yoda_len, data_from_yoda_socket);

      uint16_t crc_expected = 0;
      uint16_t crc_received = 0;

      crc_expected  = data_from_yoda[2] << 8;
      crc_expected |= data_from_yoda[3];

      int i = 0;

      for (i = 0; i < (data_from_yoda_len - 8); i++) {
        crc_received ^= data_from_yoda[i + 8];
      }

      if (crc_received != crc_expected) {
        Debug.printf("receive_from_yoda: NESOUHLASI CRC (received=0x%08X, expected=0x%08X)\n", crc_received, crc_expected);
        data_from_yoda_len = 0;
        data_from_yoda_socket = SOCKET_UNKNOWN;
        data_from_yoda_duration = 0;
        return;
      }

      uint16_t len_expected = 0;
      uint16_t counter_received = 0;

      len_expected  = data_from_yoda[4] << 8;
      len_expected |= data_from_yoda[5];

      counter_received  = data_from_yoda[6] << 8;
      counter_received |= data_from_yoda[7];

      data_from_yoda[data_from_yoda_len] = 0x00; // prida za data terminacni nulu, kdyby byl nahodou potreba sprintf

      // pokud je prijaty socket ACK, nedela se uz nic
      if (data_from_yoda_socket == SOCKET_ACK) {

        /*
          char buffer_ack[16];
          strncpy(buffer_ack, data_from_yoda+8, data_from_yoda_len-8);
          strncpy(data_from_yoda_len
        */
        Debug.printf("receive_from_yoda: prijmul jsem ACK na zpravu cislo %s\n", data_from_yoda + 8);

        data_from_yoda_len -= 8;
       // data_from_yoda_socket = SOCKET_ACK;
        data_from_yoda_ptr  = data_from_yoda + 8;
        data_from_yoda_duration = millis() - time_begin;

      } else {

        // pošle ihned ACK o přijatém packetu
        char buffer_ack[16];
        sprintf(buffer_ack, "%d", counter_received);
        Debug.printf("receive_from_yoda: prijimam data cislo %s\n", buffer_ack);
        send_to_yoda(SOCKET_ACK, buffer_ack, strlen(buffer_ack));

        // useknu delku dat o 8
        data_from_yoda_len -= 8;
        data_from_yoda_ptr  = data_from_yoda + 8;

        // zmerim, kolik trval prijem dat
        data_from_yoda_duration = millis() - time_begin;

        Debug.printf("receive_from_yoda: data cislo %s prijata, konec fce\n", buffer_ack);

      }

      // data maji malou delku
    } else {
      Debug.printf("receive_from_yoda: received %d characters, zahozeno\n", data_from_yoda_len);
      data_from_yoda_len = 0;
      data_from_yoda_socket = SOCKET_UNKNOWN;
      data_from_yoda_duration = 0;
    }
  }
}

/*
   Posle data do Yody
   TODO: dokumentace
*/
void send_to_yoda(socket_t socket, char* data, int len) {

  unsigned long time_diff;
  time_diff = millis() - last_sent_msg_timestamp;

  if (time_diff < CHARACTER_TIME_DIFF_OUTCOMMING) {
    delay(CHARACTER_TIME_DIFF_OUTCOMMING - time_diff);
  }

  //int randNumber = random(10, 20);
  //delay(randNumber);

  switch (socket) {
    case SOCKET_COMMAND:
      Yoda.print('C');
      break;
    case SOCKET_HTTP:
      Yoda.print('H');
      break;
    case SOCKET_MQTT:
      Yoda.print('M');
      break;
    case SOCKET_ACK:
      Yoda.print('A');
      break;
    default:
      return;
  }

  // dummy
  Yoda.print('0');

  // CRC
  uint16_t crc = 0;
  int i = 0;

  for (i = 0; i < len; i++) {
    crc ^= data[i];
  }

  // CRC
  Yoda.write((uint8_t)(crc >> 8) & 0xFF);
  Yoda.write((uint8_t)(crc & 0xFF));

  // LEN
  Yoda.write((uint8_t)(len >> 8) & 0xFF);
  Yoda.write((uint8_t)(len & 0xFF));

  // outcomming counter
  Yoda.write((uint8_t)(cnt_outcomming >> 8) & 0xFF);
  Yoda.write((uint8_t)(cnt_outcomming & 0xFF));

  if (socket == SOCKET_ACK) {
    Debug.printf("send_to_yoda: odesilam ACK na zpravu = %s\n", data);
  } else {
    Debug.printf("send_to_yoda: odesilam data s cislem = %d\n", cnt_outcomming);
    cnt_outcomming++;
  }
  
  // poslu teda ty data
  Yoda.write((uint8_t *)data, len);  

  /*
   * Kdyz posilam data, cekam na acknowledge
   */
  if(socket != SOCKET_ACK){

    unsigned long ack_time_begin = 0; // vyuziva se jako vystup z funkce
    ack_time_begin = millis();
    
    int ack_received = 0;

    Debug.printf("send_to_yoda: cekam na ack\n");
    while(ack_received==0){
      receive_from_yoda();
      
      if (data_from_yoda_len) {
        if(data_from_yoda_socket==SOCKET_ACK){
          // TODO: overit spravnost ACK
          Debug.printf("send_to_yoda: prisel ack za %d ms\n", millis()-ack_time_begin);
          ack_received = 1;
        }
      } else {
        //Debug.printf("send_to_yoda: ACK NEPRISEL, zkusim to znova, jeste mam cas\n");
      }

      if((millis()-ack_time_begin)>ACK_TIMEOUT){
        Debug.printf("send_to_yoda: ACK NEPRISEL, vyprsel timeout\n");
        return;
      }
      
    }  
    //Debug.printf("send_to_yoda: ACK PRISEL, konec fce\n");
    
  } else {
      Debug.printf("send_to_yoda: na prijeti ack necekam, konec fce\n");
  }

  last_sent_msg_timestamp = millis();
}

/***************************************************
  /***************************************************
  /***************************************************
            MAIN LOOP
  /***************************************************
  /***************************************************
***************************************************/
void loop() {

  // OTA handle, musi se cpat vsude kam to jde
  // pokud se na nej obcas nedostane rada, nepojede wifi update
  ArduinoOTA.handle();

  // pri behu programu na chvilku prepnu GPIO_2 na 0
  // ESP si ulozi flag o tom, ze se ma prepnout do AP rezimu a restartuje se
  unsigned long pin_counter = millis();
  
  while (!digitalRead(0)) {
    if ((millis() - pin_counter) > 1000) {
      read_from_eeprom(EEPROM_OFFSET_APMODE, EEPROM_SIZEOF_APMODE, &apmode);
      if(apmode){
        Debug.printf("AP mode is ON, turning OFF!!!\n");
        apmode = 0;
      } else {
        Debug.printf("AP mode is OFF, turning ON!!!\n");
        apmode = 1;
      }
      write_to_eeprom(EEPROM_OFFSET_APMODE, EEPROM_SIZEOF_APMODE, &apmode);
      delay(1000);
      ESP.restart();
    }
  }

  /*
     CLIENT mode
  */
  if (!apmode) {

    /*
     * Connect wifi
     */
    if(wifi_enabled){
      if(!WiFi.isConnected()){
        connect_wifi();
      }
    }

    // handle MQTT client
    if (mqtt_enabled){
      if(client_mqtt.connected()) {
        mqtt_connected = 1;
        mqtt_receive(); 
      } else {
        mqtt_connected = 0;
        mqtt_connect();  
      }  
    }

    // handle HTTP server
    if (http_enabled) {
      if(http_running){
        handle_http();
      } else {
        start_http();
      }
    }

    /*
       ZKONTROLUJE, CO PRISLO OD YODY
    */

    receive_from_yoda();

    if (data_from_yoda_len) {
      process_command();
    }

    /*
       AP mode
    */
  } else {
    server_ap->handleClient();
  }
}

void mqtt_connect() {

  mqtt_connected = 0;
  
  unsigned long time_connect_begin = millis();
  
  if (((time_connect_begin - time_connect_last) > 30000) || (time_connect_last == 0)) {
  
    time_connect_last = millis();
  
    Debug.print("MQTT: new attempt to MQTT connection\n");
  
    sprintf(cmd_buffer, "MQTT/SERVICE:DISCONNECTED");
    send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

    char tmp_mqtt_host[EEPROM_SIZEOF_BROKER_HOSTNAME];
    uint32_t tmp_mqtt_port;
  
    read_from_eeprom(EEPROM_OFFSET_BROKER_HOSTNAME, EEPROM_SIZEOF_BROKER_HOSTNAME, tmp_mqtt_host);
    read_from_eeprom(EEPROM_OFFSET_BROKER_PORT, EEPROM_SIZEOF_BROKER_PORT, &tmp_mqtt_port);
  
    // nejsem pripojeny, zkusim se pripojit
    Debug.printf("MQTT: connecting to host: %s, port = %d\n", tmp_mqtt_host, tmp_mqtt_port);
  
    if (client_mqtt.connect(tmp_mqtt_host, tmp_mqtt_port)) {
      Debug.print("MQTT: connection established\n");
  
      sprintf(cmd_buffer, "MQTT/SERVICE:CONNECTED");
      send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
  
      mqtt_connected = 1;
      
    } else {
      Debug.print("MQTT: connection attempt failed\n");
  
      sprintf(cmd_buffer, "MQTT/SERVICE:DISCONNECTED");
      send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
  
      mqtt_connected = 0;
    }
  }
}

void mqtt_receive(){
  String request = "";
  
  // jsem pripojeny k MQTT clientu, zkusim se zeptat, jestli nejsou data
  while (client_mqtt.available()) {
    c = client_mqtt.read();
    request += c;
  }
  
  // neco skutecne prislo z MQTT
  // predam to yodovi
  if (request.length()) {
    Debug.printf("MQTT->YODA: %4d bytes ", request.length());
    // just send msg type to terminal
    parse_mqtt_msg(request.charAt(0));
    Debug.printf("\n");    
    // send msg to yoda
    send_to_yoda(SOCKET_MQTT, (char*)request.c_str(), request.length());
  }
}

void start_http(){  
  
  uint32_t tmp_http_port = 0;
  read_from_eeprom(EEPROM_OFFSET_HTTP_PORT, EEPROM_SIZEOF_HTTP_PORT, &tmp_http_port);
  Debug.printf("INFO: HTTP PORT=%d\n", tmp_http_port);
  
  server_http = new WiFiServer(tmp_http_port);
  server_http->begin();
  
  Debug.printf("HTTP: server started at port %d\n", tmp_http_port);
  
  // netuším, k čemu to je
  client_mqtt.setNoDelay(false);

  sprintf(cmd_buffer, "HTTP/SERVICE:RUNNING");
  send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

  http_running = 1;
  
}

void handle_http() {
  
  client_http = server_http->available();
  if (client_http) {
    // pokud je nejaky http request, precte si ho a posle dal
    String request = client_http.readString();
    if (request.length()) {
      Debug.printf("HTTP->YODA: %4d bytes\n", request.length());
      send_to_yoda(SOCKET_HTTP, (char*)request.c_str(), request.length());

      unsigned long html_begin = 0;
      html_time = 0;
      html_begin = millis();

      bool html_timeout = 0;

      // pocka na odpoved z Yody
      while (!Yoda.available()) {
        html_time = millis() - html_begin;
        if (html_time > HTML_TIMEOUT) {
          html_timeout = 1;
          break;
        }
      }

      if (html_timeout) {
        Debug.printf("HTTP: response timeout (>%d ms)\n", HTML_TIMEOUT);
      } else {
        Debug.printf("HTTP: response arrived in %d ms\n", html_time);
      }
    }
  }
}

void connect_wifi(){
  int rc;

  rc = WiFi.mode(WIFI_STA);

  // tu se nacte ssid a pass z flashky

  char tmp_ssid[EEPROM_SIZEOF_WIFI_SSID];
  char tmp_pass[EEPROM_SIZEOF_WIFI_PASS];

  // read credentials
  read_from_eeprom(EEPROM_OFFSET_WIFI_SSID, EEPROM_SIZEOF_WIFI_SSID, tmp_ssid);
  read_from_eeprom(EEPROM_OFFSET_WIFI_PASS, EEPROM_SIZEOF_WIFI_PASS, tmp_pass);

  Debug.printf("WIFI: connecting to %s (%s)\n", tmp_ssid, tmp_pass);

  rc = WiFi.begin(tmp_ssid, tmp_pass);
  Debug.printf("WIFI: connect rc = ");
  switch(rc){
    case WL_NO_SHIELD:
      Debug.printf("WL_NO_SHIELD\n");
      break;
    case WL_IDLE_STATUS:
      Debug.printf("WL_IDLE_STATUS\n");
      break;
    case WL_NO_SSID_AVAIL:
      Debug.printf("WL_NO_SSID_AVAIL\n");
      break;
    case WL_SCAN_COMPLETED:
      Debug.printf("WL_SCAN_COMPLETED\n");
      break;
    case WL_CONNECTED:
      Debug.printf("WL_CONNECTED\n");
      break;
    case WL_CONNECT_FAILED:
      Debug.printf("WL_CONNECT_FAILED\n");
      break;
    case WL_CONNECTION_LOST:
      Debug.printf("WL_CONNECTION_LOST\n");
      break;
    case WL_DISCONNECTED:
      Debug.printf("WL_DISCONNECTED\n");
      break;
    default:
      Debug.printf("UNKNOWN\n");
  }

  unsigned long time_connection_start = 0;
  time_connection_start = millis();

  while(WiFi.isConnected()==0){
    ArduinoOTA.handle();
    Debug.printf("WIFI: waiting to connect\n");
    delay(100);

    // cekam 10s na pripojeni, jinak vyskocim
    if((millis()-time_connection_start)>10000){

      wifi_enabled = 0;
      
      // wifi se nepodarilo pripojit
      Debug.print("WIFI: >>>>>> connection unsuccessful\n");
      
      sprintf(cmd_buffer, "WIFI/CONNECTION:UNSUCCESSFUL");
      send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
      return;
    }
  }

  Debug.print("WIFI: >>>>>> connected\n");
  //Debug.printf("INFO: WIFI IP address: %s\n", WiFi.localIP().toString().c_str());

  // wifi je pripojena
  sprintf(cmd_buffer, "WIFI/STATE:CONNECTED");
  send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
}

void process_command() {
  switch (data_from_yoda_socket) {

    case SOCKET_ACK:
      Debug.printf("ACK = %s\n", data_from_yoda_ptr);
      break;

    case SOCKET_HTTP:
      Debug.printf("YODA->HTTP: %4d bytes in %d ms\n", data_from_yoda_len, data_from_yoda_duration);
      client_http.write((uint8_t *)data_from_yoda_ptr, data_from_yoda_len);
      break;

    case SOCKET_MQTT:
      if(mqtt_connected){
        Debug.printf("YODA->MQTT: %4d bytes in %d ms ", data_from_yoda_len, data_from_yoda_duration);
        parse_mqtt_msg(data_from_yoda_ptr[0]);
        Debug.printf("\n");
        client_mqtt.write((uint8_t *)data_from_yoda_ptr, data_from_yoda_len);
      } else {
        Debug.printf("YODA->MQTT: IGNORING DATA, mqtt is not connected\n");
      }
      break;

    case SOCKET_COMMAND:

      Debug.printf("YODA->CMND: %4d bytes in %d ms, data = %s\n", data_from_yoda_len, data_from_yoda_duration, data_from_yoda_ptr);

      // cteni stavu ESP
      if (strncmp(data_from_yoda_ptr, "ESP/STATE", data_from_yoda_len) == 0) {

        sprintf(cmd_buffer, "ESP/STATE:RUNNING");
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // cteni firmware
      } else if (strncmp(data_from_yoda_ptr, "ESP/FIRMWARE", data_from_yoda_len) == 0) {

        sprintf(cmd_buffer, "ESP/FIRMWARE:%02X.%02X.%02X.%02X", FIRMWARE_PREFIX, FIRMWARE_MAJOR, FIRMWARE_MINOR, FIRMWARE_SUBMINOR);
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // cteni MAC adresy
      } else if (strncmp(data_from_yoda_ptr, "ESP/MAC", data_from_yoda_len) == 0) {

        byte mac[6];
        WiFi.macAddress(mac);

        sprintf(cmd_buffer, "ESP/MAC:%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // cteni espid
      } else if (strncmp(data_from_yoda_ptr, "ESP/ESPID", data_from_yoda_len) == 0) {

        sprintf(cmd_buffer, "ESP/ESPID:%d", ESP.getChipId());
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // cteni flashid
      } else if (strncmp(data_from_yoda_ptr, "ESP/FLASHID", data_from_yoda_len) == 0) {

        sprintf(cmd_buffer, "ESP/FLASHID:%d", ESP.getFlashChipId());
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // cteni flashsize
      } else if (strncmp(data_from_yoda_ptr, "ESP/FLASHSIZE", data_from_yoda_len) == 0) {

        sprintf(cmd_buffer, "ESP/FLASHSIZE:%d", ESP.getFlashChipSize());
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // cteni flashspeed
      } else if (strncmp(data_from_yoda_ptr, "ESP/FLASHSPEED", data_from_yoda_len) == 0) {

        sprintf(cmd_buffer, "ESP/FLASHSPEED:%d", ESP.getFlashChipSpeed());
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // cteni fullid
      } else if (strncmp(data_from_yoda_ptr, "ESP/FULLID", data_from_yoda_len) == 0) {

        read_from_eeprom(EEPROM_OFFSET_FULLID, EEPROM_SIZEOF_FULLID, eeprom_read);
        sprintf(cmd_buffer, "ESP/FULLID:%s", eeprom_read);
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // zapis fullid
      } else if (strncmp(data_from_yoda_ptr, "ESP/FULLID:", 11) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 11, data_from_yoda_len - 11);
        tmp_buffer[data_from_yoda_len - 11] = 0x00; // terminacni nula
        write_to_eeprom(EEPROM_OFFSET_FULLID, strlen(tmp_buffer) + 1, tmp_buffer);
        Debug.printf("CMD: Setting FULL_ID = %s\n", tmp_buffer);

        // odesle potvrzeni Yodovi
        sprintf(cmd_buffer, "ESP/FULLID:%s", tmp_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // zapis MQTT hostname
      } else if (strncmp(data_from_yoda_ptr, "MQTT/HOSTNAME:", 14) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 14, data_from_yoda_len - 14);
        tmp_buffer[data_from_yoda_len - 14] = 0x00; // terminacni nula
        write_to_eeprom(EEPROM_OFFSET_BROKER_HOSTNAME, strlen(tmp_buffer) + 1, tmp_buffer);
        Debug.printf("CMD: Setting MQTT HOSTNAME = %s\n", tmp_buffer);

        // odesle potvrzeni Yodovi
        sprintf(cmd_buffer, "MQTT/HOSTNAME:%s", tmp_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // zapis MQTT portu
      } else if (strncmp(data_from_yoda_ptr, "MQTT/PORT:", 10) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 10, data_from_yoda_len - 10);
        tmp_buffer[data_from_yoda_len - 10] = 0x00; // terminacni nula
        // převede ASCII port do 4 byte int
        tmp32 = atoi(tmp_buffer);
        write_to_eeprom(EEPROM_OFFSET_BROKER_PORT, EEPROM_SIZEOF_BROKER_PORT, &tmp32);
        Debug.printf("CMD: Setting MQTT PORT = %s\n", tmp_buffer);

        // odesle potvrzeni Yodovi
        sprintf(cmd_buffer, "MQTT/PORT:%u", tmp32);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // zapis HTTP portu
      } else if (strncmp(data_from_yoda_ptr, "HTTP/PORT:", 10) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 10, data_from_yoda_len - 10);
        tmp_buffer[data_from_yoda_len - 10] = 0x00; // terminacni nula
        // převede ASCII port do 4 byte int
        tmp32 = atoi(tmp_buffer);
        write_to_eeprom(EEPROM_OFFSET_HTTP_PORT, EEPROM_SIZEOF_HTTP_PORT, &tmp32);
        Debug.printf("CMD: Setting HTTP PORT = %s\n", tmp_buffer);

        // odesle potvrzeni Yodovi
        sprintf(cmd_buffer, "HTTP/PORT:%u", tmp32);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        //start http
      } else if (strncmp(data_from_yoda_ptr, "HTTP/SERVICE:START", data_from_yoda_len) == 0) {

        Debug.printf("CMD: Starting HTTP\n");
        http_enabled = 1;

        if (http_running) {
          sprintf(cmd_buffer, "HTTP/SERVICE:RUNNING");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        } else {
          sprintf(cmd_buffer, "HTTP/SERVICE:STOPPED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        }
        
        //  start mqtt
      } else if (strncmp(data_from_yoda_ptr, "MQTT/SERVICE:CONNECT", data_from_yoda_len) == 0) {

        Debug.printf("CMD: Start MQTT\n");
        mqtt_enabled = 1;

        if (mqtt_connected) {
          Debug.printf("INFO: MQTT is connected\n");
          sprintf(cmd_buffer, "MQTT/SERVICE:CONNECTED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        } else {
          Debug.printf("INFO: MQTT is disconnected\n");
          sprintf(cmd_buffer, "MQTT/SERVICE:DISCONNECTED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        }

        // save ssid
      } else if (strncmp(data_from_yoda_ptr, "WIFI/SSID:", 10) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 10, data_from_yoda_len - 10);
        tmp_buffer[data_from_yoda_len - 10] = 0x00; // terminacni nula

        write_to_eeprom(EEPROM_OFFSET_WIFI_SSID, strlen(tmp_buffer+1), tmp_buffer);
        Debug.printf("CMD: Setting WIFI SSID = %s\n", tmp_buffer);

        sprintf(cmd_buffer, "WIFI/SSID:%s", tmp_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // save password
      } else if (strncmp(data_from_yoda_ptr, "WIFI/PASS:", 10) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 10, data_from_yoda_len - 10);
        tmp_buffer[data_from_yoda_len - 10] = 0x00; // terminacni nula

        write_to_eeprom(EEPROM_OFFSET_WIFI_PASS, strlen(tmp_buffer)+1, tmp_buffer);
        Debug.printf("CMD: Setting WIFI PASS = %s\n", tmp_buffer);

        sprintf(cmd_buffer, "WIFI/PASS:%s", tmp_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // get ssid
      } else if (strncmp(data_from_yoda_ptr, "WIFI/SSID", data_from_yoda_len) == 0) {

        Debug.printf("CMD: Requesting WIFI SSID\n");

        read_from_eeprom(EEPROM_OFFSET_WIFI_SSID, EEPROM_SIZEOF_WIFI_SSID, tmp_buffer);
        Debug.printf("CMD: Getting WIFI SSID = %s\n", tmp_buffer);

        sprintf(cmd_buffer, "WIFI/SSID:%s", tmp_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // get password
      } else if (strncmp(data_from_yoda_ptr, "WIFI/PASS", data_from_yoda_len) == 0) {

        Debug.printf("CMD: Requesting WIFI PASSWORD\n");

        read_from_eeprom(EEPROM_OFFSET_WIFI_PASS, EEPROM_SIZEOF_WIFI_PASS, tmp_buffer);
        Debug.printf("CMD: Getting WIFI PASS = %s\n", tmp_buffer);

        sprintf(cmd_buffer, "WIFI/PASS:%s", tmp_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // GET credentials has changed
      } else if (strncmp(data_from_yoda_ptr, "WIFI/CREDENTIALS_CHANGED", data_from_yoda_len) == 0) {

        Debug.printf("CMD: Requesting CREDENTIALS_CHANGED\n");        
        
        read_from_eeprom(EEPROM_OFFSET_CREDENTIALS_CHANGED, EEPROM_SIZEOF_CREDENTIALS_CHANGED, &tmp32);
        
        if(tmp32){
          Debug.printf("CMD: Getting CREDENTIALS_CHANGED = TRUE\n");
          sprintf(cmd_buffer, "WIFI/CREDENTIALS_CHANGED:TRUE");
        } else {
          Debug.printf("CMD: Getting CREDENTIALS_CHANGED = FALSE\n");
          sprintf(cmd_buffer, "WIFI/CREDENTIALS_CHANGED:FALSE");
        }
        
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

      // SET credentials has changed
      } else if (strncmp(data_from_yoda_ptr, "WIFI/CREDENTIALS_CHANGED:", 25) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 25, data_from_yoda_len - 25);
        tmp_buffer[data_from_yoda_len - 25] = 0x00; // terminacni nula

        Debug.printf("CMD: Setting CREDENTIALS_CHANGED = %s\n", tmp_buffer);  

        if(strcmp(tmp_buffer, "TRUE")==0){
          tmp32 = 1;
        } else {
          tmp32 = 0;
        }
        write_to_eeprom(EEPROM_OFFSET_CREDENTIALS_CHANGED, EEPROM_SIZEOF_CREDENTIALS_CHANGED, &tmp32);        

        sprintf(cmd_buffer, "WIFI/CREDENTIALS_CHANGED:%s", tmp_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));        

      } else if (strncmp(data_from_yoda_ptr, "MQTT/SERVICE", data_from_yoda_len) == 0) {

        Debug.printf("CMD: Yoda is requesting MQTT state\n");

        if (mqtt_connected) {
          Debug.printf("CMD: MQTT is connected\n");
          sprintf(cmd_buffer, "MQTT/SERVICE:CONNECTED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        } else {
          Debug.printf("CMD: MQTT disconnected\n");
          sprintf(cmd_buffer, "MQTT/SERVICE:DISCONNECTED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        }

        // http state
      } else if (strncmp(data_from_yoda_ptr, "HTTP/SERVICE", data_from_yoda_len) == 0) {

        Debug.printf("CMD: Yoda is requesting HTTP state\n");
        if (http_running) {
          Debug.printf("CMD: HTTP running\n");
          sprintf(cmd_buffer, "HTTP/SERVICE:RUNNING");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        } else {
          Debug.printf("CMD: HTTP dead\n");
          sprintf(cmd_buffer, "HTTP/SERVICE:STOPPED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
        }

        // set AP mode
      } else if (strncmp(data_from_yoda_ptr, "ESP/MODE:", 9) == 0) {

        // vypise a ulozi
        strncpy(tmp_buffer, data_from_yoda_ptr + 9, data_from_yoda_len - 9);
        tmp_buffer[data_from_yoda_len - 9] = 0x00; // terminacni nula

        if (strcmp(tmp_buffer, "AP") == 0) {
        } else if (strcmp(tmp_buffer, "CLIENT") == 0) {
        } else {
        }

      // get IP address
      } else if (strncmp(data_from_yoda_ptr, "WIFI/IP", 7) == 0) {        

        sprintf(cmd_buffer, "WIFI/IP:%s", WiFi.localIP().toString().c_str());
        Debug.printf("INFO: %s\n", cmd_buffer);
        send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));

        // restart
      } else if (strcmp(data_from_yoda_ptr, "ESP:RESTART") == 0) {

        Debug.printf("CMD: Restarting\n");

        // connect wifi
      } else if (strcmp(data_from_yoda_ptr, "WIFI/CONNECTION:CONNECT") == 0) {     

        wifi_enabled = 1;  

        if(WiFi.isConnected()){
          // wifi connected        
          sprintf(cmd_buffer, "WIFI/CONNECTION:CONNECTED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
          Debug.printf("WIFI: already connected\n");
        } else {          
          // wifi disconnected
          sprintf(cmd_buffer, "WIFI/CONNECTION:DISCONNECTED");
          send_to_yoda(SOCKET_COMMAND, cmd_buffer, strlen(cmd_buffer));
          Debug.printf("WIFI: disconnected, connecting procedure enabled\n");
        }
  
      } else {
        Debug.printf("CMD: unknown command\n");
      }
  
      break;

    default:
      Debug.printf("YODA->????: %4d bytes in %d ms, IGNORING\n", data_from_yoda_len, data_from_yoda_duration);
  }
}

void handleRoot() {

  Debug.printf("Someone is requesting root\n");

  String message;
  message += "<html>\n";
  message += "<head><title>Yoda access point</title></head>\n";
  message += "<body>\n";
  message += "<h1>WIFI credentials</h1>\n";  
  message += "<a href=\"/manual_credentials\">Enter Manually</a><br>\n";
  message += "<a href=\"/scan_networks\">Scan available networks</a><br>\n";
  message += "</body>\n";
  message += "</html>\n";

  server_ap->send(200, "text/html", message);

}

void handleManualCredentials() {

  Debug.printf("AP: Someone wants to enter credentials manually\n");

  char tmp_ssid[EEPROM_SIZEOF_WIFI_SSID];
  char tmp_pass[EEPROM_SIZEOF_WIFI_PASS];

  // read credentials
  read_from_eeprom(EEPROM_OFFSET_WIFI_SSID, EEPROM_SIZEOF_WIFI_SSID, tmp_ssid);
  Debug.printf("AP: ROOT: reading SSID = %s\n", tmp_ssid);
  read_from_eeprom(EEPROM_OFFSET_WIFI_PASS, EEPROM_SIZEOF_WIFI_PASS, tmp_pass);
  Debug.printf("AP: ROOT: reading PASS = %s\n", tmp_pass);

  String message;
  message += "<html>\n";
  message += "<head><title>Manually enter credentials</title></head>\n";
  message += "<body>\n";
  message += "<h1>Type your SSID and PASSWORD</h1>\n";
  message += "<form action=\"save_credentials\" method=\"get\">\n";
  message += "<input type=\"text\" name=\"ssid\" value=\"";
  message += tmp_ssid;
  message += "\"><br>\n";
  message += "<input type=\"text\" name=\"pass\" value=\"";
  message += tmp_pass;
  message += "\"><br>\n";
  message += "<input type=\"submit\" value=\"Send\">\n";
  message += "</form>\n";
  message += "</body>\n";
  message += "</html>\n";

  server_ap->send(200, "text/html", message);
}

void handleScanNetworks() {

  Debug.printf("AP: SCAN: Someone wants to scan networks\n");

  String message;
  message += "<html>\n";
  message += "<head><title>Scan networks</title></head>\n";
  message += "<body>\n";

  Debug.println("AP: SCAN: Scan start\n");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Debug.println("scan done\n");
  if (n == 0){
    Debug.println("<h1>No networks found</h1>\n");
    message+="AP: SCAN: no networks found\n";
  } else {
    Debug.printf("%d networks found\n", n);
    message += "<h1>Found networks</h1>";
    
    message += "<form action=\"save_credentials\" method=\"get\">\n";
    message += "<select name=\"ssid\">\n";

    for (int i = 0; i < n; ++i)
    {
      message+="<option value=\"";
      message+=WiFi.SSID(i);
      message+="\">";
      
      // Print SSID and RSSI for each network found
      Debug.print(i + 1);
      Debug.print(": ");
      Debug.print(WiFi.SSID(i));
      Debug.print(" (");

      message+=WiFi.SSID(i);
      message+=" (";
      /*
       TODO: RSSI (sila signalu)
      Debug.print(" (");
      Debug.print(WiFi.RSSI(i));
      Debug.print(")");
      */

      switch (WiFi.encryptionType(i)) {
        case ENC_TYPE_WEP:
          Debug.print("WEP");
          message+="WEP";
          break;
        case ENC_TYPE_TKIP:
          Debug.print("WPA/TKIP");
          message+="WPA/TKIP";
          break;
        case ENC_TYPE_CCMP:
          Debug.print("WPA2/CCMP");
          message+="WPA2/CCMP";
          break;
        case ENC_TYPE_NONE:
          Debug.print("None");
          message+="None";
          break;
        case ENC_TYPE_AUTO:
          Debug.print("Auto");
          message+="Auto";
          break;
        default:
          Debug.print("Unknown");
          message+="Unknown";
      }
      Debug.print(")\n");
      message+=")</option>";

      delay(1);
    }

    Debug.println("AP: SCAN: end of list");

    char tmp_pass[EEPROM_SIZEOF_WIFI_PASS];
    read_from_eeprom(EEPROM_OFFSET_WIFI_PASS, EEPROM_SIZEOF_WIFI_PASS, tmp_pass);
    Debug.printf("AP: SCAN: reading PASS = %s\n", tmp_pass);

    message += "</select><br>\n";
    message += "<input type=\"text\" name=\"pass\" value=\"";
    message += tmp_pass;
    message += "\">\n";
    message += "<input type=\"submit\" value=\"Send\">\n";
    message += "</form>\n";
  }

  message += "</body>\n";
  message += "</html>\n";

  server_ap->send(200, "text/html", message);

}

void handleCredentials() {

  Debug.printf("AP: Someone is setting credentials\n");

  String message;
  char tmp_ssid[EEPROM_SIZEOF_WIFI_SSID];
  char tmp_pass[EEPROM_SIZEOF_WIFI_PASS];

  message += "<html>\n";
  message += "<head><title>Yoda credentials settings</title></head>\n";
  message += "<body>\n";

  message += "<h1>Credentials</h1>\n";

  for (uint8_t i = 0; i < server_ap->args(); i++) {

    if (server_ap->argName(i) == "ssid") {
      strcpy(tmp_ssid, server_ap->arg(i).c_str());
    }

    if (server_ap->argName(i) == "pass") {
      strcpy(tmp_pass, server_ap->arg(i).c_str());
    }
  }
  message += "ssid = ";
  message += tmp_ssid;
  message += "<br>";
  message += "pass = ";
  message += tmp_pass;
  message += "<br>";
  message += "<form action=\"restart\" method=\"get\"><br>\n";
  message += "<input type=\"submit\" value=\"Restart ESP to client mode\">\n";
  message += "</form>\n";
  message += "</body>\n";
  message += "</html>";

  server_ap->send(200, "text/html", message);

  // save credentials
  write_to_eeprom(EEPROM_OFFSET_WIFI_SSID, strlen(tmp_ssid)+1, tmp_ssid);
  Debug.printf("AP: CREDE: writing SSID = %s\n", tmp_ssid);
  write_to_eeprom(EEPROM_OFFSET_WIFI_PASS, strlen(tmp_pass)+1, tmp_pass);
  Debug.printf("AP: CREDE: writing PASS = %s\n", tmp_pass);

  tmp32 = 1;
  write_to_eeprom(EEPROM_OFFSET_CREDENTIALS_CHANGED, EEPROM_SIZEOF_CREDENTIALS_CHANGED, &tmp32);

}

void handleRestart() {

  Debug.printf("AP: Someone is restarting back to client mode\n");

  String message;
  message += "<html>\n";
  message += "<head><title>Restart</title></head>\n";
  message += "<body>\n";
  message += "<h1>Restart will follow in a second</h1>\n";
  message += "</body>\n";
  message += "</html>";

  server_ap->send(200, "text/html", message);
  delay(1000);

  Debug.printf("AP: APmode is disabled, restart follows\n");
  apmode = 0;
  write_to_eeprom(EEPROM_OFFSET_APMODE, EEPROM_SIZEOF_APMODE, &apmode);
  ESP.restart();

}

void handleError() {

  Debug.printf("AP: Someone is requesting unknown page\n");

  String message;
  message += "<html>\n";
  message += "<head><title>Page not found</title></head>\n";
  message += "<body>\n";
  message += "Page Not Found\n\n";
  message += "URI: ";
  message += server_ap->uri();
  message += "\nMethod: ";
  message += (server_ap->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server_ap->args();
  message += "\n";
  for (uint8_t i = 0; i < server_ap->args(); i++) {
    message += " " + server_ap->argName(i) + ": " + server_ap->arg(i) + "\n";
  }
  message += "</body>\n";
  message += "</html>";

  server_ap->send(404, "text/html", message);

}
