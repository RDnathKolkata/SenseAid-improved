# from fastapi import FastAPI, File, UploadFile
# from fastapi.responses import JSONResponse
# import uvicorn
# from ultralytics import YOLO
# from PIL import Image
# from io import BytesIO
# import pyttsx3
# from threading import Thread

# app = FastAPI()

# # Load YOLO11m model once at startup
# print("Loading YOLO11m model...")
# model = YOLO('yolo11m.pt')
# print("Model loaded successfully!")

# # Initialize text-to-speech engine
# tts_engine = pyttsx3.init()
# tts_engine.setProperty('rate', 150)  # Speech speed
# tts_engine.setProperty('volume', 1.0)  # Volume (0.0 to 1.0)

# # Define priority objects and their custom messages
# ALERT_MESSAGES = {
#     "bicycle": "Bicycle on way. STOP!",
#     "car": "Car coming! Move back.",
#     "motorcycle": "Motorbike on way. STOP!",
#     "bus": "Bus coming. Move back!",
#     "train": "Train en route. MOVE BACK IMMEDIATELY!",
#     "truck": "Truck coming. Move back!",
#     "bench": "Bench detected. May sit down.",
#     "chair": "Chair detected. May sit down.",
#     "bed": "Bed detected. May take rest.",
#     "couch": "Couch detected. May take rest.",
#     "banana": "Banana!! YAY!!"
# }

# def speak_alert(message):
#     """Speak the alert message in a separate thread"""
#     def _speak():
#         tts_engine.say(message)
#         tts_engine.runAndWait()
    
#     Thread(target=_speak, daemon=True).start()

# @app.post("/detect")
# async def detect_objects(file: UploadFile = File(...)):
#     try:
#         # Read image from upload
#         contents = await file.read()
#         img = Image.open(BytesIO(contents)).convert('RGB')
        
#         # Run YOLO11 inference
#         results = model(img)
        
#         # Parse detections and filter for priority objects
#         all_detections = []
#         priority_alerts = []
        
#         for result in results:
#             boxes = result.boxes
#             for box in boxes:
#                 class_name = model.names[int(box.cls[0])]
#                 confidence = float(box.conf[0])
                
#                 detection = {
#                     "class": class_name,
#                     "confidence": confidence,
#                     "bbox": box.xyxy[0].tolist()
#                 }
#                 all_detections.append(detection)
                
#                 # Check if this is a priority object
#                 if class_name in ALERT_MESSAGES and confidence > 0.5:
#                     alert_message = ALERT_MESSAGES[class_name]
#                     priority_alerts.append({
#                         "class": class_name,
#                         "message": alert_message,
#                         "confidence": confidence
#                     })
                    
#                     # Speak the alert
#                     print(f"🔊 Alert: {alert_message}")
#                     speak_alert(alert_message)
        
#         return JSONResponse(content={
#             "success": True,
#             "total_detections": len(all_detections),
#             "priority_alerts": priority_alerts,
#             "all_detections": all_detections
#         })
        
#     except Exception as e:
#         return JSONResponse(content={
#             "success": False,
#             "error": str(e)
#         }, status_code=500)

# @app.get("/")
# async def root():
#     return {
#         "message": "Sense-Aid YOLO11 Detection Server with Audio Alerts",
#         "monitored_objects": list(ALERT_MESSAGES.keys())
#     }

# @app.get("/alerts")
# async def get_alert_list():
#     """Endpoint to see all configured alerts"""
#     return {"alert_messages": ALERT_MESSAGES}

# if __name__ == "__main__":
#     print("Starting Sense-Aid server with audio alerts on http://0.0.0.0:8000")
#     print(f"Monitoring {len(ALERT_MESSAGES)} priority objects")
#     uvicorn.run(app, host="0.0.0.0", port=8000)



from fastapi import FastAPI, File, UploadFile, Request
from fastapi.responses import JSONResponse
import uvicorn
from ultralytics import YOLO
from PIL import Image
from io import BytesIO
import httpx
import time
import cv2
import numpy as np
from pydantic_settings import BaseSettings
from pydantic import Field
from typing import Dict, Optional
import asyncio
from contextlib import asynccontextmanager

#  CONFIGURATION MANAGEMENT

