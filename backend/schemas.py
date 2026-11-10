from datetime import datetime
from typing import Literal, Optional
from pydantic import BaseModel, Field


class TelemetryPayload(BaseModel):
    """Schema for incoming MQTT telemetry JSON and outgoing API responses."""

    ldr_tl: int = Field(..., alias="ldr_top_left", ge=0, le=4095)
    ldr_tr: int = Field(..., alias="ldr_top_right", ge=0, le=4095)
    ldr_bl: int = Field(..., alias="ldr_bottom_left", ge=0, le=4095)
    ldr_br: int = Field(..., alias="ldr_bottom_right", ge=0, le=4095)
    pan: int = Field(..., alias="servo_pan", ge=0, le=180)
    tilt: int = Field(..., alias="servo_tilt", ge=0, le=180)
    temperature: float
    humidity: float
    lux: float = Field(..., ge=0.0)
    current_mode: Literal["AUTO", "MANUAL"] = "AUTO"
    timestamp: Optional[datetime] = None

    model_config = {
        "populate_by_name": True,
        "from_attributes": True,
    }


class ChatRequest(BaseModel):
    """User query sent from the Flutter Web AI Guide tab."""

    query: str = Field(..., min_length=1, max_length=2000)


class ChatResponse(BaseModel):
    """AI Guide answer enriched with the telemetry snapshot used for RAG."""

    reply: str
    latest_telemetry: Optional[TelemetryPayload] = None


class OverrideCommandRequest(BaseModel):
    """Manual override and D-pad steering command sent from Flutter Web."""

    action: Literal["TAKE_CONTROL", "UP", "DOWN", "LEFT", "RIGHT", "RELEASE"]
    step: int = Field(default=5, ge=1, le=45, description="Servo angle step in degrees")


class OverrideCommandResponse(BaseModel):
    """Confirmation of published MQTT command."""

    status: str
    topic: str
    published_payload: dict
