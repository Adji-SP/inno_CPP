# ESP32 Water Quality Monitor

ESP32-based system that reads water quality data from a Tuya sensor and controls relays via a REST API.

## Features

- **Tuya Integration** - Reads pH, ORP, TDS, Temperature from Tuya-compatible water sensor
- **WiFiManager** - Easy WiFi setup via captive portal (no hardcoded credentials)
- **8-Channel Relay Control** - Remotely controlled via API
- **LCD Display** - Real-time sensor readings on 16x2 I2C LCD
- **RTC Timekeeping** - DS3231 for accurate timestamps
- **REST API** - Sends data to backend, fetches relay commands

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | ESP32 DevKit |
| Sensor | Tuya Water Quality (WiFi) |
| Display | 16x2 LCD with I2C (0x27) |
| RTC | DS3231 |
| Relays | 8-channel module |

### Wiring

```
ESP32          Component
─────          ─────────
GPIO 21 (SDA)  → LCD SDA, RTC SDA
GPIO 22 (SCL)  → LCD SCL, RTC SCL
3.3V           → LCD VCC, RTC VCC
GND            → LCD GND, RTC GND

GPIO 27        → Relay 1 (ID: 2)
GPIO 26        → Relay 2 (ID: 3)
GPIO 25        → Relay 3 (ID: 4)
GPIO 33        → Relay 4 (ID: 5)
GPIO 19        → Relay 5 (ID: 6)
GPIO 18        → Relay 6 (ID: 7)
GPIO 17        → Relay 7 (ID: 8)
GPIO 16        → Relay 8 (ID: 9)
```

## Software Dependencies

Defined in `platformio.ini`:

```ini
lib_deps = 
    bblanchon/ArduinoJson@^7.0.0
    tzapu/WiFiManager@^2.0.17
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
    adafruit/RTClib@^2.1.3
```

## Setup

### 1. Flash the ESP32

```bash
pio run -t upload
pio device monitor
```

### 2. Configure via WiFi

On first boot (or if WiFi fails):

1. ESP32 creates AP: **`ESP32_Config`**
2. Connect to it with phone/laptop
3. Open `192.168.4.1` in browser
4. Fill in:

| Field | Description | Example |
|-------|-------------|---------|
| SSID | Your WiFi network | `MyWiFi` |
| Password | WiFi password | `********` |
| Device ID | Tuya device ID | `a3658e534d4fad4173ijbi` |
| Local Key | Tuya local key (16 chars) | `jBM'#B$OGU/aN_Ud` |
| Tuya IP | Sensor's local IP | `192.168.1.100` |
| API Base URL | Your backend server | `http://192.168.1.10:8000` |
| Time API URL | Time sync endpoint | `http://192.168.1.10:8000/time` |

### 3. Get Tuya Credentials

You need the **Device ID** and **Local Key** from Tuya:

**Option A: Using tinytuya (Python)**
```bash
pip install tinytuya
python -m tinytuya wizard
```

**Option B: Tuya IoT Platform**
1. Go to [iot.tuya.com](https://iot.tuya.com)
2. Create project, link your Tuya app
3. Find device → Get Device ID and Local Key

## API Endpoints

Your backend server should implement:

### POST `/submit-data`

Receives sensor readings.

**Request:**
```json
{
  "ph": 7.02,
  "orp": 400,
  "tds": 150,
  "temp": 25.5,
  "timestamp": "2025-01-30 10:30:00"
}
```

### GET `/time`

Returns current Unix timestamp for RTC sync.

**Response:**
```json
{
  "timestamp": 1738236600
}
```

### GET `/fetch/{relay_id}/state`

Returns relay state command.

**Response:**
```
1
```
or
```
0
```

| Relay ID | GPIO Pin |
|----------|----------|
| 2 | 27 |
| 3 | 26 |
| 4 | 25 |
| 5 | 33 |
| 6 | 19 |
| 7 | 18 |
| 8 | 17 |
| 9 | 16 |

## Tuya Protocol

The sensor uses Tuya local protocol v3.3. Data points (DPS):

| DPS ID | Parameter | Raw → Actual |
|--------|-----------|--------------|
| 8 | Temperature | ÷ 10 → °C |
| 106 | pH | ÷ 100 → pH |
| 111 | TDS | raw → ppm |
| 131 | ORP | raw → mV |

## LCD Display

**Normal Mode:**
```
┌────────────────┐
│pH:7.02 T:25.5  │
│ORP:400 TDS:150 │
└────────────────┘
```

**Relay Change (3 sec popup):**
```
┌────────────────┐
│[RELAY CHANGE]  │
│R2 -> ON        │
└────────────────┘
```

## Timing

| Task | Interval |
|------|----------|
| Sensor poll | 250ms |
| Relay check | 100ms |
| LCD message | 3000ms (popup duration) |

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Tuya Connect..." stuck | Check Tuya IP, ensure sensor is on same network |
| No sensor data | Verify Device ID and Local Key |
| RTC Error | Check I2C wiring (SDA/SCL) |
| Relays not responding | Check API URL, verify `/fetch/{id}/state` returns `0` or `1` |
| WiFi won't connect | Hold reset to re-enter config portal |

### Reset Configuration

To clear saved settings and re-enter WiFi setup:

```cpp
// Add to setup() temporarily:
prefs.begin("tuya_cfg", false);
prefs.clear();
prefs.end();
```

## Project Structure

```
├── include/
│   └── TuyaDevice.h       # Tuya protocol class
├── src/
│   ├── main.cpp           # Main application
│   └── TuyaDevice.cpp     # Tuya implementation
├── platformio.ini         # PlatformIO config
└── README.md
```

## License

MIT