class Settings(BaseSettings):
    """Application settings loaded from environment variables or .env file"""
    
    # YOLO Model
    yolo_model_path: str = Field(default="yolo11m.pt", env="YOLO_MODEL_PATH")
    confidence_threshold: float = Field(default=0.5, env="CONFIDENCE_THRESHOLD")
    
    # ESP32 Configuration
    esp32_audio_url: str = Field(default="http://192.168.1.100/alert", env="ESP32_AUDIO_URL")
    esp32_timeout: float = Field(default=0.5, env="ESP32_TIMEOUT")
    
    # Alert Settings
    danger_distance_m: float = Field(default=2.0, env="DANGER_DISTANCE_M")
    alert_cooldown: float = Field(default=3.0, env="ALERT_COOLDOWN")
    
    # Camera Calibration (CRITICAL for accurate distance estimation)
    camera_focal_length_px: float = Field(default=700.0, env="CAMERA_FOCAL_LENGTH_PX")
    camera_sensor_width_mm: float = Field(default=3.68, env="CAMERA_SENSOR_WIDTH_MM")
    image_width_px: int = Field(default=640, env="IMAGE_WIDTH_PX")
    
    # Known real-world object dimensions (height in meters)
    object_heights: Dict[str, float] = {
        "car": 1.5,
        "bus": 3.0,
        "truck": 2.5,
        "bicycle": 1.0,
        "motorcycle": 1.2,
        "train": 4.0,
        "person": 1.7
    }
    
    # Memory cleanup
    memory_cleanup_interval: int = Field(default=60, env="MEMORY_CLEANUP_INTERVAL")  # seconds
    memory_max_age: int = Field(default=30, env="MEMORY_MAX_AGE")  # seconds
    
    # Display Settings
    display_enabled: bool = Field(default=True, env="DISPLAY_ENABLED")
    
    class Config:
        env_file = ".env"
        env_file_encoding = 'utf-8'

# Initialize settings
settings = Settings()

#  APPLICATION STATE MANAGEMENT

class ObjectMemory:
    """Thread-safe object memory with automatic cleanup"""
    
    def __init__(self):
        self._memory: Dict[int, dict] = {}
        self._lock = asyncio.Lock()
    
    async def update(self, track_id: int, distance: float) -> Optional[str]:
        """Update object memory and return alert type if needed"""
        async with self._lock:
            now = time.time()
            
            if track_id not in self._memory:
                self._memory[track_id] = {
                    "seen": True,
                    "last_distance": distance,
                    "last_alert_time": 0,
                    "last_seen": now,
                    "first_seen": now
                }
                return "presence"  # Alert once on first sight
            
            obj = self._memory[track_id]
            prev_distance = obj["last_distance"]
            last_alert = obj["last_alert_time"]
            
            # Update tracking info
            obj["last_distance"] = distance
            obj["last_seen"] = now
            
            # Standing still or moving away
            if distance >= prev_distance:
                return None
            
            # Approaching and within danger zone
            if distance <= settings.danger_distance_m:
                if now - last_alert >= settings.alert_cooldown:
                    obj["last_alert_time"] = now
                    return "approaching"
            
            return None
    
    async def cleanup_stale_tracks(self):
        """Remove tracks not seen recently"""
        async with self._lock:
            now = time.time()
            stale_ids = [
                track_id for track_id, obj in self._memory.items()
                if now - obj["last_seen"] > settings.memory_max_age
            ]
            for track_id in stale_ids:
                del self._memory[track_id]
            
            if stale_ids:
                print(f"🧹 Cleaned up {len(stale_ids)} stale tracks")
    
    async def get_stats(self) -> dict:
        """Get memory statistics"""
        async with self._lock:
            return {
                "tracked_objects": len(self._memory),
                "memory_size_bytes": sum(len(str(v)) for v in self._memory.values())
            }

#  DISTANCE ESTIMATION (PROPER IMPLEMENTATION)

