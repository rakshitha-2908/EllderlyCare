from datetime import datetime, timezone
from typing import Optional

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel


# =========================================================
# APP
# =========================================================

app = FastAPI(
    title="SmartElderlyCare API",
    description="Backend for elderly fall detection and caregiver alerts",
    version="1.0.0"
)


# =========================================================
# CORS
# =========================================================

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


# =========================================================
# DATA MODEL
# =========================================================

class SensorData(BaseModel):
    device_id: str = "elderly-device-01"

    accel_x: float = 0.0
    accel_y: float = 0.0
    accel_z: float = 0.0

    gyro_x: float = 0.0
    gyro_y: float = 0.0
    gyro_z: float = 0.0

    total_acceleration: float = 0.0

    fall_detected: bool = False


# =========================================================
# CURRENT DEVICE STATE
# =========================================================

device_state = {
    "device_id": "elderly-device-01",

    "connected": False,

    "accel_x": 0.0,
    "accel_y": 0.0,
    "accel_z": 0.0,

    "gyro_x": 0.0,
    "gyro_y": 0.0,
    "gyro_z": 0.0,

    "total_acceleration": 0.0,

    "fall_detected": False,

    "last_update": None,
    "last_fall": None,

    "fall_count": 0,
}


# =========================================================
# ROOT
# =========================================================

@app.get("/")
def root():
    return {
        "message": "SmartElderlyCare API is running"
    }


# =========================================================
# HEALTH CHECK
# =========================================================

@app.get("/health")
def health():
    return {
        "status": "ok"
    }


# =========================================================
# GET CURRENT DEVICE STATUS
# =========================================================

@app.get("/device/status")
def get_device_status():

    return device_state


# =========================================================
# RECEIVE SENSOR DATA FROM ESP8266
# =========================================================

@app.post("/sensor")
def receive_sensor_data(data: SensorData):

    now = datetime.now(timezone.utc).isoformat()

    previous_fall = device_state["fall_detected"]

    device_state["device_id"] = data.device_id

    device_state["connected"] = True

    device_state["accel_x"] = data.accel_x
    device_state["accel_y"] = data.accel_y
    device_state["accel_z"] = data.accel_z

    device_state["gyro_x"] = data.gyro_x
    device_state["gyro_y"] = data.gyro_y
    device_state["gyro_z"] = data.gyro_z

    device_state["total_acceleration"] = data.total_acceleration

    if data.fall_detected:
        device_state["fall_detected"] = True

    device_state["last_update"] = now

    # Count only the transition:
    # NORMAL -> FALL
    if data.fall_detected and not previous_fall:

        device_state["fall_count"] += 1
        device_state["last_fall"] = now

    return {
        "status": "received",
        "timestamp": now,
        "fall_detected": data.fall_detected
    }


# =========================================================
# SIMULATE FALL
# =========================================================

@app.post("/test/fall")
def simulate_fall():

    now = datetime.now(timezone.utc).isoformat()

    device_state["connected"] = True
    device_state["fall_detected"] = True
    device_state["last_fall"] = now
    device_state["fall_count"] += 1

    return {
        "status": "test fall generated",
        "timestamp": now
    }


# =========================================================
# CLEAR FALL ALERT
# =========================================================

@app.post("/device/clear-alert")
def clear_alert():

    device_state["fall_detected"] = False

    return {
        "status": "alert cleared"
    }