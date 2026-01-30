#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <mbedtls/aes.h>
#include <ArduinoJson.h>

struct SensorData {
    float ph = 0;
    int orp = 0;
    int tds = 0;
    float temp = 0;
};

class TuyaDevice {
public:
    SensorData data;

    // version: 3.3 (most common) or 3.4/3.5 (newer devices)
    TuyaDevice(const char* deviceId, const char* ip, const char* localKey, float version = 3.3f);
    
    bool connect();
    void disconnect();
    bool isConnected();
    bool fetchStatus();

private:
    const char* _deviceId;
    const char* _ip;
    const char* _localKey;
    float _version;
    WiFiClient _client;
    uint32_t _seqNo = 1;

    bool _sendQuery();
    String _readResponse();
    void _parseDps(JsonObject& dps);
    
    // Returns raw bytes (caller must delete[])
    uint8_t* _encrypt(const String& data, size_t& outLen);
    String _decrypt(uint8_t* data, size_t len);
};