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
#include <LCD_I2C.h>
#include "secrets.h"

// ============================================================================
// 1. NETWORK & MQTT BROKER CONFIGURATION (LOADED FROM include/secrets.h)
// ============================================================================
const char* WIFI_SSID         = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD     = SECRET_WIFI_PASSWORD;

const char* MQTT_BROKER       = SECRET_MQTT_BROKER;
const uint16_t MQTT_PORT      = SECRET_MQTT_PORT;
const char* MQTT_CLIENT_ID    = SECRET_MQTT_CLIENT_ID;
const char* MQTT_USERNAME     = SECRET_MQTT_USERNAME;
const char* MQTT_PASSWORD     = SECRET_MQTT_PASSWORD;

const char* TOPIC_TELEMETRY   = MQTT_TOPIC_TELEMETRY;
const char* TOPIC_COMMAND     = MQTT_TOPIC_COMMAND;

// ============================================================================
// 2. ESP32 HARDWARE PIN MAPPINGS (LOADED FROM include/secrets.h)
// ============================================================================
// NOTE: Must use ESP32 ADC1 pins (GPIO 32-39) for LDR analog reads because
// ADC2 pins cannot be used simultaneously with Wi-Fi.
constexpr uint8_t PIN_LDR_RIGHT = CFG_PIN_LDR_RIGHT;
constexpr uint8_t PIN_LDR_LEFT = CFG_PIN_LDR_LEFT;
constexpr uint8_t PIN_LDR_TOP = CFG_PIN_LDR_TOP;
constexpr uint8_t PIN_LDR_BOTTOM = CFG_PIN_LDR_BOTTOM;

// Dual-Axis Micro Servos (PWM capable pins)
constexpr uint8_t PIN_SERVO_PAN        = CFG_PIN_SERVO_PAN;
constexpr uint8_t PIN_SERVO_TILT       = CFG_PIN_SERVO_TILT;

// Environmental Sensors
constexpr uint8_t PIN_DHT11            = CFG_PIN_DHT11;
constexpr uint8_t PIN_I2C_SDA          = CFG_PIN_I2C_SDA;
constexpr uint8_t PIN_I2C_SCL          = CFG_PIN_I2C_SCL;

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
constexpr unsigned long TRACKING_STEP_MS      = 300UL;    // Allow the mechanism to settle between samples

unsigned long lastTelemetryPublishMs = 0;
unsigned long lastManualCommandMs    = 0;
unsigned long lastTrackingStepMs     = 0;

// Keyestudio KS0530 Servo Angle State & Mechanical Limits
int servoPanAngle  = 90;  // Horizontal (Azimuth) [0..180]
int servoTiltAngle = 90;  // Vertical (Elevation) [10..170]

constexpr int SERVO_PAN_MIN  = 70;
constexpr int SERVO_PAN_MAX  = 110;
constexpr int SERVO_TILT_MIN = 70;
constexpr int SERVO_TILT_MAX = 110;

// Deadband tolerance for 12-bit ESP32 ADC (0-4095, ~4x Arduino Uno's 10-bit 0-1023)
constexpr int LDR_TOLERANCE = 180;
constexpr int TRACKING_CONFIRMATIONS = 3;
int panPendingDirection = 0, tiltPendingDirection = 0;
int panConfirmations = 0, tiltConfirmations = 0;
constexpr bool BRIGHTER_IS_HIGHER = true;
// Reverse the relevant sign if the mounted axis moves away from the light.
constexpr int PAN_DIRECTION = -1;
constexpr int TILT_DIRECTION = 1;
constexpr uint8_t PIN_BATTERY = 36;
constexpr uint8_t PIN_BUTTON = 2;
constexpr uint8_t PIN_BUZZER = 15;
constexpr uint8_t PIN_LED = 13;
float batteryVoltage = 0.0f;
bool localHold = false;
bool lastRawButton = false;
unsigned long buttonChangedMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastMqttAttemptMs = 0;
LCD_I2C lcd(0x27, 16, 2);

// Latest cached sensor readings
int ldrRight = 0;
int ldrLeft = 0;
int ldrTop = 0;
int ldrBottom = 0;
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
 * Reads Top (LCD side), Bottom (opposite), Left, and Right photoresistors.
 */
int readLdrAverage(uint8_t pin) {
    int total = 0;
    for (int i = 0; i < 8; ++i) total += analogRead(pin);
    return total / 8;
}

void readLdrSensors() {
    ldrRight = readLdrAverage(PIN_LDR_RIGHT);
    ldrLeft = readLdrAverage(PIN_LDR_LEFT);
    ldrTop = readLdrAverage(PIN_LDR_TOP);
    ldrBottom = readLdrAverage(PIN_LDR_BOTTOM);
}

