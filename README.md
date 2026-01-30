# INNOVILLAGE MICROCONTROLLER
This project is an ESP32-based solution for monitoring water quality parameters (pH, ORP, TDS, Temperature) via a Tuya smart device and controlling an 8-channel relay module. It features real-time data visualization on an LCD, WiFi provisioning via a captive portal, and data synchronization with a backend server.

## 🌟 Features

* **Tuya Device Integration**: Connects to a Tuya smart water monitor over local WiFi to fetch real-time sensor data (pH, ORP, TDS, Temperature).
* **8-Channel Relay Control**: Manages 8 physical relays based on commands fetched from a remote API.
* **Real-time Display**: 16x2 I2C LCD shows sensor readings and triggers popup notifications when relay states change.
* **WiFi Manager**: Configuration portal for easy WiFi setup without hardcoding credentials. It also allows configuring API endpoints and Tuya keys on the fly.
* **RTC Integration**: Uses a DS3231 Real-Time Clock to timestamp sensor data even if the internet is temporarily lost. The RTC syncs time automatically from the backend API on boot.
* **Persistent Settings**: Saves device configuration (IDs, Keys, URLs) in ESP32 flash memory using Preferences.

## 🛠️ Hardware Requirements

* **Microcontroller**: ESP32 Development Board (DOIT ESP32 DEVKIT V1 or similar).
* **Display**: 16x2 LCD with I2C Backpack (Address `0x27`).
* **Timekeeping**: DS3231 RTC Module (I2C).
* **Sensors**: Tuya-compatible Water Quality Monitor (supporting local Tuya protocol v3.3/3.4).
* **Actuators**: 8-Channel Relay Module (5V/12V depending on your setup).

### Pin Mapping

| Component | ESP32 Pin | Description |
| :--- | :--- | :--- |
| **I2C SDA** | GPIO 21 | LCD & RTC Data |
| **I2C SCL** | GPIO 22 | LCD & RTC Clock |
| **Relay 2** | GPIO 27 | Control Pin |
| **Relay 3** | GPIO 26 | Control Pin |
| **Relay 4** | GPIO 25 | Control Pin |
| **Relay 5** | GPIO 33 | Control Pin |
| **Relay 6** | GPIO 19 | Control Pin |
| **Relay 7** | GPIO 18 | Control Pin |
| **Relay 8** | GPIO 17 | Control Pin |
| **Relay 9** | GPIO 16 | Control Pin |

## 📦 Dependencies

This project uses **PlatformIO**. The following libraries are required and defined in `platformio.ini`:

* `bblanchon/ArduinoJson` (^7.0.0)
* `tzapu/WiFiManager` (^2.0.17)
* `marcoschwartz/LiquidCrystal_I2C` (^1.1.4)
* `adafruit/RTClib` (^2.1.3)

## 🚀 Installation & Setup

1.  **Clone the Repository**:
    ```bash
    git clone <repository-url>
    cd inno_CPP
    ```

2.  **Open in PlatformIO**:
    Open the project folder in VS Code with the PlatformIO extension installed.

3.  **Build and Upload**:
    Connect your ESP32 via USB and click the **Upload** button in PlatformIO.

4.  **Initial Configuration**:
    * On the first boot, the ESP32 will create a WiFi Hotspot named **`ESP32_Config`**.
    * Connect to this network with your phone or laptop.
    * A captive portal should open automatically (or visit `192.168.4.1`).
    * Enter your WiFi credentials, Tuya Device ID, Local Key, and Backend API URLs.
    * Click **Save**. The device will restart and connect to your network.

## 📡 API Endpoints

The system expects a backend server running at the address configured in the WiFi portal (default: `http://192.168.1.10:8000`).

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| **POST** | `/submit-data` | Receives JSON sensor data with timestamps. |
| **GET** | `/fetch/{id}/state` | Returns `1` (ON) or `0` (OFF) for the requested relay ID. |
| **GET** | `/time` | Returns `{"timestamp": <unix_epoch>}` for RTC synchronization. |

## 📂 Project Structure

* `src/main.cpp`: Main application logic (WiFi setup, loops, LCD control).
* `src/TuyaDevice.cpp`: Custom library implementation for handling Tuya Protocol v3.3 communication.
* `include/TuyaDevice.h`: Header file defining the Tuya device structure and encryption methods.
* `platformio.ini`: Project configuration and library dependencies.

## 📄 License

This project is open-source. Please ensure you have the necessary rights to the libraries used.
