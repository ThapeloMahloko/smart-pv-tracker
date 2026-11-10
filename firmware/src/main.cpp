/**
 * ============================================================================
 * Central University of Technology (CUT) - Smart Solar Tracking System
 * Hardware: Keyestudio KS0530 DIY Solar Tracking Kit driven by ESP32
 * Baseline Reference: Keyestudio KS0530 Lesson 19 (Solar Panel Device Functions)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <BH1750.h>
#include <ArduinoJson.h>

// ============================================================================
// 1. NETWORK & MQTT BROKER CONFIGURATION (PLACEHOLDERS)
// ============================================================================
const char* WIFI_SSID         = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD     = "YOUR_WIFI_PASSWORD";

const char* MQTT_BROKER       = "192.168.1.100";
const uint16_t MQTT_PORT      = 1883;
const char* MQTT_CLIENT_ID    = "cut-ks0530-esp32-tracker";
const char* MQTT_USERNAME     = ""; // Optional
const char* MQTT_PASSWORD     = ""; // Optional

const char* TOPIC_TELEMETRY   = "solar/telemetry";
const char* TOPIC_COMMAND     = "solar/command";

// ============================================================================
// 2. ESP32 HARDWARE PIN MAPPINGS (KEYESTUDIO KS0530 ADAPTATION)
// ============================================================================
// NOTE: Must use ESP32 ADC1 pins (GPIO 32-39) for LDR analog reads because
// ADC2 pins cannot be used simultaneously with Wi-Fi.
constexpr uint8_t PIN_LDR_TOP_LEFT     = 34; // KS0530 A0 -> ESP32 GPIO34 (ADC1_CH6)
constexpr uint8_t PIN_LDR_TOP_RIGHT    = 35; // KS0530 A1 -> ESP32 GPIO35 (ADC1_CH7)
constexpr uint8_t PIN_LDR_BOTTOM_LEFT  = 32; // KS0530 A2 -> ESP32 GPIO32 (ADC1_CH4)
constexpr uint8_t PIN_LDR_BOTTOM_RIGHT = 33; // KS0530 A3 -> ESP32 GPIO33 (ADC1_CH5)

// Dual-Axis Micro Servos (PWM capable pins)
constexpr uint8_t PIN_SERVO_PAN        = 18; // Horizontal / Azimuth Servo (LR)
constexpr uint8_t PIN_SERVO_TILT       = 19; // Vertical / Elevation Servo (UD)

// Environmental Sensors
constexpr uint8_t PIN_DHT11            = 4;  // DHT11 Single-Bus Digital Pin
constexpr uint8_t PIN_I2C_SDA          = 21; // BH1750 & LCD1602 I2C SDA
constexpr uint8_t PIN_I2C_SCL          = 22; // BH1750 & LCD1602 I2C SCL

// ============================================================================
// 3. TRACKING & STATE MACHINE CONSTANTS
// ============================================================================
enum class OperatingMode {
    AUTO,
    MANUAL
};

OperatingMode currentMode = OperatingMode::AUTO;

// Timers (milliseconds)
constexpr unsigned long TELEMETRY_INTERVAL_MS = 2000UL;  // Publish every 2 seconds
constexpr unsigned long MANUAL_TIMEOUT_MS     = 60000UL; // 60-second watchdog timeout
constexpr unsigned long TRACKING_STEP_MS      = 50UL;    // Servo update cadence in AUTO

unsigned long lastTelemetryPublishMs = 0;
unsigned long lastManualCommandMs    = 0;
unsigned long lastTrackingStepMs     = 0;

// Keyestudio KS0530 Servo Angle State & Mechanical Limits
int servoPanAngle  = 90;  // Horizontal (Azimuth) [0..180]
int servoTiltAngle = 90;  // Vertical (Elevation) [10..170]

constexpr int SERVO_PAN_MIN  = 5;
constexpr int SERVO_PAN_MAX  = 175;
constexpr int SERVO_TILT_MIN = 15;
constexpr int SERVO_TILT_MAX = 165;

// Deadband tolerance for 12-bit ESP32 ADC (0-4095, ~4x Arduino Uno's 10-bit 0-1023)
constexpr int LDR_TOLERANCE  = 80;

// Latest cached sensor readings
int ldrTopLeft     = 0;
int ldrTopRight    = 0;
int ldrBottomLeft  = 0;
int ldrBottomRight = 0;
float temperatureC = 0.0f;
float humidityPct  = 0.0f;
float lightLux     = 0.0f;

// ============================================================================
// 4. PERIPHERAL INSTANCES
// ============================================================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

Servo panServo;
Servo tiltServo;

#define DHTTYPE DHT11
DHT dht(PIN_DHT11, DHTTYPE);
BH1750 lightMeter(0x23);
bool bh1750Ready = false;

// ============================================================================
// 5. HELPER FUNCTIONS: SENSORS & KEYESTUDIO KS0530 TRACKING LOGIC
// ============================================================================

/**
 * Reads the 4 quadrant photoresistors (LDRs).
 */
