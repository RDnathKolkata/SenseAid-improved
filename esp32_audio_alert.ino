// #include <WiFi.h>
// #include <WebServer.h>
// #include "AudioTools.h"
// #include "BluetoothA2DPSource.h"

// /* ================= USER CONFIG ================= */

// const char* ssid     = "YOUR_WIFI_NAME";
// const char* password = "YOUR_WIFI_PASSWORD";

// const char* bt_device_name = "ESP32-YOLO-AUDIO";

// /* =============================================== */

// WebServer server(80);
// BluetoothA2DPSource a2dp_source;

// /* --------- Audio Generation --------- */
// int16_t audio_buffer[512];
// volatile int alert_type = 0;
// // 0 = silent, 1 = car, 2 = person, 3 = bicycle

// int sample_rate = 44100;
// float phase = 0.0;

// /* Generate simple tones */
// int32_t get_audio_data(Frame *frame, int32_t frame_count) {
//   for (int i = 0; i < frame_count; i++) {
//     float freq = 0;

//     if (alert_type == 1) freq = 900;      // car
//     else if (alert_type == 2) freq = 600; // person
//     else if (alert_type == 3) freq = 1200;// bicycle
//     else freq = 0;

//     int16_t sample = (freq > 0)
//         ? (int16_t)(sin(phase) * 12000)
//         : 0;

//     phase += 2.0 * PI * freq / sample_rate;
//     if (phase > 2.0 * PI) phase -= 2.0 * PI;

//     frame[i].channel1 = sample;
//     frame[i].channel2 = sample;
//   }
//   return frame_count;
// }

// /* --------- Alert Handler --------- */
// void handleAlert() {
//   String body = server.arg("plain");
//   Serial.println("Alert received:");
//   Serial.println(body);

//   if (body.indexOf("car") >= 0) {
//     alert_type = 1;
//   }
//   else if (body.indexOf("person") >= 0) {
//     alert_type = 2;
//   }
//   else if (body.indexOf("bicycle") >= 0) {
//     alert_type = 3;
//   }
//   else {
//     alert_type = 0;
//   }

//   server.send(200, "application/json", "{\"status\":\"playing\"}");
// }

// /* --------- Setup --------- */
// void setup() {
//   Serial.begin(115200);
//   delay(1000);

//   Serial.println("\nESP32 Bluetooth Audio Alert Starting");

//   // WiFi
//   WiFi.begin(ssid, password);
//   Serial.print("Connecting to WiFi");
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }
//   Serial.println("\nWiFi connected");
//   Serial.println(WiFi.localIP());

//   // HTTP server
//   server.on("/alert", HTTP_POST, handleAlert);
//   server.begin();

//   // Bluetooth Audio
//   a2dp_source.set_stream_reader(get_audio_data);
//   a2dp_source.start(bt_device_name);

//   Serial.println("Bluetooth started");
//   Serial.println("Pair your speaker with ESP32-YOLO-AUDIO");
// }

// /* --------- Loop --------- */
// void loop() {
//   server.handleClient();
// }


// #include <WiFi.h>
// #include <WebServer.h>
// #include <SPI.h>
// #include <SD.h>

// #include "AudioTools.h"
// #include "BluetoothA2DPSource.h"

// /* USER CONFIG */

// const char* ssid     = "YOUR_WIFI_NAME";
// const char* password = "YOUR_WIFI_PASSWORD";

// const char* bt_name  = "ESP32-YOLO-AUDIO";

// // SD card CS pin
// #define SD_CS 5



// WebServer server(80);
// BluetoothA2DPSource a2dp;

// /* Audio pipeline */
// I2SStream i2s;
// WAVDecoder wav;
// File audioFile;

// /* Current alert */
// String currentFile = "";

// /* A2DP callback  */
// int32_t audio_data_callback(Frame* frame, int32_t frame_count) {
//   if (!audioFile || !wav.isActive()) {
//     memset(frame, 0, frame_count * sizeof(Frame));
//     return frame_count;
//   }

