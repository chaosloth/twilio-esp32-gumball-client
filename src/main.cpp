/*
  ============================================================
  Copyright (c) 2022 Christopher Connolly
  Project:     Gumball actuator
  Author:      Christopher Connolly
  Last update: 25.04.2022
  Description: ESP8266 Gumball machine, listens for websocket instructions
  License:     GNU GPL 3, see libraries for respective licenes too
  ============================================================
*/
#define VERSION "1.5.0" // Internal version
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>      // Include the ESP8266 Wi-Fi library
#include <ESP8266WiFiMulti.h> // Include the ESP8266 Wi-Fi-Multi library
#include <ESP8266mDNS.h>      // Include the ESP8266 mDNS library
#elif defined(ESP32)
#include <WiFiMulti.h> // Include the ESP32 Wi-Fi-Multi Library
#include <ESPmDNS.h>   // Include the ESP32 mDNS Library
#include <SPIFFS.h>    // Include the ESP32 SPIFFS Library
#include <AsyncTCP.h>
#endif

#include <ESPAsyncWebServer.h>
#include <ESPAsync_WiFiManager.h>
#include <AsyncElegantOTA.h> // Include ElegantOTA library for ESP32 only
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

// Hardcoded stuff
#define OTA_USERNAME "twilio" // Username for OTA firmware updates
#define OTA_PASSWORD "twilio" // Password for OTA firmware updates

// #define MOTOR_A_PIN 2               // Use LED as indicator for now
#define WIFICHECK_INTERVAL 5000L    // How often to check WIFI connection
#define BUTTONCHECK_INTERVAL 10000L // How often to check for button press

// WIFI Multi
#if defined(ESP8266)
// Use wifiMulti
ESP8266WiFiMulti wifiMulti; // Use ESP8266 WifiMulti
#define MOTOR_A_PIN 2       // GPIO 2
#define CONFIG_WIFI_PIN 0   // GPIO 0
#elif defined(ESP32)        // Use ESP32 WifiMulti
// Use wifiMulti
WiFiMulti wifiMulti;
// PINS
#define MOTOR_A_PIN 25      // Use LED as indicator for now
#define CONFIG_WIFI_PIN 0   // Built in button on ESP32-Dev is 0, use 15 for production boards
#endif

AsyncWebServer webServer(80);
DNSServer dnsServer;
String unique_hostname = "Twilio_Gumball_" + String(ESP_getChipId(), HEX); // Suffix AP name with Chip ID
ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, unique_hostname.c_str());

// Connectivitiy
bool initOtaComplete = false;
bool initialConfig = false;
bool initWSComplete = false;
bool isWSConnected = false;

static ulong checkbutton_timeout = 0;
static ulong checkwifi_timeout = 0;

// Configurable parameters in the UI
const char *ws_url = "ws://twilio-gumball-ws-host.herokuapp.com/";
websockets::WebsocketsClient wsClient;

// #define LABEL_WS_URL               "WS Host URL"
// #define LABEL_DISPENSE_DURATION     "Dispense DurationL"
// ESPAsync_WMParameter                p_mqtt_url(LABEL_WS_URL, LABEL_WS_URL, "XXX", 255);
// ESPAsync_WMParameter                p_dispense_duration(LABEL_DISPENSE_DURATION, LABEL_DISPENSE_DURATION, "YYY", 1);

/* This variables stores the duration in milliseconds to dispense candy. Longer = more candy!!! */
int dispense_duration = 4000L;

void giveMeCandy(int duration)
{
  // Serial.printf("Dispensing candy!\n");
  digitalWrite(MOTOR_A_PIN, HIGH);
  delay(duration);
  digitalWrite(MOTOR_A_PIN, LOW);
}

void connectOrConfigureWifi()
{
  if (initialConfig)
  {
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

    if (!ESPAsync_wifiManager.startConfigPortal(unique_hostname.c_str(), OTA_PASSWORD))
    {
      Serial.println(F("Not connected to WiFi"));
    }
    else
    {
      Serial.println(F("Wifi connected"));
      String Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
      String Router_Pass = ESPAsync_wifiManager.WiFi_Pass();
      wifiMulti.addAP(Router_SSID.c_str(), Router_Pass.c_str());
      WiFi.mode(WIFI_STA);
      WiFi.begin();
    }

    initialConfig = false;
    digitalWrite(LED_BUILTIN, LOW);
  }
  else
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println(F("Normal mode. Entering WIFI_STA mode"));
      WiFi.mode(WIFI_STA);
      WiFi.begin();
      int retryCount = 0;
      while (WiFi.status() != WL_CONNECTED && retryCount <= 20)
      {
        retryCount++;
        Serial.print('.');
        delay(1000);
      }

      if (retryCount >= 20)
      {
        initialConfig = true;
      }
    }
    else
    {
      Serial.print(F("WIFI Connected. IP:"));
      Serial.println(WiFi.localIP());
    }
  }
}

void onWsEventsCallback(websockets::WebsocketsEvent event, String data)
{
  if (event == websockets::WebsocketsEvent::ConnectionOpened)
  {
    Serial.println(F("WS Connnection Opened"));
  }
  else if (event == websockets::WebsocketsEvent::ConnectionClosed)
  {
    Serial.println(F("WS Connnection Closed"));
    isWSConnected = false;
  }
  else if (event == websockets::WebsocketsEvent::GotPing)
  {
    Serial.println(F("WS Got a Ping!"));
  }
  else if (event == websockets::WebsocketsEvent::GotPong)
  {
    Serial.println(F("WS Got a Pong!"));
  }
  else
  {
    Serial.println(F("WS Got unknown event"));
  }
}
void sendWSMessage(String message)
{
  wsClient.send(message);
  Serial.printf(">> Sending WS message: %s\n", message.c_str());
}

