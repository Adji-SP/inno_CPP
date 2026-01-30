#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <ArduinoJson.h> // Ensure ArduinoJson is included
#include "TuyaDevice.h"

// ===== DEFAULTS =====
char tuya_id[40] = "a3658e534d4fad4173ijbi";
char tuya_key[40] = "jBM'#B$OGU/aN_Ud";
char tuya_ip[20] = "192.168.223.41";
char api_url[80] = "http://192.168.1.10:8000";      
char time_url[80] = "http://192.168.1.10:8000/time";
const float VER = 3.3f;

// ===== HARDWARE =====
const int RELAY_CNT = 8;
const int R_IDS[RELAY_CNT] = {2, 3, 4, 5, 6, 7, 8, 9};
const int R_PINS[RELAY_CNT] = {27, 26, 25, 33, 19, 18, 17, 16};

// Track relay states
int last_r_state[RELAY_CNT];

// ===== OBJECTS =====
TuyaDevice *device = nullptr;
HTTPClient http;
Preferences prefs;
bool shouldSave = false;
RTC_DS3231 rtc;
JsonDocument doc; // ArduinoJson v7
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== LCD STATE =====
bool showRelayMsg = false;
unsigned long msgStartTime = 0;
const unsigned long MSG_DURATION = 3000;
String relayLine1 = "";
String relayLine2 = "";

// ===== TIMING =====
unsigned long lastSensor = 0;
unsigned long lastRelay = 0;
const unsigned long T_SENSOR = 250;
const unsigned long T_RELAY = 100;

void saveCallback() { shouldSave = true; }

void triggerLcdMsg(String l1, String l2)
{
  showRelayMsg = true;
  msgStartTime = millis();
  relayLine1 = l1;
  relayLine2 = l2;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(l1);
  lcd.setCursor(0, 1);
  lcd.print(l2);
}

void setupWiFi()
{
  WiFiManager wm;
  wm.setSaveConfigCallback(saveCallback);

  prefs.begin("tuya_cfg", false);
  String s_id = prefs.getString("id", tuya_id);
  String s_key = prefs.getString("key", tuya_key);
  String s_ip = prefs.getString("ip", tuya_ip);
  String s_url = prefs.getString("url", api_url);
  String s_time = prefs.getString("time", time_url); // Load Time URL

  s_id.toCharArray(tuya_id, 40);
  s_key.toCharArray(tuya_key, 40);
  s_ip.toCharArray(tuya_ip, 20);
  s_url.toCharArray(api_url, 80);
  s_time.toCharArray(time_url, 80);

  WiFiManagerParameter p_id("id", "Device ID", tuya_id, 40);
  WiFiManagerParameter p_key("key", "Local Key", tuya_key, 40);
  WiFiManagerParameter p_ip("ip", "Tuya IP", tuya_ip, 20);
  WiFiManagerParameter p_url("url", "API Base URL", api_url, 80);
  WiFiManagerParameter p_time("time", "Time API URL", time_url, 80); // New Field

  wm.addParameter(&p_id);
  wm.addParameter(&p_key);
  wm.addParameter(&p_ip);
  wm.addParameter(&p_url);
  wm.addParameter(&p_time);

  if (!wm.autoConnect("ESP32_Config"))
    ESP.restart();

  if (shouldSave)
  {
    strcpy(tuya_id, p_id.getValue());
    strcpy(tuya_key, p_key.getValue());
    strcpy(tuya_ip, p_ip.getValue());
    strcpy(api_url, p_url.getValue());
    strcpy(time_url, p_time.getValue());

    prefs.putString("id", tuya_id);
    prefs.putString("key", tuya_key);
    prefs.putString("ip", tuya_ip);
    prefs.putString("url", api_url);
    prefs.putString("time", time_url);
    prefs.end();
  }
}

