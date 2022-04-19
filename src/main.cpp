/*
  ============================================================
  Copyright (c) 2022 Christopher Connolly
  Project:     Gumball actuator
  Author:      Christopher Connolly
  Last update: 14.04.2022
  Description: ESP32 Gumball machine, listens for websocket instructions
  License:     GNU GPL 3, see libraries for respective licenes too
  ============================================================
*/
#include <Arduino.h>
#include <WiFiMulti.h>
#include <AsyncElegantOTA.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsync_WiFiManager.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>


// Hardcoded stuff
#define OTA_USERNAME                "twilio"           // Username for OTA firmware updates
#define OTA_PASSWORD                "tw1l10ns"         // Password for OTA firmware updates

// #define MOTOR_A_PIN                 5                   // Use LED as indicator for now
#define MOTOR_A_PIN                 2                   // Use LED as indicator for now
#define WIFICHECK_INTERVAL          5000L               // How often to check WIFI connection
#define CONFIG_WIFI_PIN             15                  // Built in button on ESP-Dev is 0, use 15 for production boards
#define BUTTONCHECK_INTERVAL        10000L              // How often to check for button press

// Web Server
AsyncWebServer                      webServer(80);

// Connectivitiy
bool initOtaComplete                = false;
bool initialConfig                  = false;
bool initWSComplete                 = false;
bool isWSConnected                  = false;
static ulong checkbutton_timeout    = 0;
static ulong checkwifi_timeout      = 0;
DNSServer                           dnsServer;
String unique_hostname              = "Twilio_Gumball_" + String(ESP_getChipId(), HEX);   // Suffix AP name with Chip ID
ESPAsync_WiFiManager                ESPAsync_wifiManager(&webServer, &dnsServer, unique_hostname.c_str());

// Configurable parameters in the UI
const char *ws_url                  = "wss://localhost/wsconnect";
websockets::WebsocketsClient        wsClient;

// #define LABEL_WS_URL               "WS Host URL"
// #define LABEL_DISPENSE_DURATION     "Dispense DurationL"
// ESPAsync_WMParameter                p_mqtt_url(LABEL_WS_URL, LABEL_WS_URL, "XXX", 255);
// ESPAsync_WMParameter                p_dispense_duration(LABEL_DISPENSE_DURATION, LABEL_DISPENSE_DURATION, "YYY", 1);

// Use wifiMulti
WiFiMulti wifiMulti;

/* This variables stores the duration in milliseconds to dispense candy. Longer = more candy!!! */
int dispense_duration = 4000L;

void giveMeCandy() {
  // Serial.printf("Dispensing candy!\n");
  digitalWrite(MOTOR_A_PIN, HIGH);
  delay(dispense_duration);
  digitalWrite(MOTOR_A_PIN, LOW);
}


void connectOrConfigureWifi() {
  if (initialConfig) {
    Serial.println(F("Inital Config mode. Starting Portal"));
    digitalWrite(LED_BUILTIN, HIGH);

    /* Logic is as follows:
    1. Start AP Mode
    2. Configure IP
    3. Start web server
    4. Wait until all clients end
    5. Store the WIFI creds and connect
    */

    // Set timeout on configuration portal
    ESPAsync_wifiManager.setConfigPortalTimeout(30);

    if (!ESPAsync_wifiManager.startConfigPortal(unique_hostname.c_str(), OTA_PASSWORD)) {
      Serial.println(F("Not connected to WiFi"));
    } else {
      Serial.println(F("Wifi connected"));
      String Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
      String Router_Pass = ESPAsync_wifiManager.WiFi_Pass();
      Serial.println("WIFI Manage storing SSID = " + Router_SSID + ", Pass = " + Router_Pass);
      wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
      WiFi.mode(WIFI_STA);
      WiFi.begin();
    }

    initialConfig = false;
    digitalWrite(LED_BUILTIN, LOW);

  } else {
    Serial.println(F("Normal mode. Entering WIFI_STA mode"));
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }
}


void onWsEventsCallback(websockets::WebsocketsEvent event, String data)
{
  if (event == websockets::WebsocketsEvent::ConnectionOpened)
  {
    Serial.println("Connnection Opened");
  }
  else if (event == websockets::WebsocketsEvent::ConnectionClosed)
  {
    Serial.println("Connnection Closed");
    isWSConnected = false;
  }
  else if (event == websockets::WebsocketsEvent::GotPing)
  {
    Serial.println("Got a Ping!");
  }
  else if (event == websockets::WebsocketsEvent::GotPong)
  {
    Serial.println("Got a Pong!");
  }
  else
  {
    Serial.println("Got unknown event");
  }
}