void readLdrSensors() {
    ldrTopLeft     = analogRead(PIN_LDR_TOP_LEFT);
    ldrTopRight    = analogRead(PIN_LDR_TOP_RIGHT);
    ldrBottomLeft  = analogRead(PIN_LDR_BOTTOM_LEFT);
    ldrBottomRight = analogRead(PIN_LDR_BOTTOM_RIGHT);
}

/**
 * Reads DHT11 (OneWire/Single-bus) and BH1750 (I2C) environmental sensors.
 */
void readEnvironmentalSensors() {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) {
        temperatureC = t;
    }
    if (!isnan(h)) {
        humidityPct = h;
    }

    if (bh1750Ready && lightMeter.measurementReady()) {
        float lx = lightMeter.readLightLevel();
        if (lx >= 0.0f) {
            lightLux = lx;
        }
    }
}

/**
 * Keyestudio KS0530 Lesson 19 Dual-Axis Light Tracking Algorithm
 * Compares average top vs. bottom and average left vs. right LDR readings
 * and nudges the Pan (Horizontal) and Tilt (Vertical) servos toward the
 * brightest light source.
 */
void runKeyestudioAutoTracking() {
    readLdrSensors();

    // Calculate quadrant averages per Keyestudio Lesson 19 reference
    int avgTop    = (ldrTopLeft + ldrTopRight) / 2;
    int avgBottom = (ldrBottomLeft + ldrBottomRight) / 2;
    int avgLeft   = (ldrTopLeft + ldrBottomLeft) / 2;
    int avgRight  = (ldrTopRight + ldrBottomRight) / 2;

    int diffVert  = avgTop - avgBottom;
    int diffHoriz = avgLeft - avgRight;

    // Adjust Vertical (Tilt) Servo if vertical difference exceeds deadband
    if (abs(diffVert) > LDR_TOLERANCE) {
        if (avgTop > avgBottom) {
            servoTiltAngle = constrain(servoTiltAngle + 1, SERVO_TILT_MIN, SERVO_TILT_MAX);
        } else {
            servoTiltAngle = constrain(servoTiltAngle - 1, SERVO_TILT_MIN, SERVO_TILT_MAX);
        }
        tiltServo.write(servoTiltAngle);
    }

    // Adjust Horizontal (Pan) Servo if horizontal difference exceeds deadband
    if (abs(diffHoriz) > LDR_TOLERANCE) {
        if (avgLeft > avgRight) {
            servoPanAngle = constrain(servoPanAngle + 1, SERVO_PAN_MIN, SERVO_PAN_MAX);
        } else {
            servoPanAngle = constrain(servoPanAngle - 1, SERVO_PAN_MIN, SERVO_PAN_MAX);
        }
        panServo.write(servoPanAngle);
    }
}

// ============================================================================
// 6. MQTT CALLBACK & TELEMETRY PUBLISHER
// ============================================================================

/**
 * Handles incoming JSON commands on "solar/command":
 * Example payload: {"mode": "MANUAL", "action": "UP", "step": 5}
 */
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.print(F("[MQTT] JSON parse error: "));
        Serial.println(err.c_str());
        return;
    }

    const char* action = doc["action"] | "TAKE_CONTROL";
    int step           = doc["step"] | 5;

    if (strcmp(action, "RELEASE") == 0) {
        currentMode = OperatingMode::AUTO;
        Serial.println(F("[STATE] Reverted to AUTO mode via RELEASE command."));
        return;
    }

    // Enter or refresh MANUAL mode and reset the 60-second watchdog timer
    currentMode          = OperatingMode::MANUAL;
    lastManualCommandMs  = millis();

    if (strcmp(action, "UP") == 0) {
        servoTiltAngle = constrain(servoTiltAngle + step, SERVO_TILT_MIN, SERVO_TILT_MAX);
        tiltServo.write(servoTiltAngle);
    } else if (strcmp(action, "DOWN") == 0) {
        servoTiltAngle = constrain(servoTiltAngle - step, SERVO_TILT_MIN, SERVO_TILT_MAX);
        tiltServo.write(servoTiltAngle);
    } else if (strcmp(action, "LEFT") == 0) {
        servoPanAngle = constrain(servoPanAngle + step, SERVO_PAN_MIN, SERVO_PAN_MAX);
        panServo.write(servoPanAngle);
    } else if (strcmp(action, "RIGHT") == 0) {
        servoPanAngle = constrain(servoPanAngle - step, SERVO_PAN_MIN, SERVO_PAN_MAX);
        panServo.write(servoPanAngle);
    }

    Serial.printf(
        "[MANUAL] Action=%s | Pan=%d° | Tilt=%d° | Watchdog reset (60s)\n",
        action,
        servoPanAngle,
        servoTiltAngle
    );
}

