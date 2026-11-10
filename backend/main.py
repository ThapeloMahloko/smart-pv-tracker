from contextlib import asynccontextmanager
import logging
import os
from typing import Optional

from fastapi import Depends, FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from google import genai
from google.genai import types
from sqlalchemy import desc
from sqlalchemy.orm import Session

from database import Base, engine, get_db
from models import SolarTelemetry
from mqtt_client import TOPIC_COMMAND, mqtt_manager
from schemas import (
    ChatRequest,
    ChatResponse,
    OverrideCommandRequest,
    OverrideCommandResponse,
    TelemetryPayload,
)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("cut_solar.api")


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Initialize PostgreSQL tables and manage the background MQTT loop lifecycle."""
    try:
        Base.metadata.create_all(bind=engine)
        logger.info("PostgreSQL tables verified/created.")
    except Exception as exc:
        logger.warning("Could not initialize PostgreSQL tables at startup: %s", exc)

    mqtt_manager.start()
    yield
    mqtt_manager.stop()


app = FastAPI(
    title="CUT Smart Solar Tracker API",
    description=(
        "FastAPI backend for the Central University of Technology (CUT) "
        "Keyestudio KS0530 ESP32 Dual-Axis Solar Tracker."
    ),
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


def build_cut_system_prompt(latest: Optional[SolarTelemetry]) -> str:
    """Construct the RAG-style system instruction referencing CUT sports ground arrays and live telemetry."""
    if latest:
        telemetry_context = (
            f"- Timestamp (UTC): {latest.timestamp.isoformat()}\n"
            f"- Operating Mode: {latest.current_mode.value}\n"
            f"- Light Intensity (BH1750): {latest.lux:.1f} lx\n"
            f"- Ambient Temperature (DHT11): {latest.temperature:.1f} °C\n"
            f"- Relative Humidity (DHT11): {latest.humidity:.1f} %\n"
            f"- Servo Angles: Pan (Azimuth) = {latest.servo_pan}°, Tilt (Elevation) = {latest.servo_tilt}°\n"
            f"- 4x LDR Quadrant Readings (0-4095): "
            f"TL={latest.ldr_top_left}, TR={latest.ldr_top_right}, "
            f"BL={latest.ldr_bottom_left}, BR={latest.ldr_bottom_right}"
        )
    else:
        telemetry_context = (
            "No live telemetry has been recorded in PostgreSQL yet (awaiting ESP32 MQTT stream)."
        )

    return f"""You are the official AI Guide for the Central University of Technology (CUT) Smart Solar Project in Bloemfontein, Free State, South Africa.

PROJECT & CAMPUS CONTEXT (RAG KNOWLEDGE BASE):
1. Physical Demonstration Unit: Visitors are interacting with an ESP32-powered Keyestudio KS0530 dual-axis solar tracking scale model equipped with 4 photoresistors (LDRs), dual pan/tilt micro-servos, a BH1750 digital light intensity sensor, and a DHT11 temperature & humidity sensor.
2. Full-Scale Campus Installation: This miniature dual-axis tracker demonstrates the engineering principles behind the real photovoltaic (PV) solar panels installed at the Central University of Technology (CUT) university sports ground. The sports ground solar arrays provide clean, renewable power to campus facilities, offset peak daytime grid demand, and serve as a living laboratory for CUT Engineering and Built Environment students.
3. How Dual-Axis Tracking Works: By comparing the 4 quadrant LDR sensors (Top-Left, Top-Right, Bottom-Left, Bottom-Right), the tracker keeps the solar panel perpendicular to incoming sunlight across both azimuth (Pan) and elevation (Tilt), boosting energy yield by up to 25-35% compared to fixed-tilt installations like standard rooftop arrays, while complementing the large-scale solar infrastructure on the CUT sports ground.

LIVE TELEMETRY SNAPSHOT FROM POSTGRESQL:
{telemetry_context}

INSTRUCTIONS:
- Act as an enthusiastic, knowledgeable guide for students, visitors, and researchers exploring the Central University of Technology (CUT) solar project.
- Explicitly reference the real solar panels on the CUT university sports ground when explaining how this scale tracker relates to campus sustainability and renewable energy generation.
- Ground your answers in the live telemetry snapshot above whenever the user asks about current sunlight, temperature, humidity, panel angles, or tracking status.
- Keep explanations clear, concise, and mobile-friendly."""


@app.get("/telemetry/latest", response_model=Optional[TelemetryPayload])
def get_latest_telemetry(db: Session = Depends(get_db)):
    """Return the most recent solar telemetry row from PostgreSQL."""
    latest = (
        db.query(SolarTelemetry)
        .order_by(desc(SolarTelemetry.timestamp))
        .first()
    )
    if not latest:
        return None
    return TelemetryPayload.model_validate(latest)


@app.post("/chat", response_model=ChatResponse)
def chat_with_guide(request: ChatRequest, db: Session = Depends(get_db)):
    """Answer user queries using the google-genai SDK with RAG system prompt injection."""
    latest = (
        db.query(SolarTelemetry)
        .order_by(desc(SolarTelemetry.timestamp))
        .first()
    )

    api_key = os.getenv("GEMINI_API_KEY")
    if not api_key:
        raise HTTPException(
            status_code=500,
            detail="GEMINI_API_KEY environment variable is not configured.",
        )

    system_instruction = build_cut_system_prompt(latest)

    try:
        client = genai.Client(api_key=api_key)
        response = client.models.generate_content(
            model="gemini-2.5-flash",
            contents=request.query,
            config=types.GenerateContentConfig(
                system_instruction=system_instruction,
                temperature=0.4,
            ),
        )
        reply_text = response.text or "I couldn't generate a response at this moment."
    except Exception as exc:
        logger.error("Gemini API error: %s", exc)
        raise HTTPException(
            status_code=502,
            detail=f"Failed to generate response from Gemini AI Guide: {exc}",
        ) from exc

    return ChatResponse(
        reply=reply_text,
        latest_telemetry=TelemetryPayload.model_validate(latest) if latest else None,
    )


@app.post("/override", response_model=OverrideCommandResponse)
def trigger_manual_override(command: OverrideCommandRequest):
    """Publish a manual override or D-pad directional command to MQTT 'solar/command'."""
    mqtt_payload = {
        "mode": "AUTO" if command.action == "RELEASE" else "MANUAL",
        "action": command.action,
        "step": command.step,
    }

    published = mqtt_manager.publish_command(mqtt_payload)
    if not published:
        raise HTTPException(
            status_code=503,
            detail="MQTT broker unavailable; could not publish command to solar/command.",
        )

    return OverrideCommandResponse(
        status="published",
        topic=TOPIC_COMMAND,
        published_payload=mqtt_payload,
    )
