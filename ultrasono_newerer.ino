/*
PerceptaLucis™
© 2026 Rajdeep Debnath
CC BY-NC-SA 4.0

 * ESP32 Ultrasonic + Haptic Feedback System
 * 
 * Hardware:
 * - 3× HC-SR04 Ultrasonic Sensors (Left, Center, Right)
 * - 3× Vibration Motors with H-Bridge control
 * - 1× Servo Motor (optional scanning)
 * 
 * Features:
 * - Directional haptic feedback based on obstacle proximity
 * - Adjustable detection range via joystick
 * - Individual sensor enable/disable
 * - Serial commands from joystick ESP32
 * - WiFi alerts to laptop
 * 
 * Pin Definitions:
 * Sensor 1 (LEFT):   Trig=13, Echo=12, Motor: Fwd=32, Bwd=35, PWM=18
 * Sensor 2 (CENTER): Trig=14, Echo=27, Motor: Fwd=33, Bwd=24, PWM=19
 * Sensor 3 (RIGHT):  Trig=26, Echo=25, Motor: Fwd=15, Bwd=4,  PWM=5
 * Servo: Pin 23
 */

#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>

/* USER Configs */

// WiFi credentials (optional - for laptop alerts)
const char* ssid = "~";
const char* password = "~";

// Laptop endpoint for obstacle alerts
const char* laptopAlertUrl = "http://192.168.1.100:8000/obstacle_alert";

// Sensor positions
enum SensorPosition {
  SENSOR_LEFT = 0,
  SENSOR_CENTER = 1,
  SENSOR_RIGHT = 2
};

/* PIN DEFINING */

// Servo
Servo myServo;
const int servo1 = 23;

// Sensor 1 (LEFT)
const int trigPin = 13, echoPin = 12;
const int foreward = 32, backward = 35, pwmPin = 18;

// Sensor 2 (CENTER)
const int trigPin2 = 14, echoPin2 = 27;
const int foreward2 = 33, backward2 = 25, pwmPin2 = 19;

// Sensor 3 (RIGHT)
const int trigPin3 = 26, echoPin3 = 25;
const int foreward3 = 15, backward3 = 4, pwmPin3 = 5;

/* GLOBALIZATION IN VARIABLES cant escape sustainable development HHAHAH */

// Detection ranges (in cm) - adjustable per sensor //PLEASE CHANGE VALUES HERE
int radius = 100;   // LEFT sensor
int radius2 = 100;  // CENTER sensor
int radius3 = 100;  // RIGHT sensor

// Master range (controlled by joystick)
int masterRange = 100;

// Motor PWM values
int pwm = 0, pwm2 = 0, pwm3 = 0;

// Timing (change if needed)
unsigned long servoMillis = 0;
const long servoInterval = 10;
unsigned long sensorMillis = 0;
const long sensorInterval = 50;  // 20Hz sensor reading
unsigned long printMillis = 0;
const long printInterval = 500;  // Print every 0.5s
unsigned long alertMillis = 0;
const long alertInterval = 2000;  // Alert laptop every 2s

// Servo state
int servoPos = 90;
bool increasing = true;
bool servoEnabled = false;  // Servo scanning on/off

// System states
bool vibrationActive = true;   // Master vibration toggle
bool sensorEnabled[3] = {true, true, true};  // Individual sensor enable
bool fallAlertsEnabled = true;

// Current distances
long distance1 = -1, distance2 = -1, distance3 = -1;

// Active sensor focus (for joystick control)
int activeSensor = SENSOR_CENTER;  // 0=LEFT, 1=CENTER, 2=RIGHT

/* ============================================
   ULTRASONIC FUNCTIONS
   ============================================ */

long readDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000);  // 30ms timeout
  if (duration == 0) return -1;  // No echo
  
  return duration / 29 / 2;  // Convert to cm
}

/* ============================================
   MOTOR CONTROL
   ============================================ */

