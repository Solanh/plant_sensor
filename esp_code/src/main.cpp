#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <time.h>
#include <ArduinoJson.h>

WiFiClient wifi;
PubSubClient mqtt(wifi);

const int sensorPin = 34;
const int lightPin = 18;
int moistureVal;
char deviceId[13];

bool firstRead = false;

const int airVal = 2600;
const int waterVal = 1150;

// Wifi info
const char *hostnamePrefix = "plant-sensor";
const char *apPass = "plants123";

// MQTT params
const char *mqttPrefix = "plant-esp";
char mqttNameLoop[32];
char cmdTopic[64];

// grow light
bool lightOn = true;
uint16_t lightDuty = 255;

const int PWM_CH = 0;
const int PWM_HZ = 20000;
const int PWM_RES = 8;

// json
StaticJsonDocument<128> doc;

// put function declarations here:
// int myFunction(int, int);
int moisturePercent(int);
void mqttConnect(const char *mqttName);
void mqttCallback(char *, byte *, unsigned int);
void pubMoisture();
void makeDeviceId();
void applyLight();

void setup()
{
  // put your setup code here, to run once:
  // int result = myFunction(2, 3);
  Serial.begin(115200);
  delay(300);

  makeDeviceId();

  // Wifi
  uint64_t chipId = ESP.getEfuseMac();
  char apName[32];
  snprintf(apName, sizeof(apName), "%s-%04X", hostnamePrefix, (uint16_t)(chipId & 0xFFFF));

  WiFi.setHostname(apName);
  WiFiManager wm;
  wm.setDebugOutput(true);
  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(20);
  // wm.setEnableConfigPortal(true);

  bool ok = wm.autoConnect(apName, apPass);

  if (!ok)
  {
    Serial.println("Failed to connect");
    delay(2000);
    ESP.restart();
  }
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());

  // analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation(sensorPin, ADC_11db);

  // time
  const long gmtOffset_sec = -5 * 3600; // -5 hours from UTC
  const int daylightOffset_sec = 3600;  // +1 hour for DST

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo))
  {
    Serial.println("Retrying NTP...");
    delay(2000);
  }

  // mqtt pub sub
  snprintf(mqttNameLoop, sizeof(mqttNameLoop), "%s-%s", mqttPrefix, deviceId);

  mqtt.setServer("raspberrypi.local", 1883);
  mqtt.setCallback(mqttCallback);
  Serial.println("Callback set");
  mqttConnect(mqttNameLoop);
  Serial.print("MQTT name: ");
  Serial.println(mqttNameLoop);

  // grow light
  ledcSetup(PWM_CH, PWM_HZ, PWM_RES);
  ledcAttachPin(lightPin, PWM_CH);
  applyLight();
  // pinMode(lightPin, OUTPUT);
  // digitalWrite(lightPin, HIGH);
}

void loop()
{
  moistureVal = analogRead(sensorPin);
  if (!firstRead)
  {
    pubMoisture();
    firstRead = true;
  }

  if (!mqtt.connected())
    mqttConnect(mqttNameLoop);
  mqtt.loop();

  static unsigned long last = 0;
  if (millis() - last > 300000UL)
  { // 5 min
    pubMoisture();
    last = millis();
  }

  delay(50);
}

// for (int duty = 0; duty <= 255; duty++)
// {
//   ledcWrite(0, duty);
//   delay(10);
// }
// // Fade down
// for (int duty = 255; duty >= 0; duty--)
// {
//   ledcWrite(0, duty);
//   delay(10);
// }

// put function definitions here:
// int myFunction(int x, int y) {
//   return x + y;
// }

void pubMoisture() {
  doc.clear();
  char payload[256];                 
  doc["moisture"] = moisturePercent(moistureVal);
  doc["device_id"] = deviceId;
  doc["power"] = lightOn;
  doc["brightness"] = lightDuty;   

  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    doc["timestamp"] = timeStr; // add to JSON
  }
  else
  {
    doc["timestamp"] = "unknown";
  }

  char topic[64];
  snprintf(topic, sizeof(topic), "plants/%s", deviceId);

  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n > 0)
  {
    mqtt.publish(topic, payload, true);
    Serial.println(payload);
  }
  else
  {
    Serial.println("serializeJson failed");
  }
}

