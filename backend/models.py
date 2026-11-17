from datetime import datetime, timezone
from sqlalchemy import Column, Integer, Float, Boolean, String, DateTime, Enum as SQLEnum
import enum
from database import Base


class TrackerMode(str, enum.Enum):
    AUTO = "AUTO"
    MANUAL = "MANUAL"


class SolarTelemetry(Base):
    """PostgreSQL table schema for Keyestudio KS0530 solar tracker telemetry."""

    __tablename__ = "solar_telemetry"

    id = Column(Integer, primary_key=True, index=True)
    timestamp = Column(
        DateTime(timezone=True),
        default=lambda: datetime.now(timezone.utc),
        nullable=False,
        index=True,
    )

    # Keep SQL column names for existing databases; attributes use LCD positions.
    # 4x Photoresistor (LDR) raw ADC readings (0 - 4095 on ESP32)
    ldr_right = Column("ldr_top_left", Integer, nullable=False)
    ldr_left = Column("ldr_top_right", Integer, nullable=False)
    ldr_top = Column("ldr_bottom_left", Integer, nullable=False)
    ldr_bottom = Column("ldr_bottom_right", Integer, nullable=False)

    # Dual-axis servo angles in degrees (0 - 180)
    servo_pan = Column(Integer, nullable=False)
    servo_tilt = Column(Integer, nullable=False)

    # DHT11 environmental readings
    temperature = Column(Float, nullable=False)
    humidity = Column(Float, nullable=False)

    # BH1750 light intensity in Lux
    lux = Column(Float, nullable=False)

    battery_voltage = Column(Float, nullable=True)
    local_hold = Column(Boolean, nullable=False, default=False, server_default="false")

    # Operating mode: AUTO or MANUAL
    current_mode = Column(
        SQLEnum(TrackerMode, name="tracker_mode_enum"),
        default=TrackerMode.AUTO,
        nullable=False,
    )
