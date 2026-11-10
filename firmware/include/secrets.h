#pragma once
#include <stdint.h>

/**
 * ============================================================================
 * CUT Smart Solar Tracker - ESP32 Credentials & Pin Mapping Configuration
 * Populate the fields below before flashing the ESP32.
 * ============================================================================
 */

// ----------------------------------------------------------------------------
// 1. WI-FI CREDENTIALS (Must be a 2.4 GHz network for ESP32)
// ----------------------------------------------------------------------------
#define SECRET_WIFI_SSID          "REPLACE_WITH_YOUR_WIFI_SSID"
#define SECRET_WIFI_PASSWORD      "REPLACE_WITH_YOUR_WIFI_PASSWORD"

// ----------------------------------------------------------------------------
// 2. MQTT BROKER SETTINGS
// ----------------------------------------------------------------------------
#define SECRET_MQTT_BROKER        "192.168.1.100"   // LAN IP of machine running Mosquitto
#define SECRET_MQTT_PORT          1883
#define SECRET_MQTT_CLIENT_ID     "cut-ks0530-esp32-tracker"
#define SECRET_MQTT_USERNAME      ""                // Leave empty if anonymous
#define SECRET_MQTT_PASSWORD      ""                // Leave empty if anonymous

// MQTT Topics
#define MQTT_TOPIC_TELEMETRY      "solar/telemetry"
#define MQTT_TOPIC_COMMAND        "solar/command"

// ----------------------------------------------------------------------------
// 3. ESP32 GPIO PIN MAPPINGS (Keyestudio KS0530 Kit)
// ----------------------------------------------------------------------------
// 4x LDR Photoresistors (Must use ESP32 ADC1 pins GPIO 32-39 while Wi-Fi is active)
constexpr uint8_t CFG_PIN_LDR_TOP_LEFT     = 34; // KS0530 A0 -> GPIO 34
constexpr uint8_t CFG_PIN_LDR_TOP_RIGHT    = 35; // KS0530 A1 -> GPIO 35
constexpr uint8_t CFG_PIN_LDR_BOTTOM_LEFT  = 32; // KS0530 A2 -> GPIO 32
constexpr uint8_t CFG_PIN_LDR_BOTTOM_RIGHT = 33; // KS0530 A3 -> GPIO 33

// Dual-Axis Servos (ESP32Servo PWM pins)
constexpr uint8_t CFG_PIN_SERVO_PAN        = 18; // Horizontal / Azimuth Servo (LR)
constexpr uint8_t CFG_PIN_SERVO_TILT       = 19; // Vertical / Elevation Servo (UD)

// Environmental Sensors
constexpr uint8_t CFG_PIN_DHT11            = 4;  // DHT11 Digital Signal Pin
constexpr uint8_t CFG_PIN_I2C_SDA          = 21; // BH1750 I2C SDA
constexpr uint8_t CFG_PIN_I2C_SCL          = 22; // BH1750 I2C SCL