void onWsMessageCallback(websockets::WebsocketsMessage message)
{
  Serial.printf("<<< Got WS Message: %s\n", message.data().c_str());
  try
  {
    StaticJsonDocument<500> json;
    if (deserializeJson(json, message.data().c_str()) == DeserializationError::Ok)
    {
      JsonObject object = json.as<JsonObject>();
      JsonVariant action = object.getMember("action");
      if (action.isNull())
      {
        Serial.println(F("Error: action is missing"));
      }
      else if (strcmp(action.as<const char *>(), "none") == 0)
      {
        Serial.print(F("No action required "));
        Serial.println(message.data());
      }
      else if (strcmp(action.as<const char *>(), "dispense") == 0)
      {
        JsonVariant duration = object.getMember("duration");
        if (duration.isNull())
        {
          giveMeCandy(dispense_duration);
        }
        else
        {
          giveMeCandy(duration);
        }
      }
      else if (strcmp(action.as<const char *>(), "ping") == 0)
      {
        // Send PONG
        sendWSMessage("{\"action\":\"pong\"}");
      }
      else
      {
        throw std::runtime_error("Unknown message or cannot deserialize message");
      }
    }
  }
  catch (std::exception const &e)
  {
    printf("Message Processing Error: %s\n", e.what());
  }
}

void connectOrConfigureWebsocket()
{
  // Exit if already initialized
  if (isWSConnected)
    return;
  /* TODO:
  1. Get WS Url from configuration
  */
  wsClient = {};                           // Reset web socket client
  wsClient.onMessage(onWsMessageCallback); // Wire events callback
  wsClient.onEvent(onWsEventsCallback);    // Wire message callback

  isWSConnected = wsClient.connect(ws_url); // Establish the web socket connection
}

#if defined(ESP32)
void initOTA()
{
  // Exit if already initialized
  if (initOtaComplete)
    return;

  /* Advertise us with MDNS */
  if (!MDNS.begin(unique_hostname.c_str()))
  { // http://<DEVICE_NAME>.local
    Serial.println(F("Error setting up MDNS responder!"));
  }
  else
  {
    Serial.println("mDNS configured: " + String(unique_hostname) + ".local");
  }

  webServer.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send(SPIFFS, "/index.html", "text/html"); });

  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send(SPIFFS, "/index.html", "text/html"); });

  webServer.on("/info", HTTP_GET, [](AsyncWebServerRequest *request)
               {
    StaticJsonDocument<150> data;
    data["hostname"] = unique_hostname;
    data["version"] = VERSION;
    data["ip"] = WiFi.localIP().toString();
    data["wsConnected"] = isWSConnected;
#if defined(ESP8266)
    data["platform"] = "ESP8266";
#elif defined(ESP32)
    data["platform"] = "ESP32";
#endif
    String response;
    serializeJson(data, response);
    request->send(200, "application/json", response); });

  webServer.onNotFound([](AsyncWebServerRequest *request)
                       { request->send(404); });

  // Configure ElegantOTA with username and password
  AsyncElegantOTA.begin(&webServer, OTA_USERNAME, OTA_PASSWORD);

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "content-type");

  webServer.begin();
  MDNS.addService("http", "tcp", 80);

  Serial.println(F("HTTP server started"));
  initOtaComplete = true;
}
#endif

void checkWifiLoop()
{
  static ulong current_millis;
  current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
  {
    if ((WiFi.status() != WL_CONNECTED) || initialConfig)
    {
      Serial.println(F("WIFI not connected"));
      connectOrConfigureWifi(); // Connect to WIFI
    }
    else
    {
      Serial.print(F("WIFI Connected. Local IP: "));
      Serial.println(WiFi.localIP());
      /*
        Once we are connected to WIFI we must configure OTA update
        otherwise we will be unable to update firmware, calling initOTA
        before having a WIFI connection will cause the ESP to crash
      */
#if defined(ESP32)
      initOTA();
#endif
      connectOrConfigureWebsocket(); // Connect to WS
    }
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }
}

void blinkLed(int led, int blinkDelay, int cycles)
{
  for (int i = 0; i < cycles; i++)
  {
    digitalWrite(led, HIGH);
    delay(blinkDelay);
    digitalWrite(led, LOW);
    delay(blinkDelay);
  }
}

void checkButtons()
{
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

void setup()
{
  // MOTORS AND LEDS
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(MOTOR_A_PIN, OUTPUT);
  pinMode(CONFIG_WIFI_PIN, INPUT_PULLUP);
  // pinMode(CONFIG_WIFI_PIN, INPUT);

  // set-up serial for debug output
  Serial.begin(115200);
  while (!Serial)
    ;
  delay(200);
  Serial.printf("\nStarting Connected Gumball Feeder\n");
#if defined(ESP32)
  if (!SPIFFS.begin(true))
  {
    Serial.println(F("An Error has occurred while mounting SPIFFS"));
    return;
  }
#endif
}

void loop()
{
  checkButtons();  // Check if buttons have been pressed
  checkWifiLoop(); // Ensure WIFI connected
  wsClient.poll(); // Check for incoming messages
  AsyncElegantOTA.loop();

  yield();
}