void onWsMessageCallback(websockets::WebsocketsMessage message)
{
  Serial.printf("<<< Got Message:\n%s\n\n", message.data().c_str());
  try {
    StaticJsonDocument<500> json;
    if (deserializeJson(json, message.data().c_str()) == DeserializationError::Ok)
    {
      //TODO: Somthing with the message
      giveMeCandy();
    }
    else
    {
      throw std::runtime_error("Cannot deserialize message");
    }
  }
  catch (std::exception const &e)
    {
      printf("Message Processing Error: %s\n", e.what());
    }
}

void connectOrConfigureWebsocket() {
  // Exit if already initialized
  if (isWSConnected) return;
  /* TODO:
  1. Get WS Url
  2. Wire receiver function
  3. Digest message
  4. Connect to host
  */
 
 isWSConnected = wsClient.connect(ws_url);   // Establish the web socket connection
}

void initOTA() {
  // Exit if already initialized
  if (initOtaComplete) return;

  /* Advertise us with MDNS */
  if (!MDNS.begin(unique_hostname.c_str())) { // http://<DEVICE_NAME>.local
    Serial.println(F("Error setting up MDNS responder!"));
  } else {
    Serial.println("mDNS configured: " + String(unique_hostname) + ".local");
  }

  webServer.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html","text/html");
  });

  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html","text/html");
  });

  webServer.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });
    
  // Configure ElegantOTA with username and password
  AsyncElegantOTA.begin(&webServer, OTA_USERNAME, OTA_PASSWORD);
  webServer.begin();
  MDNS.addService("http", "tcp", 80);
  
  Serial.println(F("HTTP server started"));
  initOtaComplete = true;
}

void checkWifiLoop() {
  static ulong current_millis;
  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
  {
    if ((WiFi.status() != WL_CONNECTED) || initialConfig) {
      Serial.println(F("WIFI not connected"));
    } else {
      Serial.print(F("WIFI Connected. Local IP: "));
      Serial.println(WiFi.localIP());
      /*
        Once we are connected to WIFI we must configure OTA update
        otherwise we will be unable to update firmware, calling initOTA
        before having a WIFI connection will cause the ESP to crash
      */
      initOTA();
      connectOrConfigureWifi();                   // Connect to WIFI
      connectOrConfigureWebsocket();              // Connect to WS
    }
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }
}

void blinkLed(int led, int blinkDelay, int cycles) {
  for (int i = 0; i < cycles; i++) {
    digitalWrite(led, HIGH);
    delay(blinkDelay);
    digitalWrite(led, LOW);
    delay(blinkDelay);
  }
}

void checkButtons() {
  static ulong current_millis;
  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL (10) seconds.
  if ((current_millis > checkbutton_timeout) || (checkbutton_timeout == 0))
  {
    if ((digitalRead(CONFIG_WIFI_PIN) == LOW))
    {
      Serial.println(F("\nConfiguration portal requested."));
      blinkLed(LED_BUILTIN, 100, 5);
      initialConfig = true;
      connectOrConfigureWifi();
    }

    // Increment current timeout
    checkbutton_timeout = current_millis + BUTTONCHECK_INTERVAL;
  }
}

/* ============ Business end of town ============
  Setup logic is as follows:
  1. Configure pins for external devices
  2. Setup filesystem
  3. Configure WIFI
  4. Configure Websocket

  In the main loop
  1. Check we're connected
*/

void setup() {
  // MOTORS AND LEDS
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(MOTOR_A_PIN, OUTPUT);
  pinMode(CONFIG_WIFI_PIN, INPUT_PULLUP);
  // pinMode(CONFIG_WIFI_PIN, INPUT);  

  // set-up serial for debug output
  Serial.begin(115200); while (!Serial); delay(200);
  Serial.printf("\nStarting Connected Gumball Feeder\n");

  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  wsClient.onMessage(onWsMessageCallback);    // Wire events callback
  wsClient.onEvent(onWsEventsCallback);       // Wire message callback
}

void loop() {
    checkButtons();                           // Check if buttons have been pressed
    checkWifiLoop();                          // Ensure WIFI connected
}