int moisturePercent(int raw)
{
  int percent = map(raw, airVal, waterVal, 0, 100);
  percent = constrain(percent, 0, 100);
  return percent;
}

void mqttConnect(const char *mqttName)
{

  if (!mqtt.connected())
  {
    Serial.println("Connecting to MQTT");
    if (mqtt.connect(mqttName))
    {
      Serial.println("Connected to MQTT");

      snprintf(cmdTopic, sizeof(cmdTopic), "plants/%s/cmd", deviceId);
      mqtt.subscribe(cmdTopic);

      Serial.print("Subscribed to ");
      Serial.println(cmdTopic);

      mqtt.publish("plants/test", "Hello World");
    }
    else
    {
      Serial.println("Connection Failed");
    }
  }
}

// void mqttCallback(char *topic, byte *payload, unsigned int length)
// {
//   if (length == 0)
//     return;

//   const char *action = doc["action"];
//   if (action && strcmp(action, "update") == 0)
//   {
//     // new read and publish
//     moistureVal = analogRead(sensorPin);
//     pubMoisture();
//     return;
//   }

//   char buf[256];
//   unsigned int n = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
//   memcpy(buf, payload, n);
//   buf[n] = '\0';

//   Serial.print("Message: ");
//   Serial.println(buf);

//   StaticJsonDocument<256> doc;
//   DeserializationError err = deserializeJson(doc, buf);
//   if (err)
//   {
//     Serial.print("JSON parse error: ");
//     Serial.println(err.f_str());
//     return;
//   }

//   if (doc.containsKey("power"))
//   {
//     if (doc["power"].is<bool>())
//     {
//       lightOn = doc["power"].as<bool>();
//     }
//     else if (doc["power"].is<const char *>())
//     {
//       String s = doc["power"].as<const char *>();
//       s.toLowerCase();
//       lightOn = (s == "on" || s == "true" || s == "1");
//     }
//   }

//   if (doc["toggle"].is<bool>() && doc["toggle"].as<bool>())
//   {
//     lightOn = !lightOn;
//   }

//   if (doc.containsKey("brightness"))
//   {
//     int v = doc["brightness"].as<int>();
//     if (v <= 100 && v >= 0 && !doc["asRaw255"].as<bool>())
//     {
//       v = map(v, 0, 100, 0, 255);
//     }
//     v = constrain(v, 0, 255);
//     lightDuty = (uint8_t)v;
//   }

//   applyLight();
// }
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (length == 0) return;

  char buf[256];
  unsigned int n = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = '\0';

  Serial.print("Message: ");
  Serial.println(buf);

  StaticJsonDocument<256> j;          // use a distinct name (no shadowing confusion)
  DeserializationError err = deserializeJson(j, buf);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.f_str());
    return;
  }

  // handle action:update
  const char* action = j["action"];
  if (action && strcmp(action, "update") == 0) {
    moistureVal = analogRead(sensorPin);   // or averaged read
    pubMoisture();
    return;
  }

  // handle power/toggle/brightness
  if (j.containsKey("power")) {
    if (j["power"].is<bool>()) {
      lightOn = j["power"].as<bool>();
    } else if (j["power"].is<const char*>()) {
      String s = j["power"].as<const char*>();
      s.toLowerCase();
      lightOn = (s == "on" || s == "true" || s == "1");
    }
  }
  if (j["toggle"].is<bool>() && j["toggle"].as<bool>()) {
    lightOn = !lightOn;
  }
  if (j.containsKey("brightness")) {
    int v = j["brightness"].as<int>();
    if (v <= 100 && v >= 0 && !j["asRaw255"].as<bool>()) {
      v = map(v, 0, 100, 0, 255);
    }
    v = constrain(v, 0, 255);
    lightDuty = (uint8_t)v;
  }

  applyLight();
}



void makeDeviceId()
{
  uint64_t mac = ESP.getEfuseMac();
  uint32_t hi = (uint32_t)((mac >> 32) & 0xFFFF);
  uint32_t lo = (uint32_t)(mac & 0xFFFFFFFF);
  // zero-padded 12 hex chars
  snprintf(deviceId, sizeof(deviceId), "%04X%08X", hi, lo);
}

void applyLight()
{
  ledcWrite(PWM_CH, lightOn ? lightDuty : 0);
}