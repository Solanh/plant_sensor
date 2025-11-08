#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>

WiFiClient wifi;
PubSubClient mqtt(wifi);

const int sensorPin = 34;
int moistureVal;

const int airVal = 2600;
const int waterVal = 1150;

// Wifi info
const char *hostnamePrefix = "plant-sensor";
const char *apPass = "plants123";

// MQTT params
const char *mqttPrefix = "plant-esp";
char mqttNameLoop[32];

// put function declarations here:
// int myFunction(int, int);
int moisturePercent(int);
void mqttConnect(const char *mqttName);

void setup()
{
  // put your setup code here, to run once:
  // int result = myFunction(2, 3);
  Serial.begin(115200);
  delay(300);

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

  // mqtt pub sub
  snprintf(mqttNameLoop, sizeof(mqttNameLoop), "%s-%04X", mqttPrefix, (uint16_t)(chipId & 0xFFFF));

  mqtt.setServer("pihole.local", 1883);
  mqttConnect(mqttNameLoop);
  Serial.print("MQTT name: ");
  Serial.println(mqttNameLoop);
}

void loop()
{

  if (!mqtt.connected())
    mqttConnect(mqttNameLoop);
  mqtt.loop();
  static unsigned long last = 0;
  if (millis() - last > 60000)
  {
    mqtt.publish("plants/test", "working");
    last = millis();
  }

  // put your main code here, to run repeatedly:
  moistureVal = analogRead(sensorPin);
  Serial.println(moisturePercent(moistureVal));
  delay(1000);
}

// put function definitions here:
// int myFunction(int x, int y) {
//   return x + y;
// }

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
      mqtt.publish("plants/test", "Hello World");
    }
    else
    {
      Serial.println("Connection Failed");
    }
  }
} 