class DistanceEstimator:
    """Accurate distance estimation using pinhole camera model"""
    
    def __init__(self):
        self.focal_length = settings.camera_focal_length_px
        self.object_heights = settings.object_heights
    
    def estimate_distance(self, bbox: list, obj_class: str) -> float:
        """
        Calculate distance using pinhole camera model:
        Distance = (Real_Height × Focal_Length) / Pixel_Height
        
        Args:
            bbox: [x1, y1, x2, y2] bounding box coordinates
            obj_class: Object class name
            
        Returns:
            Estimated distance in meters
        """
        x1, y1, x2, y2 = bbox
        pixel_height = y2 - y1
        
        if pixel_height <= 0:
            return 10.0  # Default far distance
        
        # Get known real-world height for this object class
        real_height = self.object_heights.get(obj_class.lower())
        
        if real_height is None:
            # Fallback: use generic heuristic
            return max(0.5, 10.0 / (pixel_height / 100))
        
        # Pinhole camera formula
        distance = (real_height * self.focal_length) / pixel_height
        
        # Sanity check: clamp to reasonable range (0.3m to 50m)
        distance = max(0.3, min(distance, 50.0))
        
        return distance
    
    def calibrate_focal_length(self, known_distance: float, pixel_height: float, 
                               real_height: float) -> float:
        """
        Calculate focal length from a known measurement
        Focal_Length = (Pixel_Height × Known_Distance) / Real_Height
        
        Usage:
        1. Place object at known distance (e.g., 2 meters)
        2. Measure pixel height in image
        3. Call this function
        4. Update CAMERA_FOCAL_LENGTH_PX in .env
        """
        focal_length = (pixel_height * known_distance) / real_height
        print(f"📐 Calibrated focal length: {focal_length:.2f} pixels")
        print(f"💡 Add to .env: CAMERA_FOCAL_LENGTH_PX={focal_length:.2f}")
        return focal_length


#  ASYNC HTTP CLIENT FOR ESP32

class ESP32AlertClient:
    """Async HTTP client for ESP32 communication"""
    
    def __init__(self):
        self.client: Optional[httpx.AsyncClient] = None
        self.enabled = True
    
    async def start(self):
        """Initialize async HTTP client"""
        self.client = httpx.AsyncClient(
            timeout=settings.esp32_timeout,
            limits=httpx.Limits(max_keepalive_connections=5, max_connections=10)
        )
        print(f"🌐 ESP32 client initialized (URL: {settings.esp32_audio_url})")
    
    async def stop(self):
        """Close async HTTP client"""
        if self.client:
            await self.client.aclose()
            print("🌐 ESP32 client closed")
    
    async def send_alert(self, obj_class: str, distance: float, alert_type: str) -> bool:
        """
        Send alert to ESP32 using async HTTP
        
        Returns:
            True if successful, False otherwise
        """
        if not self.enabled or not self.client:
            return False
        
        payload = {
            "object": obj_class,
            "distance": round(distance, 2),
            "type": alert_type
        }
        
        try:
            response = await self.client.post(
                settings.esp32_audio_url,
                json=payload
            )
            
            if response.status_code == 200:
                print(f"🔊 {alert_type.upper()} ALERT → {payload}")
                return True
            else:
                print(f"⚠️  ESP32 responded with status {response.status_code}")
                return False
                
        except httpx.TimeoutException:
            print(f"⏱️  ESP32 request timeout (>{settings.esp32_timeout}s)")
            return False
        except httpx.ConnectError:
            print(f"❌ Cannot connect to ESP32 at {settings.esp32_audio_url}")
            return False
        except Exception as e:
            print(f"❌ ESP32 alert failed: {e}")
            return False
    
    def toggle(self):
        """Toggle ESP32 alerts on/off"""
        self.enabled = not self.enabled
        print(f"🔁 ESP32 alerts: {'ON' if self.enabled else 'OFF'}")
        return self.enabled

# ============================================================================
#  BACKGROUND TASKS
# ============================================================================

async def memory_cleanup_task(memory: ObjectMemory):
    """Background task to periodically clean up stale tracks"""
    while True:
        await asyncio.sleep(settings.memory_cleanup_interval)
        await memory.cleanup_stale_tracks()
        stats = await memory.get_stats()
        print(f"📊 Memory stats: {stats}")