/**
 * Publishes all sensor and state telemetry to "solar/telemetry".
 */
void publishTelemetry() {
    readLdrSensors();
    readEnvironmentalSensors();

    JsonDocument doc;
    doc["ldr_top_left"]     = ldrTopLeft;
    doc["ldr_top_right"]    = ldrTopRight;
    doc["ldr_bottom_left"]  = ldrBottomLeft;
    doc["ldr_bottom_right"] = ldrBottomRight;
    doc["servo_pan"]        = servoPanAngle;
    doc["servo_tilt"]       = servoTiltAngle;
    doc["temperature"]      = serialized(String(temperatureC, 1));
    doc["humidity"]         = serialized(String(humidityPct, 1));
    doc["lux"]              = serialized(String(lightLux, 1));
    doc["current_mode"]     = (currentMode == OperatingMode::MANUAL) ? "MANUAL" : "AUTO";

    char buffer[320];
    size_t n = serializeJson(doc, buffer, sizeof(buffer));
    mqttClient.publish(TOPIC_TELEMETRY, buffer, n);
}

/**
 * Ensures Wi-Fi and MQTT connections remain active.
 */
void ensureMqttConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    if (!mqttClient.connected()) {
        Serial.print(F("[MQTT] Connecting to broker... "));
        bool connected = false;
        if (strlen(MQTT_USERNAME) > 0) {
            connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
        } else {
            connected = mqttClient.connect(MQTT_CLIENT_ID);
        }

        if (connected) {
            Serial.println(F("connected!"));
            mqttClient.subscribe(TOPIC_COMMAND);
        } else {
            Serial.printf("failed (rc=%d)\n", mqttClient.state());
        }
    }
}

// ============================================================================
// 7. ARDUINO SETUP & MAIN LOOP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(F("\n=== CUT Smart Solar Tracker (Keyestudio KS0530 + ESP32) ==="));

    // Configure ESP32 12-bit ADC resolution for the 4 LDR pins
    analogReadResolution(12);
    pinMode(PIN_LDR_TOP_LEFT, INPUT);
    pinMode(PIN_LDR_TOP_RIGHT, INPUT);
    pinMode(PIN_LDR_BOTTOM_LEFT, INPUT);
    pinMode(PIN_LDR_BOTTOM_RIGHT, INPUT);

    // Allocate hardware PWM timers for ESP32Servo and attach Pan/Tilt servos
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    panServo.setPeriodHertz(50);
    tiltServo.setPeriodHertz(50);
    panServo.attach(PIN_SERVO_PAN, 500, 2400);
    tiltServo.attach(PIN_SERVO_TILT, 500, 2400);

    panServo.write(servoPanAngle);
    tiltServo.write(servoTiltAngle);

    // Initialize DHT11 & I2C BH1750 sensors
    dht.begin();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    bh1750Ready = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
    if (!bh1750Ready) {
        Serial.println(F("[WARN] BH1750 not detected on I2C bus (0x23)."));
    }

    // Connect to Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 10000UL) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();

    // Configure MQTT client
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMqttMessage);
    mqttClient.setBufferSize(512);
}

void loop() {
    ensureMqttConnected();
    mqttClient.loop();

    const unsigned long now = millis();

    // State Machine: AUTO vs. MANUAL
    if (currentMode == OperatingMode::MANUAL) {
        // 60-second inactivity watchdog timer
        if (now - lastManualCommandMs >= MANUAL_TIMEOUT_MS) {
            currentMode = OperatingMode::AUTO;
            Serial.println(F("[WATCHDOG] 60s elapsed without MQTT command -> reverting to AUTO mode."));
        }
    } else {
        // AUTO mode: run Keyestudio LDR tracking loop at regular cadence
        if (now - lastTrackingStepMs >= TRACKING_STEP_MS) {
            lastTrackingStepMs = now;
            runKeyestudioAutoTracking();
        }
    }

    // Publish full telemetry every 2 seconds (active in both AUTO and MANUAL so UI stays live)
    if (now - lastTelemetryPublishMs >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryPublishMs = now;
        publishTelemetry();
    }
}