//   return wav.read((uint8_t*)frame, frame_count * sizeof(Frame)) / sizeof(Frame);
// }

// /*  Play WAV  */
// void playWav(const char* filename) {
//   if (audioFile) {
//     audioFile.close();
//   }

//   audioFile = SD.open(filename);
//   if (!audioFile) {
//     Serial.println("Failed to open WAV file");
//     return;
//   }

//   wav.begin(audioFile);
//   Serial.printf("Playing %s\n", filename);
// }

// /*  HTTP ALERT */
// void handleAlert() {
//   String body = server.arg("plain");
//   Serial.println("Alert received:");
//   Serial.println(body);

//   if (body.indexOf("car") >= 0) {
//     playWav("/car.wav");
//   }
//   else if (body.indexOf("person") >= 0) {
//     playWav("/person.wav");
//   }
//   else if (body.indexOf("bicycle") >= 0) {
//     playWav("/bicycle.wav");
//   }
//   else {
//     Serial.println("⚠️ Unknown alert");
//   }

//   server.send(200, "application/json", "{\"status\":\"ok\"}");
// }

// /*  SETUP  */
// void setup() {
//   Serial.begin(115200);
//   delay(1000);

//   Serial.println("\nESP32-WROOM Audio Alert Starting");

//   /* WiFi */
//   WiFi.begin(ssid, password);
//   Serial.print("📡 Connecting to WiFi");
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }
//   Serial.println("\nWiFi connected");
//   Serial.println(WiFi.localIP());

//   /* SD card */
//   SPI.begin(18, 19, 23, SD_CS);
//   if (!SD.begin(SD_CS)) {
//     Serial.println(" SD card mount failed");
//     while (true);
//   }
//   Serial.println(" SD card mounted");

//   /* Audio */
//   auto cfg = i2s.defaultConfig(TX_MODE);
//   cfg.sample_rate = 44100;
//   cfg.bits_per_sample = 16;
//   cfg.channels = 2;
//   i2s.begin(cfg);

//   wav.setOutput(i2s);

//   /* Bluetooth */
//   a2dp.set_stream_reader(audio_data_callback);
//   a2dp.start(bt_name);

//   Serial.println("🔵 Bluetooth started");
//   Serial.println("📢 Pair speaker with ESP32-YOLO-AUDIO");

//   /* HTTP server */
//   server.on("/alert", HTTP_POST, handleAlert);
//   server.begin();

//   Serial.println("🚀 Ready: POST alerts to /alert");
// }


// void loop() {
//   server.handleClient();
// }


// # PerceptaLucis™
// # © 2026 Rajdeep Debnath
// # CC BY-NC-SA 4.0

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>  // ← ADD THIS LIBRARY

#include "AudioTools.h"
#include "BluetoothA2DPSource.h"

/* USER CONFIG */

const char* ssid     = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

const char* bt_name  = "ESP32-YOLO-AUDIO";

// SD card CS pin
#define SD_CS 5

WebServer server(80);
BluetoothA2DPSource a2dp;

/* Audio pipeline */
I2SStream i2s;
WAVDecoder wav;
File audioFile;

/* Current alert */
String currentFile = "";
bool isPlaying = false;

/* A2DP callback */
int32_t audio_data_callback(Frame* frame, int32_t frame_count) {
  if (!audioFile || !wav.isActive()) {
    memset(frame, 0, frame_count * sizeof(Frame));
    isPlaying = false;
    return frame_count;
  }

  isPlaying = true;
  return wav.read((uint8_t*)frame, frame_count * sizeof(Frame)) / sizeof(Frame);
}

/* Play WAV */
void playWav(const char* filename) {
  // Allow interrupting current playback for urgent alerts
  if (audioFile) {
    audioFile.close();
  }

  audioFile = SD.open(filename);
  if (!audioFile) {
    Serial.printf("❌ Failed to open: %s\n", filename);
    return;
  }

  wav.begin(audioFile);
  Serial.printf("🔊 Playing: %s\n", filename);
}

