import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

os.environ["DATABASE_URL"] = "sqlite://"
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from sqlalchemy import create_engine, text
from sqlalchemy.orm import sessionmaker
from database import Base
from migrations import upgrade_telemetry
from models import SolarTelemetry
from mqtt_client import SolarMQTTManager
from schemas import TelemetryPayload


class TelemetryTests(unittest.TestCase):
    def setUp(self):
        self.engine = create_engine("sqlite://")
        Base.metadata.create_all(self.engine)
        self.sessions = sessionmaker(bind=self.engine)

    def tearDown(self):
        self.engine.dispose()

    def ingest(self, payload):
        manager = SolarMQTTManager()
        message = type("Message", (), {
            "payload": json.dumps(payload).encode(), "topic": "solar/telemetry"
        })()
        with patch("mqtt_client.SessionLocal", self.sessions):
            manager._on_message(None, None, message)
        with self.sessions() as session:
            record = session.query(SolarTelemetry).one()
            return TelemetryPayload.model_validate(record).model_dump(by_alias=True)

    def test_new_firmware_round_trip(self):
        result = self.ingest({
            "ldr_top": 320, "ldr_bottom": 330, "ldr_left": 350, "ldr_right": 340,
            "battery_voltage": 4.12, "local_hold": True, "current_mode": "MANUAL"
        })
        self.assertEqual(result["battery_voltage"], 4.12)
        self.assertTrue(result["local_hold"])
        self.assertEqual(result["current_mode"], "MANUAL")
        self.assertEqual(result["ldr_top"], 320)

    def test_old_firmware_keeps_battery_unknown(self):
        result = self.ingest({
            "ldr_top_left": 340, "ldr_top_right": 350,
            "ldr_bottom_left": 320, "ldr_bottom_right": 330
        })
        self.assertIsNone(result["battery_voltage"])
        self.assertFalse(result["local_hold"])
        self.assertEqual([result[f"ldr_{p}"] for p in
                          ("top", "bottom", "left", "right")], [320, 330, 350, 340])

    def test_upgrade_preserves_existing_rows_and_is_repeatable(self):
        engine = create_engine("sqlite://")
        with engine.begin() as conn:
            conn.execute(text("CREATE TABLE solar_telemetry (id INTEGER PRIMARY KEY, lux FLOAT)"))
            conn.execute(text("INSERT INTO solar_telemetry VALUES (1, 123.5)"))
        upgrade_telemetry(engine)
        upgrade_telemetry(engine)
        with engine.connect() as conn:
            row = conn.execute(text("SELECT * FROM solar_telemetry")).mappings().one()
            self.assertEqual(row["lux"], 123.5)
            self.assertIsNone(row["battery_voltage"])
            self.assertFalse(row["local_hold"])
        engine.dispose()

    def test_invalid_battery_does_not_create_unreadable_api_record(self):
        manager = SolarMQTTManager()
        for voltage in (-1, 10, float("nan")):
            message = type("Message", (), {
                "payload": json.dumps({"battery_voltage": voltage}).encode(),
                "topic": "solar/telemetry"
            })()
            with patch("mqtt_client.SessionLocal", self.sessions), self.assertLogs(
                "cut_solar.mqtt", level="ERROR"
            ):
                manager._on_message(None, None, message)
        with self.sessions() as session:
            self.assertEqual(session.query(SolarTelemetry).count(), 0)


if __name__ == "__main__":
    unittest.main()