void controlMotor(int fwdPin, int bwdPin, int pwmPinNum, int pwmValue, bool enabled) {
  if (!vibrationActive || !enabled || pwmValue == 0) {
    // Turn off motor
    digitalWrite(fwdPin, LOW);
    digitalWrite(bwdPin, LOW);
    analogWrite(pwmPinNum, 0);
    return;
  }
  
  // Vibration pattern: alternate forward/backward for buzzing effect
  static unsigned long lastSwitch = 0;
  static bool direction = true;
  
  if (millis() - lastSwitch > 50) {  // Switch every 50ms
    direction = !direction;
    lastSwitch = millis();
  }
  
  if (direction) {
    digitalWrite(fwdPin, HIGH);
    digitalWrite(bwdPin, LOW);
  } else {
    digitalWrite(fwdPin, LOW);
    digitalWrite(bwdPin, HIGH);
  }
  
  analogWrite(pwmPinNum, pwmValue);
}

/* ============================================
   NETWORK FUNCTIONS
   ============================================ */

void sendObstacleAlert(int sensor, long distance) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  http.begin(laptopAlertUrl);
  http.setTimeout(1000);
  http.addHeader("Content-Type", "application/json");
  
  String position = (sensor == SENSOR_LEFT) ? "left" : 
                    (sensor == SENSOR_CENTER) ? "center" : "right";
  
  String payload = "{\"sensor\":\"" + position + "\",\"distance\":" + String(distance) + "}";
  
  int responseCode = http.POST(payload);
  if (responseCode > 0) {
    Serial.printf("📡 Obstacle alert sent: %s @ %ldcm\n", position.c_str(), distance);
  }
  
  http.end();
}

/* ============================================
   SERIAL COMMAND HANDLER
   ============================================ */

void handleSerialCommands() {
  if (!Serial.available()) return;
  
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  
  Serial.printf("📥 Command received: %s\n", cmd.c_str());
  
  // Time query
  if (cmd == "CMD_TIME") {
    Serial.printf("⏰ System uptime: %lu seconds\n", millis() / 1000);
  }
  
  // Vibration toggle
  else if (cmd == "CMD_VIB_TOGGLE") {
    vibrationActive = !vibrationActive;
    Serial.printf("📳 Vibration motors: %s\n", vibrationActive ? "ON" : "OFF");
  }
  
  // Range adjustment (affects all sensors)
  else if (cmd == "CMD_RANGE_UP") {
    masterRange += 20;
    if (masterRange > 400) masterRange = 400;
    radius = radius2 = radius3 = masterRange;
    Serial.printf("📏 Detection range: %d cm\n", masterRange);
  }
  else if (cmd == "CMD_RANGE_DOWN") {
    masterRange -= 20;
    if (masterRange < 20) masterRange = 20;
    radius = radius2 = radius3 = masterRange;
    Serial.printf("📏 Detection range: %d cm\n", masterRange);
  }
  
  // Fall detection toggle (forwarded from fall detection ESP32)
  else if (cmd == "CMD_FALL_TOGGLE") {
    fallAlertsEnabled = !fallAlertsEnabled;
    Serial.printf("🚨 Fall alerts: %s\n", fallAlertsEnabled ? "ENABLED" : "DISABLED");
  }
  
  // Servo scanning toggle
  else if (cmd == "CMD_SERVO_TOGGLE") {
    servoEnabled = !servoEnabled;
    if (!servoEnabled) myServo.write(90);  // Center position
    Serial.printf("🔄 Servo scanning: %s\n", servoEnabled ? "ON" : "OFF");
  }
  
  // Individual sensor control
  else if (cmd == "CMD_SENSOR_LEFT") {
    sensorEnabled[SENSOR_LEFT] = !sensorEnabled[SENSOR_LEFT];
    Serial.printf("👈 Left sensor: %s\n", sensorEnabled[SENSOR_LEFT] ? "ON" : "OFF");
  }
  else if (cmd == "CMD_SENSOR_CENTER") {
    sensorEnabled[SENSOR_CENTER] = !sensorEnabled[SENSOR_CENTER];
    Serial.printf("⬆️ Center sensor: %s\n", sensorEnabled[SENSOR_CENTER] ? "ON" : "OFF");
  }
  else if (cmd == "CMD_SENSOR_RIGHT") {
    sensorEnabled[SENSOR_RIGHT] = !sensorEnabled[SENSOR_RIGHT];
    Serial.printf("👉 Right sensor: %s\n", sensorEnabled[SENSOR_RIGHT] ? "ON" : "OFF");
  }
  
  // Focus on specific sensor
  else if (cmd == "CMD_FOCUS_LEFT") {
    activeSensor = SENSOR_LEFT;
    Serial.println("👈 Focused on LEFT sensor");
  }
  else if (cmd == "CMD_FOCUS_CENTER") {
    activeSensor = SENSOR_CENTER;
    Serial.println("⬆️ Focused on CENTER sensor");
  }
  else if (cmd == "CMD_FOCUS_RIGHT") {
    activeSensor = SENSOR_RIGHT;
    Serial.println("👉 Focused on RIGHT sensor");
  }
  
  // Query current sensor
  else if (cmd == "CMD_QUERY") {
    String pos = (activeSensor == SENSOR_LEFT) ? "LEFT" : 
                 (activeSensor == SENSOR_CENTER) ? "CENTER" : "RIGHT";
    long dist = (activeSensor == SENSOR_LEFT) ? distance1 : 
                (activeSensor == SENSOR_CENTER) ? distance2 : distance3;
    
    if (dist > 0 && dist <= masterRange) {
      Serial.printf("📍 %s: Obstacle at %ld cm\n", pos.c_str(), dist);
    } else {
      Serial.printf("📍 %s: Clear (>%d cm)\n", pos.c_str(), masterRange);
    }
  }
  
  else {
    Serial.printf("❓ Unknown command: %s\n", cmd.c_str());
  }
}

