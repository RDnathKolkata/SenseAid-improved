// #include "esp_camera.h"
// #include <WiFi.h>
// #include <HTTPClient.h>

// /*  USER CONFIG  */

// const char* ssid     = "YOUR_WIFI_NAME";
// const char* password = "YOUR_WIFI_PASSWORD";

// // Laptop YOLO server endpoint
// const char* serverUrl = "http://LAPTOP_IP:8000/frame";


// // ESP32-CAM pin definition
// #define CAMERA_MODEL_AI_THINKER
// #include "camera_pins.h"

// /* Camera */
// void setupCamera() {
//   camera_config_t config;
//   config.ledc_channel = LEDC_CHANNEL_0;
//   config.ledc_timer   = LEDC_TIMER_0;
//   config.pin_d0       = Y2_GPIO_NUM;
//   config.pin_d1       = Y3_GPIO_NUM;
//   config.pin_d2       = Y4_GPIO_NUM;
//   config.pin_d3       = Y5_GPIO_NUM;
//   config.pin_d4       = Y6_GPIO_NUM;
//   config.pin_d5       = Y7_GPIO_NUM;
//   config.pin_d6       = Y8_GPIO_NUM;
//   config.pin_d7       = Y9_GPIO_NUM;
//   config.pin_xclk     = XCLK_GPIO_NUM;
//   config.pin_pclk     = PCLK_GPIO_NUM;
//   config.pin_vsync    = VSYNC_GPIO_NUM;
//   config.pin_href     = HREF_GPIO_NUM;
//   config.pin_sccb_sda = SIOD_GPIO_NUM;
//   config.pin_sccb_scl = SIOC_GPIO_NUM;
//   config.pin_pwdn     = PWDN_GPIO_NUM;
//   config.pin_reset    = RESET_GPIO_NUM;

//   config.xclk_freq_hz = 20000000;
//   config.pixel_format = PIXFORMAT_JPEG;

//   // IMPORTANT: Stable + fast
//   config.frame_size   = FRAMESIZE_QVGA; // 320x240
//   config.jpeg_quality = 35;             // prolly needs tuning cuz no way 35
//   config.fb_count     = 1;

//   esp_err_t err = esp_camera_init(&config);
//   if (err != ESP_OK) {
//     Serial.printf("Camera init failed: 0x%x\n", err);
//     while (true);
//   }

//   Serial.println("Camera initialized");
// }

// /* ---------- Setup ---------- */
// void setup() {
//   Serial.begin(115200);
//   delay(1000);

//   Serial.println("\nESP32-CAM Sender Starting");

//   // WiFi
//   WiFi.begin(ssid, password);
//   Serial.print("📡 Connecting to WiFi");
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }

//   Serial.println("\nWiFi connected");
//   Serial.print("ESP32-CAM IP: ");
//   Serial.println(WiFi.localIP());

//   setupCamera();
// }

// /* Main */
// void loop() {
//   camera_fb_t *fb = esp_camera_fb_get();
//   if (!fb) {
//     Serial.println("Camera capture failed");
//     delay(200);
//     return;
//   }

//   HTTPClient http;
//   http.begin(serverUrl);
//   http.addHeader("Content-Type", "image/jpeg");

//   int responseCode = http.POST(fb->buf, fb->len);

//   if (responseCode > 0) {
//     Serial.println("Frame sent");
//   } else {
//     Serial.println("Failed to send frame");
//   }

//   http.end();
//   esp_camera_fb_return(fb);

//   delay(200); // ~5 FPS (CRITICAL FOR STABILITY)
// }








//  PerceptaLucis™
//  © 2026 Rajdeep Debnath
//  CC BY-NC-SA 4.0

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // For parsing responses

/*  USER CONFIG  */

const char* ssid     = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// Laptop YOLO server endpoint
const char* serverUrl = "http://LAPTOP_IP:8000/frame";

// Performance tuning
#define FRAME_DELAY_MS 200        // Base delay (5 FPS)
#define MAX_RETRY_ATTEMPTS 3
#define WIFI_TIMEOUT_MS 10000
#define HTTP_TIMEOUT_MS 5000

// Stats
unsigned long framesSent = 0;
unsigned long framesFailed = 0;
unsigned long lastStatsTime = 0;

// ESP32-CAM pin definition
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