#  APPLICATION LIFESPAN MANAGEMENT

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Manage application startup and shutdown"""
    # Startup
    print("🚀 Starting application...")
    
    # Initialize ESP32 client
    await app.state.esp32_client.start()
    
    # Start background cleanup task
    cleanup_task = asyncio.create_task(
        memory_cleanup_task(app.state.object_memory)
    )
    
    print("✅ Application started")
    
    yield
    
    # Shutdown
    print("🛑 Shutting down application...")
    cleanup_task.cancel()
    await app.state.esp32_client.stop()
    cv2.destroyAllWindows()
    print("✅ Application stopped")

#  FASTAPI APPLICATION

app = FastAPI(lifespan=lifespan)

# Initialize application state
app.state.object_memory = ObjectMemory()
app.state.esp32_client = ESP32AlertClient()
app.state.distance_estimator = DistanceEstimator()
app.state.display_enabled = settings.display_enabled

# Load YOLO model
print(f"🧠 Loading YOLO model from {settings.yolo_model_path}...")
model = YOLO(settings.yolo_model_path)
print("✅ YOLO loaded")

# Alert object classes
ALERT_CLASSES = {
    "car", "bicycle", "motorcycle", "bus", "truck", "train", "person"
}

#  HELPER FUNCTIONS

def _overlay_status(img):
    """Overlay current mode on the video frame"""
    mode = f"ESP32 ALERTS: {'ON' if app.state.esp32_client.enabled else 'OFF'}"
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
        track_id = det.get('track_id')
        distance = det.get('distance', 0)
        
        x1, y1, x2, y2 = map(int, bbox)
        
        # Color based on alert priority
        if class_name in ALERT_CLASSES:
            color = (0, 0, 255)  # Red for alert objects
        else:
            color = (0, 255, 0)  # Green for other objects
        
        # Draw bounding box
        cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
        
        # Prepare label text
        label = f"{class_name} {confidence:.2f}"
        if track_id is not None:
            label += f" ID:{track_id}"
        if distance > 0:
            label += f" {distance:.1f}m"
        
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
    if not app.state.display_enabled:
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
    key = cv2.waitKey(1) & 0xFF
    
    if key == ord('t'):
        app.state.esp32_client.toggle()
    elif key == ord('q'):
        app.state.display_enabled = False
        cv2.destroyAllWindows()
        print("🛑 Display window closed (server still running)")

# ============================================================================
#  API ENDPOINTS
# ============================================================================

@app.post("/frame")
async def receive_frame(file: UploadFile = File(...)):
    """
    Process uploaded frame and detect objects
    
    Returns detection results and sends alerts to ESP32 if needed
    """
    try:
        contents = await file.read()
        img = Image.open(BytesIO(contents)).convert("RGB")
        
        # Convert PIL Image to numpy array for OpenCV
        img_array = np.array(img)
        img_array = cv2.cvtColor(img_array, cv2.COLOR_RGB2BGR)

        # Detection and tracking
        results = model.track(img, persist=True)

        detections = []
        alert_tasks = []  # Collect async alert tasks

        for result in results:
            for box in result.boxes:
                class_name = model.names[int(box.cls[0])]
                confidence = float(box.conf[0])
                
                # Skip low confidence detections
                if confidence < settings.confidence_threshold:
                    continue
                
                # Get track ID
                track_id = int(box.id[0]) if box.id is not None else None
                bbox = box.xyxy[0].tolist()
                
                # Estimate distance using proper calibration
                distance = app.state.distance_estimator.estimate_distance(bbox, class_name)
                
                detections.append({
                    "class": class_name,
                    "confidence": confidence,
                    "bbox": bbox,
                    "track_id": track_id,
                    "distance": distance
                })

                # Alert logic (only for tracked objects in alert classes)
                if (class_name in ALERT_CLASSES 
                    and track_id is not None):
                    
                    alert_type = await app.state.object_memory.update(track_id, distance)
                    
                    if alert_type:
                        # Schedule async alert (non-blocking)
                        alert_task = asyncio.create_task(
                            app.state.esp32_client.send_alert(class_name, distance, alert_type)
                        )
                        alert_tasks.append(alert_task)

        # Wait for all alerts to complete (with timeout)
        if alert_tasks:
            await asyncio.wait(alert_tasks, timeout=1.0)

        # Display frame with detections
        if app.state.display_enabled:
            display_frame(img_array, detections)

        return JSONResponse({
            "success": True,
            "detections": detections,
            "total_tracked": len([d for d in detections if d['track_id'] is not None])
        })

    except Exception as e:
        print(f"❌ Error processing frame: {e}")
        return JSONResponse({
            "success": False,
            "error": str(e)
        }, status_code=500)

@app.get("/")
def root():
    """API health check"""
    return {
        "message": "YOLO Detection Server",
        "status": "running",
        "model": settings.yolo_model_path,
        "esp32_enabled": app.state.esp32_client.enabled
    }

@app.get("/stats")
async def get_stats():
    """Get server statistics"""
    memory_stats = await app.state.object_memory.get_stats()
    return {
        "memory": memory_stats,
        "config": {
            "danger_distance": settings.danger_distance_m,
            "alert_cooldown": settings.alert_cooldown,
            "focal_length": settings.camera_focal_length_px
        },
        "esp32": {
            "enabled": app.state.esp32_client.enabled,
            "url": settings.esp32_audio_url
        }
    }

@app.post("/calibrate")
async def calibrate_camera(
    known_distance: float,
    pixel_height: float,
    real_height: float
):
    """
    Calibrate camera focal length
    
    Example: Place a car (1.5m tall) at 5 meters distance,
    measure pixel height (e.g., 200px), then call:
    POST /calibrate?known_distance=5.0&pixel_height=200&real_height=1.5
    """
    focal_length = app.state.distance_estimator.calibrate_focal_length(
        known_distance, pixel_height, real_height
    )
    return {
        "calibrated_focal_length": focal_length,
        "message": f"Add CAMERA_FOCAL_LENGTH_PX={focal_length:.2f} to your .env file"
    }

@app.post("/fall_alert")
async def fall_alert(request: Request):
    """
    Receive fall detection alert from ESP32-3 (Fall Detection Unit)
    
    Expected payload:
    {
        "event": "fall_detected",
        "timestamp": 12345
    }
    """
    try:
        data = await request.json()
        
        print("\n ═══════════════════════════════════════")
        print(" FALL DETECTED!")
        print(" ═══════════════════════════════════════")
        print(f"   Timestamp: {data.get('timestamp', 0)}ms")
        print(f"   Event: {data.get('event', 'fall_detected')}")
        print(f"   Time: {time.strftime('%H:%M:%S')}")
        
        # Trigger audio alert using ASYNC HTTP (improved!)
        if app.state.esp32_client.enabled:
            success = await app.state.esp32_client.send_alert(
                "emergency",
                0.0,
                "fall_alert"
            )
            if success:
                print(f"   ✅ Emergency alert sent to audio unit")
            else:
                print(f"   ⚠️  Failed to send emergency alert")
        
        print("🚨 ═══════════════════════════════════════\n")
        
        return {"success": True, "message": "Fall alert logged"}
        
    except Exception as e:
        print(f"❌ Error processing fall alert: {e}")
        return {"success": False, "error": str(e)}
    
#  MAIN ENTRY POINT

if __name__ == "__main__":
    print("\n" + "="*60)
    print("🎯 YOLO Detection Server - Enhanced Version")
    print("="*60)
    print(f"📹 Model: {settings.yolo_model_path}")
    print(f"🎯 Confidence threshold: {settings.confidence_threshold}")
    print(f"⚠️  Danger distance: {settings.danger_distance_m}m")
    print(f"🔊 ESP32 URL: {settings.esp32_audio_url}")
    print(f"📐 Focal length: {settings.camera_focal_length_px}px")
    print(f"🧹 Memory cleanup: every {settings.memory_cleanup_interval}s")
    print(f"💾 Max track age: {settings.memory_max_age}s")
    print("="*60)
    
    if settings.display_enabled:
        print("🎥 Video display: ENABLED")
        print("💡 Controls: 't' = toggle ESP32 alerts, 'q' = close window")
    else:
        print("🎥 Video display: DISABLED")
    
    print(f"📡 Server starting on http://0.0.0.0:8000")
    print("📚 API docs: http://localhost:8000/docs")
    print("="*60 + "\n")
    
    try:
        uvicorn.run(app, host="0.0.0.0", port=8000)
    except KeyboardInterrupt:
        print("\n🛑 Shutting down...")
        cv2.destroyAllWindows()


#ts pmo fr fr icl chat