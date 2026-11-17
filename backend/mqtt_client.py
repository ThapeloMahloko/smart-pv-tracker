import json
import logging
import math
import os
from uuid import uuid4
import paho.mqtt.client as mqtt
from database import SessionLocal
from models import SolarTelemetry, TrackerMode

logger = logging.getLogger("cut_solar.mqtt")

MQTT_BROKER = os.getenv("MQTT_BROKER", "broker.hivemq.com")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")

TOPIC_TELEMETRY = os.getenv("MQTT_TOPIC_TELEMETRY", "cut-solar/b774380d864e/telemetry")
TOPIC_COMMAND = os.getenv("MQTT_TOPIC_COMMAND", "cut-solar/b774380d864e/command")


class SolarMQTTManager:
    """Manages the background paho-mqtt client loop for telemetry ingestion and command publishing."""

    def __init__(self) -> None:
        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=os.getenv("MQTT_CLIENT_ID", "cut-backend-" + uuid4().hex[:12]),
        )
        if MQTT_USERNAME:
            self.client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if reason_code == 0:
            logger.info("Connected to MQTT Broker at %s:%s", MQTT_BROKER, MQTT_PORT)
            client.subscribe(TOPIC_TELEMETRY, qos=1)
            client.subscribe("solar/telemetry", qos=1)
            client.subscribe("cut-solar/+/telemetry", qos=1)
            logger.info("Subscribed to telemetry topics: %s, solar/telemetry, cut-solar/+/telemetry", TOPIC_TELEMETRY)
        else:
            logger.error("MQTT connection failed with reason code: %s", reason_code)

    def _on_disconnect(self, client, userdata, disconnect_flags, reason_code, properties=None):
        logger.warning("Disconnected from MQTT Broker (reason=%s)", reason_code)

    def _on_message(self, client, userdata, msg: mqtt.MQTTMessage):
        """Callback triggered when ESP32 publishes JSON telemetry to 'solar/telemetry'."""
        try:
            payload_str = msg.payload.decode("utf-8")
            data = json.loads(payload_str)

            battery_voltage = data.get("battery_voltage")
            if battery_voltage is not None:
                battery_voltage = float(battery_voltage)
                if not math.isfinite(battery_voltage) or not 0 <= battery_voltage <= 6.6:
                    raise ValueError("battery_voltage must be finite and between 0 and 6.6 V")

            mode_raw = str(data.get("current_mode", data.get("mode", "AUTO"))).upper()
            current_mode = (
                TrackerMode.MANUAL if mode_raw == "MANUAL" else TrackerMode.AUTO
            )

            record = SolarTelemetry(
                ldr_right=int(data.get("ldr_right", data.get("ldr_top_left", data.get("ldr_tl", 0)))),
                ldr_left=int(data.get("ldr_left", data.get("ldr_top_right", data.get("ldr_tr", 0)))),
                ldr_top=int(data.get("ldr_top", data.get("ldr_bottom_left", data.get("ldr_bl", 0)))),
                ldr_bottom=int(data.get("ldr_bottom", data.get("ldr_bottom_right", data.get("ldr_br", 0)))),
                servo_pan=int(data.get("servo_pan", data.get("pan", 90))),
                servo_tilt=int(data.get("servo_tilt", data.get("tilt", 90))),
                temperature=float(data.get("temperature", data.get("temp", 0.0))),
                humidity=float(data.get("humidity", data.get("hum", 0.0))),
                lux=float(data.get("lux", 0.0)),
                battery_voltage=battery_voltage,
                local_hold=data.get("local_hold") is True,
                current_mode=current_mode,
            )

            db = SessionLocal()
            try:
                db.add(record)
                db.commit()
                logger.debug(
                    "Logged telemetry id=%s (mode=%s, lux=%.1f)",
                    record.id,
                    record.current_mode,
                    record.lux,
                )
            except Exception as db_exc:
                db.rollback()
                logger.error("Database error saving telemetry: %s", db_exc)
            finally:
                db.close()

        except json.JSONDecodeError as json_exc:
            logger.error("Malformed JSON on %s: %s", msg.topic, json_exc)
        except Exception as exc:
            logger.error("Unexpected error processing MQTT message: %s", exc)

    def start(self) -> None:
        """Connect to MQTT broker and start the non-blocking background network thread."""
        try:
            self.client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
            self.client.loop_start()
            logger.info("MQTT background loop started.")
        except Exception as exc:
            logger.error("Failed to start MQTT client loop: %s", exc)

    def stop(self) -> None:
        """Stop the background MQTT network loop and disconnect cleanly."""
        try:
            self.client.loop_stop()
            self.client.disconnect()
            logger.info("MQTT background loop stopped.")
        except Exception as exc:
            logger.error("Error stopping MQTT client: %s", exc)

    def publish_command(self, payload: dict) -> bool:
        """Publish a manual override or steering command to 'solar/command'."""
        message = json.dumps(payload)
        info = self.client.publish(TOPIC_COMMAND, payload=message, qos=1)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            logger.error("Failed to publish command to %s (rc=%s)", TOPIC_COMMAND, info.rc)
            return False
        return True


mqtt_manager = SolarMQTTManager()
