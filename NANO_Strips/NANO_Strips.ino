#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SoftwareSerial.h>

#define debug Serial
SoftwareSerial communication(13, 15, false, 256);

// jak dlouha musi byt mezera mezi znakama (v ms), aby program vyhodnotil, ze prisel cely string
#define CHARACTER_TIME_DIFF         10

const char* ssid = "Byzance public";
const char* password = "PracujVic";

uint32_t  strip_color[3];
uint8_t   strip_progress[3];
bool      strip_request[3];
int       strip_effect[3];
bool      relay_state[3];
int       relay_pins[3] = {5,4,16};
int       strip_pins[3] = {14,2,0};

struct struct_lamps {
  uint8_t   header; // padding to %32bit struct
  uint8_t   strip;
  uint8_t   value;
  uint8_t   effect;
  uint32_t  color;
};

struct_lamps lamps;

// ARDUINO -> RELAYS
#define RELAY0_PIN_OUT 5
#define RELAY1_PIN_OUT 4
#define RELAY2_PIN_OUT 16

// LED STRIP
#define STRIP0_PIN_PWM 14
#define STRIP1_PIN_PWM 2
#define STRIP2_PIN_PWM 0

#define NUMBER_OF_STRIPS  3
#define NUMBER_OF_LEDS    40

// Parameter 1 = number of pixels in strip
// Parameter 2 = Arduino pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
Adafruit_NeoPixel* strip[NUMBER_OF_STRIPS];

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.
void setup() {
  
  /*
   * init LED strips and relays
   */
  for(uint32_t i = 0; i< NUMBER_OF_STRIPS; i++){
    strip[i] = new Adafruit_NeoPixel(NUMBER_OF_LEDS, strip_pins[i], NEO_GRB + NEO_KHZ800);
    strip[i]->begin();
    pinMode(relay_pins[i], OUTPUT);
  } 

  delay(1000);

  // switch off all leds on all strips
  for(uint32_t j = 0; j<NUMBER_OF_STRIPS; j++){
    for (uint32_t i = 0; i < NUMBER_OF_LEDS; i++) {
      strip[j]->setPixelColor(i, strip[j]->Color(0x00, 0x00, 0x00)); // clear
    }    
    strip[j]->show();
  }

  debug.begin(115200);
  debug.println("Connecting...\n");

  communication.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  /*
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  */

  Serial.println("Connected\n");

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("zasuvky_X3");

  // No authentication by default
  ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}

uint32_t loop_counter = 0;

char  data_from_ioda[512];
int   data_from_ioda_len = 0;

