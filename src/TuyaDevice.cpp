#include "TuyaDevice.h"

#define TUYA_PORT 6668

TuyaDevice::TuyaDevice(const char* deviceId, const char* ip, const char* localKey, float version)
    : _deviceId(deviceId), _ip(ip), _localKey(localKey), _version(version) {}

bool TuyaDevice::connect() {
    Serial.printf("Connecting to %s (v%.1f)...\n", _ip, _version);
    if (_client.connect(_ip, TUYA_PORT)) {
        Serial.println("Connected!");
        return true;
    }
    Serial.println("Connection failed!");
    return false;
}

void TuyaDevice::disconnect() {
    _client.stop();
}

bool TuyaDevice::isConnected() {
    return _client.connected();
}

bool TuyaDevice::fetchStatus() {
    if (!isConnected() && !connect()) return false;
    if (!_sendQuery()) return false;
    
    String response = _readResponse();
    if (response.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, response)) return false;
    if (!doc.containsKey("dps")) return false;

    JsonObject dps = doc["dps"];
    _parseDps(dps);
    return true;
}

void TuyaDevice::_parseDps(JsonObject& dps) {
    if (dps.containsKey("106")) data.ph = dps["106"].as<float>() / 100.0f;
    if (dps.containsKey("131")) data.orp = dps["131"].as<int>();
    if (dps.containsKey("111")) data.tds = dps["111"].as<int>();
    if (dps.containsKey("8"))   data.temp = dps["8"].as<float>() / 10.0f;
}

