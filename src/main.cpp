#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h> // Lib: LiquidCrystal_I2C
#include <RTClib.h>            // Lib: RTClib (Adafruit)
#include "TuyaDevice.h"

// ===== DEFAULTS =====
char tuya_id[40] = "a3658e534d4fad4173ijbi";
char tuya_key[40] = "jBM'#B$OGU/aN_Ud";
char tuya_ip[20] = "192.168.223.41";
char api_url[60] = "http://192.168.1.10:8000";
const float VER = 3.3f;

// ===== HARDWARE =====
const int RELAY_CNT = 8;
const int R_IDS[RELAY_CNT] = {2, 3, 4, 5, 6, 7, 8, 9};
const int R_PINS[RELAY_CNT] = {27, 26, 25, 33, 19, 18, 17, 16};

// Track relay states to detect changes for LCD
int last_r_state[RELAY_CNT];

// ===== OBJECTS =====
TuyaDevice *device = nullptr;
HTTPClient http;
Preferences prefs;
bool shouldSave = false;
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Address 0x27 is standard

// ===== LCD STATE =====
bool showRelayMsg = false;
unsigned long msgStartTime = 0;
const unsigned long MSG_DURATION = 3000; // Show relay msg for 3s
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

  s_id.toCharArray(tuya_id, 40);
  s_key.toCharArray(tuya_key, 40);
  s_ip.toCharArray(tuya_ip, 20);
  s_url.toCharArray(api_url, 60);

  WiFiManagerParameter p_id("id", "Device ID", tuya_id, 40);
  WiFiManagerParameter p_key("key", "Local Key", tuya_key, 40);
  WiFiManagerParameter p_ip("ip", "Tuya IP", tuya_ip, 20);
  WiFiManagerParameter p_url("url", "API URL", api_url, 60);

  wm.addParameter(&p_id);
  wm.addParameter(&p_key);
  wm.addParameter(&p_ip);
  wm.addParameter(&p_url);

  if (!wm.autoConnect("ESP32_Config"))
    ESP.restart();

  if (shouldSave)
  {
    strcpy(tuya_id, p_id.getValue());
    strcpy(tuya_key, p_key.getValue());
    strcpy(tuya_ip, p_ip.getValue());
    strcpy(api_url, p_url.getValue());
    prefs.putString("id", tuya_id);
    prefs.putString("key", tuya_key);
    prefs.putString("ip", tuya_ip);
    prefs.putString("url", api_url);
    prefs.end();
  }
}

void sendData(SensorData &d)
{
  if (WiFi.status() != WL_CONNECTED) return;

  // Get Time
  DateTime now = rtc.now();
  char ts[25];
  sprintf(ts, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  String u = String(api_url) + "/submit-data";
  http.begin(u);
  http.addHeader("Content-Type", "application/json");

  // JSON with Timestamp
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
    if (p == "1" || p == "ON")cmd = 1;
    else if (p == "0" || p == "OFF")cmd = 0;
  }
  http.end();
  return cmd;
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();

  // 1. Setup LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Init...");

  // 2. Setup RTC
  if (!rtc.begin())
  {
    Serial.println("RTC Missing");
    lcd.setCursor(0, 1);
    lcd.print("RTC Error!");
  }
  if (rtc.lostPower())
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // 3. Setup Relays
  for (int i = 0; i < RELAY_CNT; i++)
  {
    pinMode(R_PINS[i], OUTPUT);
    digitalWrite(R_PINS[i], LOW);
    last_r_state[i] = -1; // Init state unknown
  }

  setupWiFi();

  lcd.clear();
  lcd.setCursor(0, 0);
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
    bool success = device->fetchStatus();

    if (success)
    {
      sendData(device->data);

      if (!showRelayMsg)
      {
        // Layout:
        // pH: 7.00 T:25.0
        // ORP:750 TDS:120
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
          String s1 = "[RELAY CHANGE]";
          String s2 = "R" + String(R_IDS[i]) + " -> " + (cmd ? "ON" : "OFF");
          triggerLcdMsg(s1, s2);

          Serial.printf("Relay %d set to %d\n", R_IDS[i], cmd);
        }
      }
    }
  }
}