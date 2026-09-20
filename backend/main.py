import asyncio
import json
import socket
from copy import deepcopy
from datetime import datetime, timezone
from typing import Set

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, StreamingResponse
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

    # Most recent falls first, capped below. Exposed over both /device/status
    # (REST fallback) and /events (SSE) so a page that reconnects after
    # missing broadcasts can still rebuild its history.
    "fall_history": [],
}

FALL_HISTORY_LIMIT = 50


# =========================================================
# LIVE UPDATES (SERVER-SENT EVENTS)
# =========================================================
#
# Each connected dashboard gets its own asyncio.Queue. /sensor (and the
# other state-mutating endpoints) push the latest snapshot into every
# queue after updating device_state; /events streams whatever lands in
# its queue back to that one browser tab.

subscribers: Set["asyncio.Queue[dict]"] = set()


async def broadcast_state() -> None:
    if not subscribers:
        return

    payload = status_payload()

    for queue in list(subscribers):
        try:
            queue.put_nowait(payload)
        except asyncio.QueueFull:
            # A stalled client falls behind; drop it rather than block
            # every other subscriber or leak memory forever.
            subscribers.discard(queue)


# =========================================================
# ROOT
# =========================================================

@app.on_event("startup")
def show_listen_addresses():
    print("SmartElderlyCare API is listening.")
    print("  Point the ESP SERVER_IP at one of these laptop addresses:")
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith("127."):
                print(f"    http://{ip}:8001/sensor")
    except OSError:
        pass
    print("  Dashboard: keep Vite proxying to http://127.0.0.1:8001")


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


def status_payload():
    """Snapshot of device state. Never return the live dict — it can
    change while FastAPI is serializing the response."""
    return deepcopy(device_state)


def status_response():
    return JSONResponse(
        content=status_payload(),
        headers={
            "Cache-Control": "no-store, no-cache, must-revalidate",
            "Pragma": "no-cache",
        },
    )


# =========================================================
# GET CURRENT DEVICE STATUS
# =========================================================

@app.get("/device/status")
def get_device_status():

    return status_response()


# =========================================================
# SSE STREAM ENDPOINT
# =========================================================

@app.get("/events")
async def stream_events(request: Request):
    """SSE stream for the dashboard. Sends the current snapshot right
    away, then pushes a fresh one every time /sensor (or a test/clear
    endpoint) changes device_state. REST polling remains available as
    a fallback if this connection drops."""

    queue: "asyncio.Queue[dict]" = asyncio.Queue(maxsize=50)
    subscribers.add(queue)

    async def event_generator():
        try:
            yield f"data: {json.dumps(status_payload())}\n\n"

            while True:
                if await request.is_disconnected():
                    break

                try:
                    payload = await asyncio.wait_for(queue.get(), timeout=15)
                    yield f"data: {json.dumps(payload)}\n\n"
                except asyncio.TimeoutError:
                    yield ": keep-alive\n\n"
        finally:
            subscribers.discard(queue)

    return StreamingResponse(
        event_generator(),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
            "X-Accel-Buffering": "no",
        },
    )


def record_fall(device_id: str, timestamp: str) -> None:
    device_state["fall_count"] += 1
    device_state["last_fall"] = timestamp

    device_state["fall_history"].insert(0, {
        "device_id": device_id,
        "timestamp": timestamp,
    })
    del device_state["fall_history"][FALL_HISTORY_LIMIT:]


# =========================================================
# RECEIVE SENSOR DATA FROM ESP8266
# =========================================================

@app.post("/sensor")
async def receive_sensor_data(data: SensorData):

    now = datetime.now(timezone.utc).isoformat()

    previous_fall = bool(device_state["fall_detected"])

    device_state["device_id"] = data.device_id

    device_state["connected"] = True

    device_state["accel_x"] = data.accel_x
    device_state["accel_y"] = data.accel_y
    device_state["accel_z"] = data.accel_z

    device_state["gyro_x"] = data.gyro_x
    device_state["gyro_y"] = data.gyro_y
    device_state["gyro_z"] = data.gyro_z

    device_state["total_acceleration"] = data.total_acceleration

    device_state["last_update"] = now

    # Latch the caregiver alert. The ESP only sends fall_detected=true
    # on the packet right after a fall, then goes back to false. That
    # later false must NEVER clear the dashboard.
    if data.fall_detected:
        device_state["fall_detected"] = True

        if not previous_fall:
            record_fall(data.device_id, now)

    await broadcast_state()

    return {
        "status": "received",
        "timestamp": now,
        "fall_detected": device_state["fall_detected"],
    }


# =========================================================
# SIMULATE FALL
# =========================================================

@app.post("/test/fall")
async def simulate_fall():

    now = datetime.now(timezone.utc).isoformat()

    device_state["connected"] = True
    device_state["fall_detected"] = True
    record_fall(device_state["device_id"], now)

    await broadcast_state()

    return {
        "status": "test fall generated",
        "timestamp": now
    }


# =========================================================
# CLEAR FALL ALERT
# =========================================================

@app.post("/device/clear-alert")
async def clear_alert():

    device_state["fall_detected"] = False

    await broadcast_state()

    return {
        "status": "alert cleared"
    }
