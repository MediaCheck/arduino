#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>

#define Yoda Serial
#define Debug Serial1

/* Set these to your desired credentials. */
const char *ssid = "YODA_123456789";
//const char *password = "";

ESP8266WebServer server(80);

/* Just a little test message.  Go to http://192.168.4.1 in a web browser
 * connected to this access point to see it.
 */
void handleRoot() {
  
  String message;
  message+="<h1>You are connected</h1>";
  message+="<form action=\"save_credentials\" method=\"get\"><br>";
  message+="<input type=\"text\" name=\"ssid\" value=\"ssid\"><br>";
  message+="<input type=\"text\" name=\"pass\" value=\"password\">";
  message+="<input type=\"submit\" value=\"Send\">";
  message+="</form>";
  
	server.send(200, "text/html", message);
 
  Debug.printf("Someone is requesting root\n");
}

void handleCredentials() {
  
  Debug.printf("Someone is setting credentials\n");

  String message;  
  String rcvd_ssid;
  String rcvd_pass;

  message += "Credentials\n";

  for (uint8_t i=0; i<server.args(); i++){
    
    if(server.argName(i) == "ssid"){
        rcvd_ssid = server.arg(i);
    }

    if(server.argName(i) == "pass"){
        rcvd_pass = server.arg(i);
    }
  }

  message+= "ssid = ";
  message+=rcvd_ssid;
  message+= "\n";
  
  message+= "pass = ";
  message+=rcvd_pass;
  message+= "\n";

  server.send(200, "text/plain", message);

}

void handleError() {
  
  Debug.printf("Someone is requesting unknown page\n");

  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);

}

void setup() {
	delay(1000);
	Debug.begin(921600);
	Debug.println();
	Debug.print("Configuring access point...\n");
	/* You can remove the password parameter if you want the AP to be open. */
	WiFi.softAP(ssid);

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

	IPAddress myIP = WiFi.softAPIP();
	Debug.print("AP IP address: ");
	Debug.println(myIP);
	server.on("/", handleRoot);
 server.on("/save_credentials", handleCredentials);
  server.onNotFound(handleError);
	server.begin();
	Debug.println("HTTP server started");
}

void loop() {
  ArduinoOTA.handle();  
	server.handleClient();
}