/* ============================================
   SETUP
   ============================================ */

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n");
  Serial.println("╔═══════════════════════════════════════╗");
  Serial.println("║  📡 ULTRASONIC HAPTIC SYSTEM 📡      ║");
  Serial.println("╚═══════════════════════════════════════╝");
  Serial.println();
  
  // Setup servo
  myServo.attach(servo1);
  myServo.write(90);  // Center position
  Serial.println("✅ Servo initialized (centered)");
  
  // Setup Sensor 1 (LEFT)
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(foreward, OUTPUT);
  pinMode(backward, OUTPUT);
  pinMode(pwmPin, OUTPUT);
  Serial.println("✅ Sensor 1 (LEFT) initialized");
  
  // Setup Sensor 2 (CENTER)
  pinMode(trigPin2, OUTPUT);
  pinMode(echoPin2, INPUT);
  pinMode(foreward2, OUTPUT);
  pinMode(backward2, OUTPUT);
  pinMode(pwmPin2, OUTPUT);
  Serial.println("✅ Sensor 2 (CENTER) initialized");
  
  // Setup Sensor 3 (RIGHT)
  pinMode(trigPin3, OUTPUT);
  pinMode(echoPin3, INPUT);
  pinMode(foreward3, OUTPUT);
  pinMode(backward3, OUTPUT);
  pinMode(pwmPin3, OUTPUT);
  Serial.println("✅ Sensor 3 (RIGHT) initialized");
  
  // WiFi (optional)
  WiFi.begin(ssid, password);
  Serial.print("📡 Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi connected");
    Serial.printf("📍 IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("⚠️  WiFi failed - operating offline");
  }
  
  Serial.println();
  Serial.println("╔═══════════════════════════════════════╗");
  Serial.println("║  ✅ SYSTEM READY                      ║");
  Serial.println("║  📊 Monitoring obstacles...           ║");
  Serial.println("╚═══════════════════════════════════════╝");
  Serial.println();
  Serial.printf("📏 Detection range: %d cm\n", masterRange);
  Serial.printf("📳 Vibration: %s\n", vibrationActive ? "ON" : "OFF");
  Serial.println();
}

/*  MAIN LOOP  */