/* Camera Setup */
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Optimized settings for YOLO
  config.frame_size   = FRAMESIZE_QVGA;  // 320x240 - perfect for YOLO
  config.jpeg_quality = 12;              // aro komate hobe mp
  config.fb_count     = 2;               // Double buffering for stability

  // camera chalu
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    while (true) delay(1000);
  }

  // Camera sensor adjustments -- might comment out if doesnt work
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_hmirror(s, 0);        // 0 or 1
    s->set_vflip(s, 0);          // 0 or 1
  }

  Serial.println("✅ Camera initialized");
  Serial.printf("   Resolution: 320x240\n");
  Serial.printf("   JPEG Quality: 12\n");
  Serial.printf("   Frame Rate: ~5 FPS\n");
}

/* WiFi Connection with timeout */
bool connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("📡 Connecting to WiFi");
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_TIMEOUT_MS) {
      Serial.println("\n❌ WiFi connection timeout");
      return false;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n WiFi connected");
  Serial.printf(" ESP32-CAM IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf(" Server: %s\n", serverUrl);
  return true;
}

/* Send frame with retry logic */
bool sendFrame(camera_fb_t *fb, int &detectionCount) {
  for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++) {
    HTTPClient http;
    http.begin(serverUrl);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "image/jpeg");

    int responseCode = http.POST(fb->buf, fb->len);

    if (responseCode == 200) {
      // Parse response to get detection count
      String response = http.getString();
      
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error && doc["success"]) {
        detectionCount = doc["detections"].size();
        Serial.printf("✅ Frame sent (attempt %d) - %d detections\n", 
                      attempt, detectionCount);
        http.end();
        return true;
      }
      
      Serial.printf("✅ Frame sent (attempt %d)\n", attempt);
      http.end();
      return true;
    }
    else if (responseCode > 0) {
      Serial.printf("⚠️ Server error: %d (attempt %d)\n", responseCode, attempt);
    }
    else {
      Serial.printf("❌ Connection failed: %s (attempt %d)\n", 
                    http.errorToString(responseCode).c_str(), attempt);
    }

    http.end();

    if (attempt < MAX_RETRY_ATTEMPTS) {
      delay(100 * attempt); // Exponential backoff
    }
  }

  return false;
}

/* Print statistics */
void printStats() {
  unsigned long now = millis();
  if (now - lastStatsTime >= 10000) { // Every 10 seconds
    float successRate = (framesSent > 0) 
      ? (float)framesSent / (framesSent + framesFailed) * 100 
      : 0;
    
    Serial.println("\n📊 Statistics:");
    Serial.printf("   Frames sent: %lu\n", framesSent);
    Serial.printf("   Frames failed: %lu\n", framesFailed);
    Serial.printf("   Success rate: %.1f%%\n", successRate);
    Serial.printf("   WiFi RSSI: %d dBm\n", WiFi.RSSI());
    Serial.println();
    
    lastStatsTime = now;
  }
}

/*  Setup  */
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n🚀 ESP32-CAM YOLO Sender Starting");
  Serial.println("================================");

  // Connect WiFi
  if (!connectWiFi()) {
    Serial.println("❌ Startup failed - cannot connect to WiFi");
    while (true) delay(1000);
  }

  // Setup camera
  setupCamera();

  Serial.println("================================");
  Serial.println("✅ System ready - starting capture loop\n");
}

/*  Main Loop  */
void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected - reconnecting...");
    framesFailed++;
    if (connectWiFi()) {
      Serial.println("✅ WiFi reconnected");
    } else {
      delay(5000);
      return;
    }
  }

  // Capture frame
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera capture failed");
    framesFailed++;
    delay(FRAME_DELAY_MS);
    return;
  }

  // Send frame
  int detectionCount = 0;
  bool success = sendFrame(fb, detectionCount);

  if (success) {
    framesSent++;
    
    // Optional: Blink LED on detection
    if (detectionCount > 0) {
      digitalWrite(4, HIGH); // Flash LED (GPIO 4 on AI-Thinker)
      delay(50);
      digitalWrite(4, LOW);
    }
  } else {
    framesFailed++;
  }

  // Cleanup
  esp_camera_fb_return(fb);

  // Print stats periodically
  printStats();

  // Adaptive delay based on detections (optional)
  int adaptiveDelay = FRAME_DELAY_MS;
  if (detectionCount > 0) {
    adaptiveDelay = 100; // Faster when objects detected (10 FPS)
  }
  
  delay(adaptiveDelay);
}
