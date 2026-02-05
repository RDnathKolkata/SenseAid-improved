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
import cv2
import numpy as np

object_memory = {}
DANGER_DISTANCE_M = 2.0

app = FastAPI()

#  CONFIG  

YOLO_MODEL_PATH = "yolo11m.pt"

# ESP32 Audio Unit
ESP32_AUDIO_URL = "http://ESP32_AUDIO_IP/alert"

CONFIDENCE_THRESHOLD = 0.5
ALERT_COOLDOWN = 3.0  # seconds

# Display + global ESP32 involvement toggle
display_enabled = True
alerts_enabled = True  # if False -> never send alerts to ESP32

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

def _overlay_status(img):
    """Overlay current mode on the video frame."""
    global alerts_enabled
    mode = "ESP32 ALERTS: ON" if alerts_enabled else "ESP32 ALERTS: OFF"
    hint = "Press 't' to toggle, 'q' to close window"
    cv2.putText(img, mode, (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
    cv2.putText(img, hint, (10, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 0), 2)

def draw_detections(img_array, detections):
    """Draw bounding boxes and labels on image"""
    img = img_array.copy()
    
    for det in detections:
        bbox = det['bbox']
        class_name = det['class']
        confidence = det['confidence']
        track_id = det.get('track_id', None)
        
        x1, y1, x2, y2 = map(int, bbox)
        
        # Color based on alert priority
        if class_name in ALERT_MESSAGES:
            color = (0, 0, 255)  # Red for alert objects
        else:
            color = (0, 255, 0)  # Green for other objects
        
        # Draw bounding box
        cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
        
        # Prepare label text
        label = f"{class_name} {confidence:.2f}"
        if track_id is not None:
            label += f" ID:{track_id}"
        
        # Draw label background
        (text_width, text_height), baseline = cv2.getTextSize(
            label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1
        )
        cv2.rectangle(
            img, 
            (x1, y1 - text_height - baseline - 5), 
            (x1 + text_width, y1), 
            color, 
            -1
        )
        
        # Draw label text
        cv2.putText(
            img, 
            label, 
            (x1, y1 - baseline - 5), 
            cv2.FONT_HERSHEY_SIMPLEX, 
            0.5, 
            (255, 255, 255), 
            1
        )
    
    return img

def display_frame(img_array, detections):
    """Display frame with detections"""
    if not display_enabled:
        return
    
    annotated_img = draw_detections(img_array, detections)
    _overlay_status(annotated_img)
    
    # Resize for display if too large
    height, width = annotated_img.shape[:2]
    if width > 1280:
        scale = 1280 / width
        new_width = 1280
        new_height = int(height * scale)
        annotated_img = cv2.resize(annotated_img, (new_width, new_height))
    
    cv2.imshow("ESP32-CAM Detection", annotated_img)
    key = cv2.waitKey(1) & 0xFF  # Non-blocking
    if key == ord('t'):
        global alerts_enabled
        alerts_enabled = not alerts_enabled
        print(f"🔁 Toggled ESP32 alerts: {'ON' if alerts_enabled else 'OFF'}")
    elif key == ord('q'):
        global display_enabled
        display_enabled = False
        cv2.destroyAllWindows()
        print("🛑 Display window closed (server still running)")

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
    """Send alert to ESP32 audio unit"""
    payload = {
        "object": obj_class,
        "distance": round(distance, 2),
        "type": alert_type
    }

    try:
        requests.post(ESP32_AUDIO_URL, json=payload, timeout=0.5)
        print(f"🔊 {alert_type.upper()} ALERT → {payload}")
    except Exception as e:
        print(f"❌ Failed to send alert: {e}")


#  API  #

@app.post("/frame")
async def receive_frame(file: UploadFile = File(...)):
    try:
        contents = await file.read()
        img = Image.open(BytesIO(contents)).convert("RGB")
        
        # Convert PIL Image to numpy array for OpenCV
        img_array = np.array(img)
        img_array = cv2.cvtColor(img_array, cv2.COLOR_RGB2BGR)

        # Detection and tracking
        results = model.track(img, persist=True)

        detections = []

        for result in results:
            for box in result.boxes:
                class_name = model.names[int(box.cls[0])]
                confidence = float(box.conf[0])
                
                # Get track ID (None if tracking fails)
                track_id = int(box.id[0]) if box.id is not None else None

                bbox = box.xyxy[0].tolist()
                detections.append({
                    "class": class_name,
                    "confidence": confidence,
                    "bbox": bbox,
                    "track_id": track_id
                })

                # Alert logic (only if globally enabled)
                if alerts_enabled and (
                    class_name in ALERT_MESSAGES
                    and confidence >= CONFIDENCE_THRESHOLD
                    and track_id is not None  # Need valid tracking
                ):
                    distance = estimate_distance(bbox, class_name)
                    alert_type = check_alert(track_id, distance)
                    if alert_type:
                        send_alert_to_esp32(class_name, distance, alert_type)

        # Display frame with detections
        if display_enabled:
            display_frame(img_array, detections)

        return JSONResponse({
            "success": True,
            "detections": detections
        })

    except Exception as e:
        print(f"❌ Error processing frame: {e}")
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
    # One-time authorization: involve ESP32 or not
    try:
        ans = input("Enable ESP32 Bluetooth alert unit? (y/n): ").strip().lower()
    except Exception:
        ans = "y"

    alerts_enabled = (ans == "y")

    print("\n🎥 Video display enabled (OpenCV window)")
    print(f"🔊 ESP32 alerts: {'ENABLED' if alerts_enabled else 'DISABLED'}")
    print("💡 In the video window: press 't' to toggle ESP32 alerts, 'q' to close window")
    print("📡 Server starting on `http://0.0.0.0:8000`\n")
    
    try:
        uvicorn.run(app, host="0.0.0.0", port=8000)
    except KeyboardInterrupt:
        print("\n🛑 Shutting down...")
        cv2.destroyAllWindows()

# @app.post("/fall_alert")
# async def fall_alert(request: Request):
#     data = await request.json()
#     print("🚨 FALL DETECTED!")
#     print(f"   Timestamp: {data['timestamp']}ms")
    
#     # Trigger audio alert
#     payload = {"object": "emergency", "distance": 0, "type": "fall_alert"}
#     requests.post(ESP32_AUDIO_URL, json=payload, timeout=1.0)
    
#     return {"success": True, "message": "Fall alert logged"}
