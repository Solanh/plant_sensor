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

// schedule
bool schedEnabled = false;
uint16_t schedStartMin = 0;
uint16_t schedEndMin = 0;
unsigned long lastSchedCheck = 0;
bool overrideActive = false;
unsigned long overrideUntil = 0;
const unsigned long OVERRIDE_MS = 15UL;
bool userPower = true;
bool effectiveOn = true;

// Wifi info
const char *hostnamePrefix = "plant-sensor";
const char *apPass = "plants123";

// MQTT params
const char *mqttPrefix = "plant-esp";
char mqttNameLoop[32];
char cmdTopic[64];

// grow light
uint16_t lightDuty = 255;

const int PWM_CH = 0;
const int PWM_HZ = 20000;
const int PWM_RES = 8;

// json
StaticJsonDocument<320> doc;
// prefs
Preferences prefs;
String storedName;

// put function declarations here:
// int myFunction(int, int);
int moisturePercent(int);
void mqttConnect(const char *mqttName);
void mqttCallback(char *, byte *, unsigned int);
void pubMoisture();
void makeDeviceId();
void applyLight();
int parseHHMM(const char *);
bool inWindow(uint16_t, uint16_t, uint16_t);
uint16_t nowMinutesLocal();
void applyLightScheduled();

void setup()
{
  // put your setup code here, to run once:
  // int result = myFunction(2, 3);
  Serial.begin(115200);
  delay(300);

  makeDeviceId();

  prefs.begin("plant", false);
  storedName = prefs.getString("name", "");

  schedEnabled = prefs.getBool("sched_en", false);
  schedStartMin = prefs.getUShort("sched_s", 0);
  schedEndMin = prefs.getUShort("sched_e", 0);

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
  setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1); // US Eastern with DST
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

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
  mqtt.setBufferSize(768);
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
  if (millis() - lastSchedCheck > 1000UL)
  {
    lastSchedCheck = millis();
    applyLightScheduled();
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

void pubMoisture()
{
  doc.clear();
  char payload[768];
  doc["moisture"] = moisturePercent(moistureVal);
  doc["device_id"] = deviceId;
  doc["power"] = userPower;
  doc["effective"] = effectiveOn;
  doc["brightness"] = lightDuty;
  if (storedName.length())
  {
    doc["name"] = storedName;
  }
  JsonObject sch = doc.createNestedObject("schedule");
  sch["enabled"] = schedEnabled;
  char startBuf[6], endBuf[6];
  snprintf(startBuf, sizeof(startBuf), "%02d:%02d", schedStartMin / 60, schedStartMin % 60);
  snprintf(endBuf, sizeof(endBuf), "%02d:%02d", schedEndMin / 60, schedEndMin % 60);
  sch["start"] = startBuf;
  sch["end"] = endBuf;

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

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  if (length == 0)
    return;

  Serial.print("Message: ");
  Serial.write(payload, length);
  Serial.println();

  StaticJsonDocument<384> j; // a bit larger buffer for complex JSON
  DeserializationError err = deserializeJson(j, payload, length);

  if (err)
  {
    Serial.print("JSON parse error: ");
    Serial.println(err.f_str());
    return;
  }

  // update
  const char *action = j["action"];
  if (action && strcmp(action, "update") == 0)
  {
    moistureVal = analogRead(sensorPin);
    pubMoisture();
    return;
  }

  if (j.containsKey("name"))
  {
    const char *incoming = j["name"];
    if (incoming && *incoming)
    { // ignore empty names
      String s = String(incoming);
      s.trim();
      if (s.length() > 40)
        s = s.substring(0, 40);
      if (s != storedName)
      {
        storedName = s;
        prefs.putString("name", storedName);
        pubMoisture();
      }
    }
  }
  if (j.containsKey("schedule"))
  {
    JsonObject s = j["schedule"].as<JsonObject>();
    bool en = s.containsKey("enabled") ? s["enabled"].as<bool>() : schedEnabled;

    uint16_t newStart = schedStartMin;
    uint16_t newEnd = schedEndMin;

    if (s.containsKey("start"))
    {
      int v = parseHHMM(s["start"]);
      if (v >= 0)
        newStart = (uint16_t)v;
    }
    if (s.containsKey("end"))
    {
      int v = parseHHMM(s["end"]);
      if (v >= 0)
        newEnd = (uint16_t)v;
    }

    bool changed = (en != schedEnabled) || (newStart != schedStartMin) || (newEnd != schedEndMin);

    schedEnabled = en;
    schedStartMin = newStart;
    schedEndMin = newEnd;

    // persist & confirm
    prefs.putBool("sched_en", schedEnabled);
    prefs.putUShort("sched_s", schedStartMin);
    prefs.putUShort("sched_e", schedEndMin);

    overrideActive = false;  
    applyLightScheduled();
    pubMoisture();
  }

  if (j.containsKey("power"))
  {
    if (j["power"].is<bool>())
      userPower = j["power"].as<bool>();
    else if (j["power"].is<const char *>())
    {
      String s = j["power"].as<const char *>();
      s.toLowerCase();
      userPower = (s == "on" || s == "true" || s == "1");
    }
    if (userPower && lightDuty == 0)
      lightDuty = 255;
    overrideActive = true;
    overrideUntil = millis() + OVERRIDE_MS;
  }

  if (j["toggle"].is<bool>() && j["toggle"].as<bool>())
  {
    userPower = !userPower;
    overrideActive = true;
    overrideUntil = millis() + OVERRIDE_MS;
  }

  if (j.containsKey("brightness"))
  {
    int v = j["brightness"].as<int>();
    if (v <= 100 && v >= 0 && !j["asRaw255"].as<bool>())
      v = map(v, 0, 100, 0, 255);
    v = constrain(v, 0, 255);
    lightDuty = (uint8_t)v;
    overrideActive = true;
    overrideUntil = millis() + OVERRIDE_MS;
  }
  applyLightScheduled();
  pubMoisture();
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
  ledcWrite(PWM_CH, effectiveOn ? lightDuty : 0);
}

int parseHHMM(const char *s)
{
  if (!s || strlen(s) < 4)
    return -1;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2)
    return -1;
  if (h < 0 || h > 23 || m < 0 || m > 59)
    return -1;
  return h * 60 + m;
}

bool inWindow(uint16_t nowMin, uint16_t startMin, uint16_t endMin)
{
  if (startMin == endMin)
    return false; // 0-length window = off
  if (startMin < endMin)
    return nowMin >= startMin && nowMin < endMin;
  return nowMin >= startMin || nowMin < endMin; // crosses midnight
}

uint16_t nowMinutesLocal()
{
  struct tm t;
  if (!getLocalTime(&t))
    return 0;
  return (uint16_t)(t.tm_hour * 60 + t.tm_min);
}

void applyLightScheduled()
{
  if (overrideActive)
  {
    if ((long)(overrideUntil - millis()) > 0)
    {
      effectiveOn = userPower;
      applyLight();
      return;
    }
    overrideActive = false;
  }

  if (!schedEnabled)
  {
    effectiveOn = userPower;
    applyLight();
    return;
  }

  if (!userPower)
  {
    effectiveOn = false;
  }
  else
  {
    const bool inWin = inWindow(nowMinutesLocal(), schedStartMin, schedEndMin);
    effectiveOn = inWin;
  }

  applyLight();
}
