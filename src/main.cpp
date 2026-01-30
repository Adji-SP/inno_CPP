#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "TuyaDevice.h"

// ===== CONFIGURATION =====
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Tuya Sensor Config
const char* DEVICE_ID  = "a3658e534d4fad4173ijbi";
const char* LOCAL_KEY  = "jBM'#B$OGU/aN_Ud";
const char* TUYA_IP    = "192.168.223.41";
const float VERSION    = 3.3f; //Versi Tuya (bisa diganti sampai 3.5)

// API Config
const char* API_BASE_URL = "http://192.168.1.10:8000"; 
String API_SENSOR = "/submit-data";

// ===== HARDWARE MAPPING (FROM IMAGE) =====
const int RELAY_COUNT = 8;

// Mapping Relay dari PCB
const int RELAY_IDS[RELAY_COUNT]  = {2,  3,  4,  5,  6,  7,  8,  9}; 
const int RELAY_PINS[RELAY_COUNT] = {27, 26, 25, 33, 19, 18, 17, 16};

// =========================

unsigned long lastSensorTime = 0;
unsigned long lastRelayTime = 0;
const unsigned long SENSOR_INTERVAL = 250; 
const unsigned long RELAY_INTERVAL  = 100;

TuyaDevice device(DEVICE_ID, TUYA_IP, LOCAL_KEY, VERSION);
HTTPClient http;

void sendSensorData(SensorData& data) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    String url = String(API_BASE_URL) + API_SENSOR;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String json = "{\"ph\":" + String(data.ph, 2) +
                  ",\"orp\":" + String(data.orp) +
                  ",\"tds\":" + String(data.tds) +
                  ",\"temp\":" + String(data.temp, 1) +
                  ",\"turbidity\":0}";
    
    int code = http.POST(json);
    if (code > 0) {
        Serial.printf("[API] Data sent (Code: %d)\n", code);
    } else {
        Serial.printf("[API] Send failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();
}

int getRelayCommand(int relayId) {
    if (WiFi.status() != WL_CONNECTED) return -1;

    String url = String(API_BASE_URL) + "/fetch/" + String(relayId) + "/state";
    
    http.begin(url);
    int httpCode = http.GET();
    int command = -1;
    
    if (httpCode == 200) {
        String payload = http.getString();
        payload.trim(); 
        if (payload == "1" || payload == "true" || payload == "ON") command = 1;
        else if (payload == "0" || payload == "false" || payload == "OFF") command = 0;
    } else {
        Serial.printf("[API] Relay %d fetch failed (Code: %d)\n", relayId, httpCode);
    }
    http.end();
    return command;
}

void setup() {
    Serial.begin(115200);    
    Serial.println("Initializing Relays...");
    for (int i = 0; i < RELAY_COUNT; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], LOW); 
        Serial.printf(" - Mapped Relay ID %d to GPIO %d\n", RELAY_IDS[i], RELAY_PINS[i]);
    }

    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    Serial.println("Connecting to Tuya Sensor...");
    device.connect();
}

void loop() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastSensorTime >= SENSOR_INTERVAL) {
        lastSensorTime = currentMillis;

        if (device.fetchStatus()) {
            Serial.printf("[Sensor] pH: %.2f | ORP: %d | TDS: %d | Temp: %.1f\n",
                device.data.ph, device.data.orp, device.data.tds, device.data.temp);
            sendSensorData(device.data);
        } else {
            Serial.println("[Sensor] Read failed. Attempting reconnect...");
            device.disconnect();
            device.connect(); 
        }
    }

    if (currentMillis - lastRelayTime >= RELAY_INTERVAL) {
        lastRelayTime = currentMillis;

        for (int i = 0; i < RELAY_COUNT; i++) {
            int targetId = RELAY_IDS[i];
            int targetPin = RELAY_PINS[i];

            int command = getRelayCommand(targetId);
            
            if (command != -1) {
                digitalWrite(targetPin, command == 1 ? HIGH : LOW);
                Serial.printf("[Relay %d] Set to %s\n", targetId, command == 1 ? "ON" : "OFF");
            }
        }
    }
}