/* Select audio file based on object, distance, and alert type */
String selectAudioFile(String object, float distance, String alertType) {
  String basePath = "/";
  
  // Priority: use specific alert type files if available
  // Format: /car_approaching.wav, /car_presence.wav, /car_close.wav
  
  if (alertType == "approaching" && distance < 2.0) {
    // Urgent: close and approaching
    return basePath + object + "_urgent.wav";
  }
  else if (alertType == "approaching") {
    // Warning: approaching
    return basePath + object + "_approaching.wav";
  }
  else if (alertType == "presence") {
    // Info: first detection
    return basePath + object + "_detected.wav";
  }
  
  // Fallback to generic object alert
  return basePath + object + ".wav";
}

/* HTTP ALERT HANDLER */
void handleAlert() {
  String body = server.arg("plain");
  Serial.println("📨 Alert received:");
  Serial.println(body);
  else if (body.indexOf("announce_time") >= 0) {

  // Parse JSON
  StaticJsonDocument<256> doc;
  deserializeJson(doc, body);
  
  int hours = doc["hours"] | 0;
  int minutes = doc["minutes"] | 0;
  
  Serial.printf("Time: %02d:%02d\n", hours, minutes);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }

  // Extract data
  String object = doc["object"] | "unknown";
  float distance = doc["distance"] | 999.0;
  String alertType = doc["type"] | "presence";

  Serial.printf("Object: %s | Distance: %.2fm | Type: %s\n", 
                object.c_str(), distance, alertType.c_str());

  // Select appropriate audio file
  String audioPath = selectAudioFile(object, distance, alertType);
  
  // Check if file exists, fallback to generic
  if (!SD.exists(audioPath.c_str())) {
    Serial.printf("⚠️ File not found: %s, trying generic...\n", audioPath.c_str());
    audioPath = "/" + object + ".wav";
    
    if (!SD.exists(audioPath.c_str())) {
      Serial.println("❌ No audio file available");
      server.send(404, "application/json", "{\"status\":\"error\",\"message\":\"No audio file\"}");
      return;
    }
  }

  // Play the alert
  playWav(audioPath.c_str());

  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

/* Health check endpoint */
void handleRoot() {
  String status = isPlaying ? "playing" : "idle";
  String response = "{\"status\":\"online\",\"audio_status\":\"" + status + "\"}";
  server.send(200, "application/json", response);
}

/* SETUP */
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n🚀 ESP32-WROOM Audio Alert Starting");

  /* WiFi */
  WiFi.begin(ssid, password);
  Serial.print("📡 Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ WiFi connection failed!");
    while(true) delay(1000);
  }
  
  Serial.println("\n✅ WiFi connected");
  Serial.print("📍 IP Address: ");
  Serial.println(WiFi.localIP());

  /* SD card */
  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD card mount failed");
    while (true) delay(1000);
  }
  Serial.println("✅ SD card mounted");

  // List available audio files
  Serial.println("📂 Available audio files:");
  File root = SD.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      Serial.printf("  - %s (%d bytes)\n", entry.name(), entry.size());
    }
    entry.close();
  }
  root.close();

  /* Audio */
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = 16;
  cfg.channels = 2;
  i2s.begin(cfg);

  wav.setOutput(i2s);

  /* Bluetooth */
  a2dp.set_stream_reader(audio_data_callback);
  a2dp.start(bt_name);

  Serial.println("🔵 Bluetooth A2DP started");
  Serial.printf("📢 Pair your speaker with: %s\n", bt_name);

  /* HTTP server */
  server.on("/", HTTP_GET, handleRoot);
  server.on("/alert", HTTP_POST, handleAlert);
  server.begin();

  Serial.println("✅ HTTP server started");
  Serial.printf("🎯 Ready! Send POST to http://%s/alert\n", WiFi.localIP().toString().c_str());
}

void loop() {
  server.handleClient();
  
  // WiFi reconnection logic
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected, reconnecting...");
    WiFi.reconnect();
  }
  
  delay(10);  // Small delay to prevent watchdog issues
}