// Require three consecutive errors outside the deadband in the same
// direction for EACH correction. Balanced or reversed readings reset it.
int confirmedTrackingStep(int error, int& pendingDirection, int& confirmations) {
    int direction = error > LDR_TOLERANCE ? 1 :
                    (error < -LDR_TOLERANCE ? -1 : 0);
    if (direction == 0) {
        pendingDirection = confirmations = 0;
        return 0;
    }
    if (direction != pendingDirection) {
        pendingDirection = direction;
        confirmations = 0;
    }
    if (++confirmations < TRACKING_CONFIRMATIONS) return 0;
    confirmations = 0;
    return direction;
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
 * Compares top vs. bottom and left vs. right LDR readings directly
 * and nudges the Pan (Horizontal) and Tilt (Vertical) servos toward the
 * brightest light source.
 */
void runKeyestudioAutoTracking() {
    readLdrSensors();

    // LCD reference: Top=GPIO32, Bottom=GPIO33, Left=GPIO35, Right=GPIO34.
    int diffVert  = ldrTop - ldrBottom;
    int diffHoriz = ldrLeft - ldrRight;

    if (!BRIGHTER_IS_HIGHER) {
        diffVert = -diffVert;
        diffHoriz = -diffHoriz;
    }
    int tiltStep = confirmedTrackingStep(diffVert, tiltPendingDirection, tiltConfirmations);
    int panStep = confirmedTrackingStep(diffHoriz, panPendingDirection, panConfirmations);
    int nextTilt = constrain(servoTiltAngle + tiltStep * TILT_DIRECTION,
                             SERVO_TILT_MIN, SERVO_TILT_MAX);
    int nextPan = constrain(servoPanAngle + panStep * PAN_DIRECTION,
                            SERVO_PAN_MIN, SERVO_PAN_MAX);
    if (nextTilt != servoTiltAngle) {
        servoTiltAngle = nextTilt;
        tiltServo.write(servoTiltAngle);
    }
    if (nextPan != servoPanAngle) {
        servoPanAngle = nextPan;
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
    if (localHold || digitalRead(PIN_BUTTON) == LOW) return; // Physical control takes priority.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.print(F("[MQTT] JSON parse error: "));
        Serial.println(err.c_str());
        return;
    }

    const char* action = doc["action"] | "TAKE_CONTROL";
    int step           = constrain(doc["step"] | 5, 1, 45);
    if (strcmp(action, "TAKE_CONTROL") && strcmp(action, "RELEASE") &&
        strcmp(action, "UP") && strcmp(action, "DOWN") &&
        strcmp(action, "LEFT") && strcmp(action, "RIGHT")) return;

    if (strcmp(action, "RELEASE") == 0) {
        currentMode = OperatingMode::AUTO;
        Serial.println(F("[STATE] Reverted to AUTO mode via RELEASE command."));
        return;
    }

    // Enter or refresh MANUAL mode and reset the 60-second watchdog timer
    currentMode          = OperatingMode::MANUAL;
    lastManualCommandMs  = millis();

    if (strcmp(action, "UP") == 0) {
        servoTiltAngle = constrain(servoTiltAngle + step * TILT_DIRECTION, SERVO_TILT_MIN, SERVO_TILT_MAX);
        tiltServo.write(servoTiltAngle);
    } else if (strcmp(action, "DOWN") == 0) {
        servoTiltAngle = constrain(servoTiltAngle - step * TILT_DIRECTION, SERVO_TILT_MIN, SERVO_TILT_MAX);
        tiltServo.write(servoTiltAngle);
    } else if (strcmp(action, "LEFT") == 0) {
        servoPanAngle = constrain(servoPanAngle + step * PAN_DIRECTION, SERVO_PAN_MIN, SERVO_PAN_MAX);
        panServo.write(servoPanAngle);
    } else if (strcmp(action, "RIGHT") == 0) {
        servoPanAngle = constrain(servoPanAngle - step * PAN_DIRECTION, SERVO_PAN_MIN, SERVO_PAN_MAX);
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
    JsonDocument doc;
    doc["ldr_right"] = ldrRight;
    doc["ldr_left"] = ldrLeft;
    doc["ldr_top"] = ldrTop;
    doc["ldr_bottom"] = ldrBottom;
    doc["servo_pan"]        = servoPanAngle;
    doc["servo_tilt"]       = servoTiltAngle;
    doc["temperature"]      = serialized(String(temperatureC, 1));
    doc["humidity"]         = serialized(String(humidityPct, 1));
    doc["lux"]              = serialized(String(lightLux, 1));
    doc["current_mode"]     = (currentMode == OperatingMode::MANUAL) ? "MANUAL" : "AUTO";

    doc["battery_voltage"] = batteryVoltage;
    doc["local_hold"] = localHold;

    char buffer[512];
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
        if (millis() - lastMqttAttemptMs < 5000UL) return;
        lastMqttAttemptMs = millis();
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
    pinMode(PIN_LDR_RIGHT, INPUT);
    pinMode(PIN_LDR_LEFT, INPUT);
    pinMode(PIN_LDR_TOP, INPUT);
    pinMode(PIN_LDR_BOTTOM, INPUT);

    pinMode(PIN_BATTERY, INPUT);
    analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    lcd.begin();
    lcd.backlight();
    lcd.clear();
    lcd.print("Keep hands clear");
    lcd.setCursor(0, 1);
    lcd.print("Moving in 3s");
    delay(3000);

    // Allocate hardware PWM timers for ESP32Servo and attach Pan/Tilt servos
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    panServo.setPeriodHertz(50);
    tiltServo.setPeriodHertz(50);
    panServo.attach(PIN_SERVO_PAN);
    tiltServo.attach(PIN_SERVO_TILT);

    panServo.write(servoPanAngle);
    tiltServo.write(servoTiltAngle);

    // Initialize DHT11 & I2C BH1750 sensors
    dht.begin();
    bh1750Ready = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
    if (!bh1750Ready) {
        Serial.println(F("[WARN] BH1750 not detected on I2C bus (0x23)."));
    }

    // Start Wi-Fi asynchronously; local controls work without a network.
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    espClient.setConnectionTimeout(250);

    // Configure MQTT client
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMqttMessage);
    mqttClient.setBufferSize(768);
    mqttClient.setSocketTimeout(1);
}

void lcdRow(uint8_t row, const char* text) {
    char padded[17];
    snprintf(padded, sizeof(padded), "%-16.16s", text);
    lcd.setCursor(0, row);
    lcd.print(padded);
}

void handleLocalButton(unsigned long now) {
    bool pressed = digitalRead(PIN_BUTTON) == LOW;
    if (pressed != lastRawButton) {
        lastRawButton = pressed;
        buttonChangedMs = now;
    }
    if (now - buttonChangedMs < 30 || pressed == localHold) return;
    localHold = pressed;
    digitalWrite(PIN_LED, localHold ? HIGH : LOW);
    currentMode = localHold ? OperatingMode::MANUAL : OperatingMode::AUTO;
    if (localHold) {
        servoPanAngle = servoTiltAngle = 90;
        panServo.write(90);
        tiltServo.write(90);
        tone(PIN_BUZZER, 1000, 150);
    }
}

void updateLocalDisplay(unsigned long now) {
    char line[17];
    switch ((now / 3000UL) % 3) {
        case 0:
            if (bh1750Ready) snprintf(line, sizeof(line), "Light:%.0f lx", lightLux);
            else snprintf(line, sizeof(line), "BH1750 error");
            break;
        case 1: snprintf(line, sizeof(line), "Battery:%.2fV", batteryVoltage); break;
        default: snprintf(line, sizeof(line), "T:%.1fC H:%.0f%%", temperatureC, humidityPct);
    }
    lcdRow(0, line);
    snprintf(line, sizeof(line), "%s P:%d V:%d", localHold ? "HOLD" :
             (currentMode == OperatingMode::AUTO ? "AUTO" : "MAN"), servoPanAngle, servoTiltAngle);
    lcdRow(1, line);
}

void loop() {
    unsigned long now = millis();
    handleLocalButton(now);
    if (localHold || currentMode == OperatingMode::MANUAL) {
        panPendingDirection = tiltPendingDirection = 0;
        panConfirmations = tiltConfirmations = 0;
        lastTrackingStepMs = now;
    }
    if (!localHold && currentMode == OperatingMode::MANUAL &&
        now - lastManualCommandMs >= MANUAL_TIMEOUT_MS) currentMode = OperatingMode::AUTO;

    if (!localHold && currentMode == OperatingMode::AUTO &&
        now - lastTrackingStepMs >= TRACKING_STEP_MS) {
        lastTrackingStepMs = now;
        runKeyestudioAutoTracking();
    }
    // Sample locally even when the broker is unavailable.
    if (now - lastTelemetryPublishMs >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryPublishMs = now;
        readLdrSensors();
        readEnvironmentalSensors();
        uint32_t millivolts = 0;
        for (int i = 0; i < 20; ++i) millivolts += analogReadMilliVolts(PIN_BATTERY);
        batteryVoltage = millivolts / 20.0f * 2.0f / 1000.0f;
        Serial.printf("Top:%d Bottom:%d Left:%d Right:%d | Pan:%d Tilt:%d | Battery:%.2fV Lux:%.1f\n",
                      ldrTop, ldrBottom, ldrLeft, ldrRight, servoPanAngle, servoTiltAngle,
                      batteryVoltage, lightLux);
        if (mqttClient.connected()) publishTelemetry();
    }
    if (now - lastDisplayMs >= 1000UL) {
        lastDisplayMs = now;
        updateLocalDisplay(now);
    }
    // Bounded, spaced retries rather than a connection attempt every loop.
    ensureMqttConnected();
    mqttClient.loop();
    delay(2);
}
