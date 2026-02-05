# PerceptaLucis™
# © 2026 Rajdeep Debnath
# CC BY-NC-SA 4.0


from fastapi import FastAPI, File, UploadFile
from fastapi.responses import JSONResponse
import uvicorn
from ultralytics import YOLO
from PIL import Image
from io import BytesIO
import requests
import time

object_memory = {}
DANGER_DISTANCE_M = 2.0

app = FastAPI()

#  CONFIG  

YOLO_MODEL_PATH = "yolo11m.pt"

# ESP32 Audio Unit
ESP32_AUDIO_URL = "http://ESP32_AUDIO_IP/alert"

CONFIDENCE_THRESHOLD = 0.5
ALERT_COOLDOWN = 3.0  # seconds


print("🧠 Loading YOLO model...")
model = YOLO(YOLO_MODEL_PATH)
print("✅ YOLO loaded")

# objects
ALERT_MESSAGES = {
    "car": "car",
    "bicycle": "bicycle",
    "motorcycle": "motorcycle",
    "bus": "bus",
    "truck": "truck",
    "train": "train"
}

# Alert cooldown memory
last_alert_time = {}


#  HELPERS  #

def check_alert(track_id, distance):
    now = time.time()

    if track_id not in object_memory:
        object_memory[track_id] = {
            "seen": True,
            "last_distance": distance,
            "last_alert_time": 0
        }
        return "presence"  # alert once on first sight

    prev_distance = object_memory[track_id]["last_distance"]
    last_alert = object_memory[track_id]["last_alert_time"]

    object_memory[track_id]["last_distance"] = distance

    # Standing still or moving away
    if distance >= prev_distance:
        return None

    # Approaching
    if distance <= DANGER_DISTANCE_M:
        if now - last_alert >= ALERT_COOLDOWN:
            object_memory[track_id]["last_alert_time"] = now
            return "approaching"

    return None



def send_alert_to_esp32(obj_class, distance, alert_type):
    payload = {
        "object": obj_class,
        "distance": round(distance, 2),
        "type": alert_type
    }

    try:
        requests.post(ESP32_AUDIO_URL, json=payload, timeout=0.5)
        print(f"🔊 {alert_type.upper()} ALERT → {payload}")
    except:
        pass


#  API  #

@app.post("/frame")
async def receive_frame(file: UploadFile = File(...)):
    try:
        contents = await file.read()
        img = Image.open(BytesIO(contents)).convert("RGB")

        #detection er jaygay tracking
        results = model.track(img, persist=True)  # <-- CHANGED

        detections = []

        for result in results:
            for box in result.boxes:
                class_name = model.names[int(box.cls[0])]
                confidence = float(box.conf[0])
                
                # ✅ Get track ID (None if tracking fails)
                track_id = int(box.id[0]) if box.id is not None else None

                bbox = box.xyxy[0].tolist()
                detections.append({
                    "class": class_name,
                    "confidence": confidence,
                    "bbox": bbox,
                    "track_id": track_id
                })

                # FIXED ALERT LOGIC
                if (
                    class_name in ALERT_MESSAGES
                    and confidence >= CONFIDENCE_THRESHOLD
                    and track_id is not None  # Need valid tracking
                ):
                    # Estimate distance from bounding box
                    distance = estimate_distance(bbox, class_name)
                    
                    # Check if we should alert
                    alert_type = check_alert(track_id, distance)
                    
                    if alert_type:
                        send_alert_to_esp32(class_name, distance, alert_type)

        return JSONResponse({
            "success": True,
            "detections": detections
        })

    except Exception as e:
        return JSONResponse({
            "success": False,
            "error": str(e)
        }, status_code=500)

def estimate_distance(bbox, obj_class):
    """Estimate distance based on bbox size (simplified)"""
    x1, y1, x2, y2 = bbox
    height = y2 - y1
    
    # Rough heuristic large mane close
    # This is VERY approximate and needs calibration
    if height > 0:
        return max(0.5, 10.0 / (height / 100))
    return 10.0  # Default far maybe too much dekhte hobe

@app.get("/")
def root():
    return {
        "message": "YOLO Detection Server",
        "status": "running"
    }


# (reeshav if ur reading this then -->) RUN (Claude rated ts 5/10 at the beginning 😭)- #

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

# @app.post("/fall_alert")
# async def fall_alert(request: Request):
#     data = await request.json()
#     print("🚨 FALL DETECTED!")
#     print(f"   Timestamp: {data['timestamp']}ms")
    
#     # Trigger audio alert
#     payload = {"object": "emergency", "distance": 0, "type": "fall_alert"}
#     requests.post(ESP32_AUDIO_URL, json=payload, timeout=1.0)
    
#     return {"success": True, "message": "Fall alert logged"}