bool TuyaDevice::_sendQuery() {
    // 1. Build JSON payload
    JsonDocument doc;
    doc["gwId"] = _deviceId;
    doc["devId"] = _deviceId;
    doc["uid"] = _deviceId;
    doc["t"] = String(millis() / 1000);
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // 2. Encrypt (raw bytes, no hex conversion)
    size_t encLen = 0;
    uint8_t* encData = _encrypt(jsonPayload, encLen);
    if (!encData) return false;

    // 3. Calculate packet size
    // v3.3: Header(4) + Seq(4) + Cmd(4) + Len(4) + Data + CRC(4) + Footer(4) = 24 + data
    // v3.4+: adds 16-byte version header after Cmd
    size_t extraHeader = (_version >= 3.4f) ? 16 : 0;
    size_t packetSize = 24 + extraHeader + encLen;
    uint8_t* buffer = new uint8_t[packetSize];
    size_t idx = 0;

    // -- Prefix --
    buffer[idx++] = 0x00; buffer[idx++] = 0x00;
    buffer[idx++] = 0x55; buffer[idx++] = 0xAA;

    // -- Sequence --
    buffer[idx++] = (_seqNo >> 24) & 0xFF;
    buffer[idx++] = (_seqNo >> 16) & 0xFF;
    buffer[idx++] = (_seqNo >> 8) & 0xFF;
    buffer[idx++] = _seqNo & 0xFF;
    _seqNo++;

    // -- Command --
    // v3.3: 0x0A (DP_QUERY)
    // v3.4+: 0x10 (DP_QUERY_NEW)
    uint8_t cmd = (_version >= 3.4f) ? 0x10 : 0x0A;
    buffer[idx++] = 0x00; buffer[idx++] = 0x00;
    buffer[idx++] = 0x00; buffer[idx++] = cmd;

    // -- Version header (v3.4+ only) --
    if (_version >= 3.4f) {
        buffer[idx++] = '3';
        buffer[idx++] = '.';
        buffer[idx++] = (_version >= 3.5f) ? '5' : '4';
        buffer[idx++] = 0x00;
        memset(buffer + idx, 0, 12);
        idx += 12;
    }

    // -- Length (data + CRC + footer) --
    size_t payloadLen = encLen + 8;
    buffer[idx++] = (payloadLen >> 24) & 0xFF;
    buffer[idx++] = (payloadLen >> 16) & 0xFF;
    buffer[idx++] = (payloadLen >> 8) & 0xFF;
    buffer[idx++] = payloadLen & 0xFF;

    // -- Encrypted data --
    memcpy(buffer + idx, encData, encLen);
    idx += encLen;
    delete[] encData;

    // -- CRC32 --
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < idx; i++) {
        crc ^= buffer[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    crc = ~crc;
    buffer[idx++] = (crc >> 24) & 0xFF;
    buffer[idx++] = (crc >> 16) & 0xFF;
    buffer[idx++] = (crc >> 8) & 0xFF;
    buffer[idx++] = crc & 0xFF;

    // -- Suffix --
    buffer[idx++] = 0x00; buffer[idx++] = 0x00;
    buffer[idx++] = 0xAA; buffer[idx++] = 0x55;

    // 4. Send
    size_t written = _client.write(buffer, idx);
    delete[] buffer;
    return written == idx;
}

String TuyaDevice::_readResponse() {
    unsigned long start = millis();
    while (!_client.available() && millis() - start < 5000) delay(10);
    if (!_client.available()) return "";

    // 1. ALWAYS read standard 16-byte header first
    //    [Prefix 4] [Seq 4] [Cmd 4] [Len 4]
    uint8_t header[16];
    if (_client.readBytes(header, 16) != 16) return "";

    // 2. Verify prefix
    if (header[0] != 0x00 || header[1] != 0x00 || 
        header[2] != 0x55 || header[3] != 0xAA) {
        return "";
    }

    // 3. Get payload length (bytes 12-15, big endian)
    uint32_t payloadLen = (header[12] << 24) | (header[13] << 16) | 
                          (header[14] << 8) | header[15];

    if (payloadLen == 0 || payloadLen > 2048) return "";

    // 4. Read exact payload size
    uint8_t* payload = new uint8_t[payloadLen];
    if (_client.readBytes(payload, payloadLen) != payloadLen) {
        delete[] payload;
        return "";
    }

    // 5. Calculate offsets based on version
    //    v3.3: [Encrypted Data] + [CRC 4] + [Footer 4]
    //    v3.4: [RetCode 4] + [Encrypted Data] + [CRC 4] + [Footer 4]
    size_t retCodeOffset = 0;
    if (_version >= 3.4f) {
        if (payloadLen < 12) { delete[] payload; return ""; }
        retCodeOffset = 4;  // Skip return code
    } else {
        if (payloadLen < 8) { delete[] payload; return ""; }
    }

    // 6. Encrypted data length = total - retcode - CRC - footer
    size_t encLen = payloadLen - retCodeOffset - 8;

    // 7. Decrypt
    String result = _decrypt(payload + retCodeOffset, encLen);
    delete[] payload;
    return result;
}

// Optimized: returns raw bytes instead of hex string
uint8_t* TuyaDevice::_encrypt(const String& data, size_t& outLen) {
    size_t len = data.length();
    size_t padLen = ((len / 16) + 1) * 16;
    outLen = padLen;

    // PKCS7 padding
    uint8_t* input = new uint8_t[padLen];
    memcpy(input, data.c_str(), len);
    uint8_t padVal = padLen - len;
    memset(input + len, padVal, padVal);

    // Encrypt
    uint8_t* output = new uint8_t[padLen];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (uint8_t*)_localKey, 128);

    for (size_t i = 0; i < padLen; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
    }
    
    mbedtls_aes_free(&aes);
    delete[] input;
    return output;
}

String TuyaDevice::_decrypt(uint8_t* data, size_t len) {
    // Check if maybe unencrypted (look for JSON)
    if (len > 0 && (data[0] == '{' || memchr(data, '{', len))) {
        String raw((char*)data, len);
        int idx = raw.indexOf('{');
        if (idx >= 0) return raw.substring(idx);
    }

    // Must be multiple of 16 for AES
    if (len == 0 || len % 16 != 0) return "";

    uint8_t* output = new uint8_t[len + 1];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, (uint8_t*)_localKey, 128);

    for (size_t i = 0; i < len; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data + i, output + i);
    }
    mbedtls_aes_free(&aes);

    // Remove PKCS7 padding
    uint8_t padVal = output[len - 1];
    if (padVal > 0 && padVal <= 16) len -= padVal;
    output[len] = '\0';

    String result((char*)output);
    delete[] output;
    return result;
}