void loop() {

  if (loop_counter == 100) {
    Serial.print("NODEMCU running; IP address: ");
    Serial.println(WiFi.localIP());
    loop_counter = 0;
  } else {
    loop_counter++;
  }

  ArduinoOTA.handle();

  unsigned long time_last = 0;
  unsigned long time_diff = 0;
  unsigned long time_begin = 0;

  time_last = millis();
  time_begin = time_last;
  
  while(time_diff<CHARACTER_TIME_DIFF){
  
    while(communication.available()){
      time_last = millis();
      data_from_ioda[data_from_ioda_len] = communication.read();
      data_from_ioda_len++;
    }
    
    time_diff = millis() - time_last;
  }

  if((data_from_ioda_len == 8) && (data_from_ioda[0]==0xAA)){
    debug.printf("received %d bytes\n", data_from_ioda_len);
    memcpy(&lamps, data_from_ioda, data_from_ioda_len);
    
    debug.printf("header = 0x%02X\n", lamps.header);
    debug.printf("strip = 0x%02X\n", lamps.strip);
    debug.printf("value = 0x%02X\n", lamps.value);
    debug.printf("effect = 0x%02X\n", lamps.effect);
    debug.printf("color = 0x%08X\n", lamps.color);

    strip_request[lamps.strip]  = 1;
    strip_progress[lamps.strip] = 0;
    strip_color[lamps.strip] = lamps.color;
    strip_effect[lamps.strip] = lamps.effect;
    relay_state[lamps.strip] = lamps.value;
    
  } else {
    if(data_from_ioda_len){
      debug.printf("invalid data\n");
    }
  }

  data_from_ioda_len = 0;

  if (communication.available()) {
    debug.write(communication.read());
  }

  /*
   * ITERATION OVER ALL THE STRIPS
   */
  for(uint32_t i = 0; i<NUMBER_OF_STRIPS; i++){
    
    if (strip_request[i]) {  

     /*
     * Effect 0 -> LED goes from TOP to BOTTOM
     */
     if(strip_effect[i] == 0){
      for (uint32_t j = 0; j < NUMBER_OF_LEDS; j++) {
        if (strip_progress[i]== j) {
          strip[i]->setPixelColor(j, strip[i]->Color((strip_color[i] & 0x00ff0000) >> 16, (strip_color[i] & 0x0000ff00) >> 8, (strip_color[i] & 0x000000ff) >> 0)); // red, green, blue
        } else {
          strip[i]->setPixelColor(j, strip[i]->Color(0x00, 0x00, 0x00)); //disable
        }
      }

    /*
     * Effect 1 -> LED goes from BOTTOM to TOP
     */
     } else if (strip_effect[i] == 1){

       for (uint32_t j = 0; j < NUMBER_OF_LEDS; j++) {
        if ((NUMBER_OF_LEDS - strip_progress[i])== j) {
          strip[i]->setPixelColor(j, strip[i]->Color((strip_color[i] & 0x00ff0000) >> 16, (strip_color[i] & 0x0000ff00) >> 8, (strip_color[i] & 0x000000ff) >> 0)); // red, green, blue
        } else {
          strip[i]->setPixelColor(j, strip[i]->Color(0x00, 0x00, 0x00)); //disable
        }
      }

    /*
     * Effect 2 -> BREATHING
     */
     } else if (strip_effect[i] == 2){

      uint32_t color = strip_color[i];

      uint32_t rgbdelta = 0;

      float delta_r = ((float)((color & 0x00FF0000)>>16) / (float)NUMBER_OF_LEDS);
      float delta_g = ((float)((color & 0x0000FF00)>> 8) / (float)NUMBER_OF_LEDS);
      float delta_b = ((float)((color & 0x000000FF)>> 0) / (float)NUMBER_OF_LEDS);

      debug.printf("delta_r = %f, delta_g = %f, delta_b = %f\n", delta_r, delta_g, delta_b);

      // inhale
      if(strip_progress[i]<(NUMBER_OF_LEDS/2)){
        rgbdelta |= (uint32_t)((uint32_t)delta_r*strip_progress[i]*2)<<16;
        rgbdelta |= (uint32_t)((uint32_t)delta_g*strip_progress[i]*2)<<8;
        rgbdelta |= (uint32_t)((uint32_t)delta_b*strip_progress[i]*2)<<0;

      // exhale 
      } else {
        rgbdelta |= (uint32_t)((uint32_t)delta_r*(NUMBER_OF_LEDS-strip_progress[i])*2)<<16;
        rgbdelta |= (uint32_t)((uint32_t)delta_g*(NUMBER_OF_LEDS-strip_progress[i])*2)<<8;
        rgbdelta |= (uint32_t)((uint32_t)delta_b*(NUMBER_OF_LEDS-strip_progress[i])*2)<<0;        
      }

      debug.printf("progress = %d, rgbdelta = 0x%08X\n", strip_progress[i], rgbdelta);

      for (uint32_t j = 0; j < NUMBER_OF_LEDS; j++) {
        strip[i]->setPixelColor(j, strip[i]->Color((rgbdelta & 0x00ff0000) >> 16, (rgbdelta & 0x0000ff00) >> 8, (rgbdelta & 0x000000ff) >> 0)); // red, green, blue
      }

     /*
     * Effect 3 -> RAIN
     */
     } else if (strip_effect[i] == 3){

      uint32_t color = strip_color[i];

      uint32_t rgbdelta = 0;

      float delta_r = ((float)((color & 0x00FF0000)>>16) / (float)NUMBER_OF_LEDS);
      float delta_g = ((float)((color & 0x0000FF00)>> 8) / (float)NUMBER_OF_LEDS);
      float delta_b = ((float)((color & 0x000000FF)>> 0) / (float)NUMBER_OF_LEDS);

      debug.printf("delta_r = %f, delta_g = %f, delta_b = %f\n", delta_r, delta_g, delta_b);

      strip[i]->setPixelColor(strip_progress[i], strip[i]->Color(delta_g*strip_progress[i], delta_g*strip_progress[i], delta_b*strip_progress[i])); // red, green, blue
      
     } else {
      debug.printf("unsupported effect=%d\n", strip_effect[i]);
     }

      //all colors for all pixels were set, show it
      strip[i]->show();
  
      strip_progress[i]++;
 
    }

    delay(2);

  /*
   * CLEAR STRIPS
   */
    if (strip_progress[i] == NUMBER_OF_LEDS) {
  
      // switch off strip
      for (uint32_t k = 0; k < NUMBER_OF_LEDS; k++) {
        strip[i]->setPixelColor(k, strip[i]->Color(0x00, 0x00, 0x00)); //set it
      }  
      strip[i]->show();
  
      Serial.printf("strip %d off\n", i);

      strip_progress[i] = 0;
      strip_request[i] = 0;

    /*
    * UPDATE RELAY STATE
    */
    digitalWrite(relay_pins[i], relay_state[i]);
    }
    
  }

}

