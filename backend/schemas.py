from datetime import datetime
from typing import Literal, Optional
from pydantic import AliasChoices, BaseModel, Field


class TelemetryPayload(BaseModel):
    """Schema for incoming MQTT telemetry JSON and outgoing API responses."""

    ldr_right: int = Field(..., validation_alias=AliasChoices("ldr_right", "ldr_top_left", "ldr_tl"), ge=0, le=4095)
    ldr_left: int = Field(..., validation_alias=AliasChoices("ldr_left", "ldr_top_right", "ldr_tr"), ge=0, le=4095)
    ldr_top: int = Field(..., validation_alias=AliasChoices("ldr_top", "ldr_bottom_left", "ldr_bl"), ge=0, le=4095)
    ldr_bottom: int = Field(..., validation_alias=AliasChoices("ldr_bottom", "ldr_bottom_right", "ldr_br"), ge=0, le=4095)
    pan: int = Field(..., alias="servo_pan", ge=0, le=180)
    tilt: int = Field(..., alias="servo_tilt", ge=0, le=180)
    temperature: float
    humidity: float
    lux: float = Field(..., ge=0.0)
    battery_voltage: Optional[float] = Field(default=None, ge=0, le=6.6)
    local_hold: bool = False
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