void loop() {
  unsigned long currentMillis = millis();
  
  // Handle serial commands from joystick ESP32
  handleSerialCommands();
  
  // Servo sweep (if enabled)
  if (servoEnabled && currentMillis - servoMillis >= servoInterval) {
    servoMillis = currentMillis;
    if (increasing) {
      servoPos++;
      if (servoPos >= 180) increasing = false;
    } else {
      servoPos--;
      if (servoPos <= 0) increasing = true;
    }
    myServo.write(servoPos);
  }
  
  // Sensor reading and motor control
  if (currentMillis - sensorMillis >= sensorInterval) {
    sensorMillis = currentMillis;
    
    // Read all three sensors
    distance1 = readDistance(trigPin, echoPin);    // LEFT
    distance2 = readDistance(trigPin2, echoPin2);  // CENTER
    distance3 = readDistance(trigPin3, echoPin3);  // RIGHT
    
    // === MOTOR 1 (LEFT) ===
    if (distance1 > radius || distance1 < 0) {
      pwm = 0;
    } else {
      // Closer = stronger vibration
      pwm = map(distance1, 1, radius, 255, 50);
      pwm = constrain(pwm, 50, 255);
    }
    controlMotor(foreward, backward, pwmPin, pwm, sensorEnabled[SENSOR_LEFT]);
    
    // === MOTOR 2 (CENTER) ===
    if (distance2 > radius2 || distance2 < 0) {
      pwm2 = 0;
    } else {
      pwm2 = map(distance2, 1, radius2, 255, 50);
      pwm2 = constrain(pwm2, 50, 255);
    }
    controlMotor(foreward2, backward2, pwmPin2, pwm2, sensorEnabled[SENSOR_CENTER]);
    
    // === MOTOR 3 (RIGHT) ===
    if (distance3 > radius3 || distance3 < 0) {
      pwm3 = 0;
    } else {
      pwm3 = map(distance3, 1, radius3, 255, 50);
      pwm3 = constrain(pwm3, 50, 255);
    }
    controlMotor(foreward3, backward3, pwmPin3, pwm3, sensorEnabled[SENSOR_RIGHT]);
  }
  
  // Print status periodically
  if (currentMillis - printMillis >= printInterval) {
    printMillis = currentMillis;
    
    Serial.println("─────────────────────────────────");
    
    // Sensor 1 (LEFT)
    Serial.printf("👈 LEFT:   %s | ", sensorEnabled[SENSOR_LEFT] ? "ON " : "OFF");
    if (distance1 > 0 && distance1 <= radius) {
      Serial.printf("%3ld cm | PWM: %3d\n", distance1, pwm);
    } else {
      Serial.println("Clear");
    }
    
    // Sensor 2 (CENTER)
    Serial.printf("⬆️  CENTER: %s | ", sensorEnabled[SENSOR_CENTER] ? "ON " : "OFF");
    if (distance2 > 0 && distance2 <= radius2) {
      Serial.printf("%3ld cm | PWM: %3d\n", distance2, pwm2);
    } else {
      Serial.println("Clear");
    }
    
    // Sensor 3 (RIGHT)
    Serial.printf("👉 RIGHT:  %s | ", sensorEnabled[SENSOR_RIGHT] ? "ON " : "OFF");
    if (distance3 > 0 && distance3 <= radius3) {
      Serial.printf("%3ld cm | PWM: %3d\n", distance3, pwm3);
    } else {
      Serial.println("Clear");
    }
    
    if (servoEnabled) {
      Serial.printf("🔄 Servo: %d°\n", servoPos);
    }
  }
  
  // Send alerts to laptop (if obstacles are close)
  if (currentMillis - alertMillis >= alertInterval) {
    alertMillis = currentMillis;
    
    if (distance1 > 0 && distance1 <= 50) sendObstacleAlert(SENSOR_LEFT, distance1);
    if (distance2 > 0 && distance2 <= 50) sendObstacleAlert(SENSOR_CENTER, distance2);
    if (distance3 > 0 && distance3 <= 50) sendObstacleAlert(SENSOR_RIGHT, distance3);
  }
}


// test