void syncTime()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  Serial.print("Syncing Time from: ");
  Serial.println(time_url);
  http.begin(time_url);
  int code = http.GET();

  if (code == 200)
  {
    String payload = http.getString();
    deserializeJson(doc, payload);

    // Expects format: {"timestamp": 1709999999}
    long ts = doc["timestamp"];

    if (ts > 1000000000)
    { // Basic validity check
      rtc.adjust(DateTime(ts));
      Serial.printf("RTC Synced! Unix: %ld\n", ts);
      lcd.clear();
      lcd.print("Time Synced!");
      delay(1000);
    }
    else
    {
      Serial.println("Invalid timestamp received");
    }
  }
  else
  {
    Serial.printf("Time Sync Failed: %d\n", code);
  }
  http.end();
}

void sendData(SensorData &d)
{
  if (WiFi.status() != WL_CONNECTED) return;

  DateTime now = rtc.now();
  char ts[25];
  sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  String u = String(api_url) + "/submit-data";
  http.begin(u);
  http.addHeader("Content-Type", "application/json");

  String j = "{\"ph\":" + String(d.ph, 2) + ",\"orp\":" + String(d.orp) +
             ",\"tds\":" + String(d.tds) + ",\"temp\":" + String(d.temp, 1) +
             ",\"timestamp\":\"" + String(ts) + "\"}";

  http.POST(j);
  http.end();
}

int getCmd(int id)
{
  if (WiFi.status() != WL_CONNECTED) return -1;
  http.begin(String(api_url) + "/fetch/" + String(id) + "/state");
  http.setTimeout(150);
  int code = http.GET();
  int cmd = -1;
  if (code == 200)
  {
    String p = http.getString();
    p.trim();
    if (p == "1" || p == "ON")
      cmd = 1;
    else if (p == "0" || p == "OFF")
      cmd = 0;
  }
  http.end();
  return cmd;
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Init...");
  if (!rtc.begin())
  {
    lcd.setCursor(0, 1);
    lcd.print("RTC Error!");
    Serial.println("RTC Missing");
  }
  for (int i = 0; i < RELAY_CNT; i++)
  {
    pinMode(R_PINS[i], OUTPUT);
    digitalWrite(R_PINS[i], LOW);
    last_r_state[i] = -1;
  }
  setupWiFi();
  syncTime();
  lcd.clear();
  lcd.print("Tuya Connect...");
  device = new TuyaDevice(tuya_id, tuya_ip, tuya_key, VER);
  device->connect();

  lcd.clear();
}

void loop()
{
  if (!device)
    return;
  unsigned long now = millis();

  // === LCD RESTORE LOGIC ===
  if (showRelayMsg && (now - msgStartTime > MSG_DURATION))
  {
    showRelayMsg = false;
    lcd.clear();
  }

  // === SENSOR TASK ===
  if (now - lastSensor >= T_SENSOR)
  {
    lastSensor = now;
    if (device->fetchStatus())
    {
      sendData(device->data);

      if (!showRelayMsg)
      {
        // Real-time sensor display
        lcd.setCursor(0, 0);
        lcd.printf("pH:%.2f T:%.1f ", device->data.ph, device->data.temp);
        lcd.setCursor(0, 1);
        lcd.printf("ORP:%d TDS:%d ", device->data.orp, device->data.tds);
      }
    }
    else
    {
      device->disconnect();
      device->connect();
    }
  }

  // === RELAY TASK ===
  if (now - lastRelay >= T_RELAY)
  {
    lastRelay = now;
    for (int i = 0; i < RELAY_CNT; i++)
    {
      int cmd = getCmd(R_IDS[i]);

      if (cmd != -1)
      {
        if (cmd != last_r_state[i])
        {
          digitalWrite(R_PINS[i], cmd ? HIGH : LOW);
          last_r_state[i] = cmd;

          // Show Popup on Change
          String s1 = "[RELAY CHANGE]";
          String s2 = "R" + String(R_IDS[i]) + " -> " + (cmd ? "ON" : "OFF");
          triggerLcdMsg(s1, s2);
        }
      }
    }
  }
}