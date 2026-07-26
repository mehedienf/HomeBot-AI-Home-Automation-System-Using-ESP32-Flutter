// ============================================================================
// HomeBot - ESP32 Firmware (Full Code with Fixed Servo 0-180° Range)
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "time.h"

#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <DHT.h>
#include <ESP32Servo.h>

// OLED Display Libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "secrets.h"
#include "firebase_certs.h"

// ============================================================================
// HARDWARE & DISPLAY CONFIGURATION
// ============================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C // 0.96" OLED I2C Address
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Hardware Pins
#define PIN_RELAY_LIGHT     26
#define PIN_RELAY_PUMP      27
#define PIN_RELAY_HUMID     14
#define PIN_RELAY_FAN       25      // Fan ON/OFF Relay
#define PIN_SERVO_FAN       18      // Servo Signal Pin for Fan Speed
#define PIN_BUZZER          19      // Buzzer Pin for Smoke Alarm
#define PIN_DHT             4
#define PIN_SMOKE           34      // Analog Smoke Sensor
#define PIN_LDR             33      // Digital LDR Sensor Pin (D0)
#define PIN_ECHO            35      // Ultrasonic Echo (Input)
#define PIN_TRIG            32      // Ultrasonic Trig (Output)

// OLED I2C Pins (ESP32 Default)
#define PIN_OLED_SDA        21
#define PIN_OLED_SCL        22

// ============================================================================
// AUTOMATION THRESHOLDS
// ============================================================================
#define TEMP_FAN_ON          27.0  // কত °C তাপমাত্রায় ফ্যান চালু হবে
#define TEMP_FAN_MAX         32.0  // কত °C তাপমাত্রায় ফ্যান ফুল স্পিডে (২৫৫) চলবে
#define TEMP_FAN_OFF         26.0  // কত °C তাপমাত্রার নিচে নামলে ফ্যান সম্পূর্ণ বন্ধ হবে

#define HUMID_HUMIDIFIER_ON  40.0  // আর্দ্রতা কত % এর নিচে নামলে হিউমিডিফায়ার অন হবে
#define HUMID_HUMIDIFIER_OFF 50.0  // আর্দ্রতা কত % এর উপরে উঠলে হিউমিডিফায়ার অফ হবে

// Relay Active Logic
#define RELAY_ON             LOW
#define RELAY_OFF            HIGH

// Timing (milliseconds)
#define SENSOR_PERIOD_MS    3000    // সেন্সর ডাটা রিড ও ডিসপ্লে রিফ্রেশ টাইম (৩ সেকেন্ড)
#define STREAM_RECONNECT_MS 20000   

// Water Tank Calibration (Centimeters)
#define TANK_DEPTH_CM       12     // সেন্সর থেকে পানির সর্বোচ্চ দূরত্ব (খালি = 0%)
#define TANK_FULL_GAP_CM    2      // সেন্সর থেকে পানির সর্বনিম্ন দূরত্ব (ভর্তি = 100%)

// Servo Instance
Servo fanServo;

// ===========================================================================
// STATE STRUCTS
// ===========================================================================
struct DeviceState     { bool light, fan, pump, humidifier; uint8_t fanSpeed; };
struct AutomationState { bool autoFan, autoHumidifier, autoPump, autoLight; };
struct SensorState     { float temperature, humidity; uint8_t waterLevel; bool smokeDetected; int smokeRaw; int lightState; };
struct TimerState { 
  uint64_t fanOffTime; 
  uint64_t humidifierOffTime; 
  uint64_t lightOffTime; 
  uint64_t pumpOffTime; 
};

TimerState      gTimers     = {0, 0, 0, 0};
DeviceState     gDevices    = { false, false, false, false, 0 };
AutomationState gAutomation = { true, true, true, true };
SensorState     gSensors    = { 0.0, 0.0, 0, false, 0, 0 };

// ===========================================================================
// FIREBASE & INSTANCES
// ===========================================================================
FirebaseData    fbdoSensors;       
FirebaseData    fbdoAutoApply;     
FirebaseData    fbdoDevices;       
FirebaseData    fbdoAutomation;    
FirebaseData    fbdoTimers;        
FirebaseAuth    auth;
FirebaseConfig  config;

unsigned long gLastSensorPublish = 0;
unsigned long gLastStreamRestart = 0;
bool          gStreamsStarted    = false;

DHT dht(PIN_DHT, DHT22);

// Forward Declarations
void applyFanServo();
void applyDevicesToHw();
void updateDisplay();
void startStreams();
void onDevicesStream(StreamData data);
void onAutomationStream(StreamData data);
void onTimersStream(StreamData data);
void onStreamTimeout(bool timeout, FirebaseData& fb);
bool parseBoolData(FirebaseJsonData& result);

// ===========================================================================
// HARDWARE INIT & CONTROL
// ===========================================================================
void initPins() {
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  pinMode(PIN_RELAY_PUMP,  OUTPUT);
  pinMode(PIN_RELAY_HUMID, OUTPUT);
  pinMode(PIN_RELAY_FAN,   OUTPUT);
  pinMode(PIN_BUZZER,      OUTPUT);
  pinMode(PIN_SMOKE,       INPUT);
  pinMode(PIN_LDR,         INPUT);  
  
  // Ultrasonic Pins
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  // Initial State: All Relays & Buzzer OFF
  digitalWrite(PIN_RELAY_LIGHT, RELAY_OFF);
  digitalWrite(PIN_RELAY_PUMP,  RELAY_OFF);
  digitalWrite(PIN_RELAY_HUMID, RELAY_OFF);
  digitalWrite(PIN_RELAY_FAN,   RELAY_OFF);
  digitalWrite(PIN_BUZZER,      LOW);
  
  applyFanServo(); 
  dht.begin();
}

void applyFanServo() {
  if (gDevices.fan && gDevices.fanSpeed > 0) {
    // 0-180 ডিগ্রি রেঞ্জের জন্য স্ট্যান্ডার্ড পালস উইডথ (544us-2400us)
    if (!fanServo.attached()) {
      fanServo.attach(PIN_SERVO_FAN, 544, 2400);
    }

    uint8_t spd = constrain(gDevices.fanSpeed, 0, 255);
    int targetAngle = map(spd, 0, 255, 0, 180); // 0 = 0°, 255 = 180°
    fanServo.write(targetAngle);
  } 
  else {
    // ফ্যান বন্ধ বা স্পিড 0 থাকলে সার্ভো 0 ডিগ্রিতে গিয়ে ডিটাচ হবে
    if (fanServo.attached()) {
      fanServo.write(0);
      delay(150);
      fanServo.detach(); // কারেন্ট ও নয়েজ বন্ধ থাকবে
    }
  }
}

void applyDevicesToHw() {
  // রিলে স্টেট প্রয়োগ
  digitalWrite(PIN_RELAY_LIGHT, gDevices.light ? RELAY_ON : RELAY_OFF);
  digitalWrite(PIN_RELAY_PUMP,  gDevices.pump  ? RELAY_ON : RELAY_OFF);
  digitalWrite(PIN_RELAY_HUMID, gDevices.humidifier ? RELAY_ON : RELAY_OFF);
  digitalWrite(PIN_RELAY_FAN,   gDevices.fan ? RELAY_ON : RELAY_OFF);
  
  // সার্ভো কন্ট্রোল ও ডিসপ্লে রিফ্রেশ
  applyFanServo();
  updateDisplay(); 
}

// ===========================================================================
// OLED DISPLAY UPDATE
// ===========================================================================
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(18, 0);
  display.print("--- HOMEBOT ---");

  // Sensor Data Section
  display.setCursor(0, 11);
  display.printf("Temp: %.1fC  Hum: %.0f%%", gSensors.temperature, gSensors.humidity);

  display.setCursor(0, 21);
  display.printf("Water: %d%%   LDR: %s", gSensors.waterLevel, (gSensors.lightState == HIGH ? "Dark" : "Bright"));

  display.setCursor(0, 31);
  if (gSensors.smokeDetected) {
    display.printf("Smoke: ALERT! (%d)", gSensors.smokeRaw);
  } else {
    display.printf("Smoke: SAFE (%d)", gSensors.smokeRaw);
  }

  // Horizontal Line
  display.drawLine(0, 41, 128, 41, SSD1306_WHITE);

  // Devices State Section
  display.setCursor(0, 45);
  display.printf("Lgt:%-3s  Hmd:%-3s", gDevices.light ? "ON" : "OFF", gDevices.humidifier ? "ON" : "OFF");

  display.setCursor(0, 55);
  if (gDevices.fan) {
    int fanPct = map(gDevices.fanSpeed, 0, 255, 0, 100);
    display.printf("Fan:%d%% ", fanPct);
  } else {
    display.print("Fan:OFF  ");
  }
  display.printf(" Pmp:%-3s", gDevices.pump ? "ON" : "OFF");

  display.display();
}

// ===========================================================================
// SENSOR READ & PUBLISH
// ===========================================================================
SensorState readSensors() {
  SensorState s = gSensors;

  // Temperature & Humidity
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) s.temperature = t;
  if (!isnan(h)) s.humidity    = h;

  // Smoke Sensor & Digital LDR Sensor Reading
  s.smokeRaw      = analogRead(PIN_SMOKE);
  s.smokeDetected = s.smokeRaw > 1500;
  s.lightState    = digitalRead(PIN_LDR); 

  // Ultrasonic Water Level Reading
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, 30000); 
  if (duration > 0) {
    float distance_cm = duration * 0.034 / 2.0;
    int level = map((int)distance_cm, TANK_DEPTH_CM, TANK_FULL_GAP_CM, 0, 100);
    s.waterLevel = (uint8_t)constrain(level, 0, 100);
  } else {
    Serial.println("[sensor] Ultrasonic read error/timeout");
  }

  return s;
}

void publishSensors(const SensorState& s) {
  bool ok = true;
  ok &= Firebase.setFloat(fbdoSensors, "/sensors/temperature",    s.temperature);
  ok &= Firebase.setFloat(fbdoSensors, "/sensors/humidity",       s.humidity);
  ok &= Firebase.setInt  (fbdoSensors, "/sensors/water_level",     s.waterLevel);
  ok &= Firebase.setBool (fbdoSensors, "/sensors/smoke_detected", s.smokeDetected);
  ok &= Firebase.setInt  (fbdoSensors, "/sensors/smoke_raw",       s.smokeRaw);
  ok &= Firebase.setInt  (fbdoSensors, "/sensors/light_state",     s.lightState);

  if (ok) {
    Serial.printf("[publish] Sensors updated -> T:%.1f°C, H:%.1f%%, Water:%d%%\n", 
                  s.temperature, s.humidity, s.waterLevel);
  } else {
    Serial.printf("[publish] /sensors FAIL: %s\n", fbdoSensors.errorReason().c_str());
  }
}

// ===========================================================================
// AUTOMATION & SAFETY LOGIC
// ===========================================================================
void handleAutomation() {
  // 1. Smoke Alarm & Safety
  if (gSensors.smokeDetected) {
    digitalWrite(PIN_BUZZER, HIGH); 
    if (gDevices.pump) {
      gDevices.pump = false;
      Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
      applyDevicesToHw();
    }
  } else {
    digitalWrite(PIN_BUZZER, LOW);  
  }

  // 2. Light Automation
  if (gAutomation.autoLight) {
    bool isDark = (gSensors.lightState == HIGH);

    if (!gDevices.light && isDark) {
      gDevices.light = true;
      Firebase.setBool(fbdoAutoApply, "/devices/light", true);
      applyDevicesToHw();
    }
    else if (gDevices.light && !isDark) {
      gDevices.light = false;
      Firebase.setBool(fbdoAutoApply, "/devices/light", false);
      applyDevicesToHw();
    }
  }

  // 3. Fan Automation
  if (gAutomation.autoFan) {
    if (gSensors.temperature >= TEMP_FAN_ON) {
      gDevices.fan = true;
      float tempClamped = constrain(gSensors.temperature, TEMP_FAN_ON, TEMP_FAN_MAX);
      int mappedSpeed = map((int)(tempClamped * 10), (int)(TEMP_FAN_ON * 10), (int)(TEMP_FAN_MAX * 10), 80, 255);
      
      gDevices.fanSpeed = (uint8_t)mappedSpeed;

      Firebase.setBool(fbdoAutoApply, "/devices/fan", true);
      Firebase.setInt (fbdoAutoApply, "/devices/fan_speed", gDevices.fanSpeed);
      applyDevicesToHw();
    }
    else if (gSensors.temperature < TEMP_FAN_OFF) {
      if (gDevices.fan) {
        gDevices.fan = false;
        gDevices.fanSpeed = 0;
        Firebase.setBool(fbdoAutoApply, "/devices/fan", false);
        Firebase.setInt (fbdoAutoApply, "/devices/fan_speed", 0);
        applyDevicesToHw();
      }
    }
  }
  
  // 4. Humidifier Automation
  if (gAutomation.autoHumidifier) {
    if (!gDevices.humidifier && gSensors.humidity < HUMID_HUMIDIFIER_ON) {
      gDevices.humidifier = true;
      Firebase.setBool(fbdoAutoApply, "/devices/humidifier", true);
      applyDevicesToHw();
    }
    else if (gDevices.humidifier && gSensors.humidity >= HUMID_HUMIDIFIER_OFF) {
      gDevices.humidifier = false;
      Firebase.setBool(fbdoAutoApply, "/devices/humidifier", false);
      applyDevicesToHw();
    }
  }
  
  // 5. Water Pump Automation
  if (gAutomation.autoPump) {
    if (!gDevices.pump && gSensors.waterLevel < 20) {
      gDevices.pump = true;
      Firebase.setBool(fbdoAutoApply, "/devices/pump", true);
      applyDevicesToHw();
    }
    else if (gDevices.pump && gSensors.waterLevel >= 90) {
      gDevices.pump = false;
      Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
      applyDevicesToHw();
    }
  }
}

// ===========================================================================
// TIMER LOGIC
// ===========================================================================
void handleTimers() {
  time_t nowSeconds = time(nullptr);
  if (nowSeconds < 100000) return; 

  uint64_t currentMillis = (uint64_t)nowSeconds * 1000ULL; 

  if (gTimers.lightOffTime > 0 && currentMillis >= gTimers.lightOffTime) {
    gDevices.light = false;
    gTimers.lightOffTime = 0;
    Firebase.setBool(fbdoAutoApply, "/devices/light", false);
    Firebase.setInt(fbdoAutoApply, "/timers/light_off_time", 0);
    applyDevicesToHw();
  }

  if (gTimers.fanOffTime > 0 && currentMillis >= gTimers.fanOffTime) {
    gDevices.fan = false;
    gDevices.fanSpeed = 0;
    gTimers.fanOffTime = 0;
    Firebase.setBool(fbdoAutoApply, "/devices/fan", false);
    Firebase.setInt(fbdoAutoApply, "/devices/fan_speed", 0);
    Firebase.setInt(fbdoAutoApply, "/timers/fan_off_time", 0);
    applyDevicesToHw();
  }

  if (gTimers.humidifierOffTime > 0 && currentMillis >= gTimers.humidifierOffTime) {
    gDevices.humidifier = false;
    gTimers.humidifierOffTime = 0;
    Firebase.setBool(fbdoAutoApply, "/devices/humidifier", false);
    Firebase.setInt(fbdoAutoApply, "/timers/humidifier_off_time", 0);
    applyDevicesToHw();
  }

  if (gTimers.pumpOffTime > 0 && currentMillis >= gTimers.pumpOffTime) {
    gDevices.pump = false;
    gTimers.pumpOffTime = 0;
    Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
    Firebase.setInt(fbdoAutoApply, "/timers/pump_off_time", 0);
    applyDevicesToHw();
  }
}

// ===========================================================================
// STREAMS & CALLBACKS
// ===========================================================================
bool parseBoolData(FirebaseJsonData& result) {
  if (result.typeNum == FirebaseJson::JSON_BOOL)   return result.boolValue;
  if (result.typeNum == FirebaseJson::JSON_INT)    return result.intValue != 0;
  String str = result.stringValue;
  str.toLowerCase();
  str.trim();
  return (str == "true" || str == "1");
}

void startStreams() {
  if (!Firebase.ready()) return;

  if (gStreamsStarted) {
    Firebase.endStream(fbdoDevices);
    Firebase.endStream(fbdoAutomation);
    Firebase.endStream(fbdoTimers);
    gStreamsStarted = false;
  }

  if (Firebase.beginStream(fbdoDevices, "/devices")) {
    Firebase.setStreamCallback(fbdoDevices, onDevicesStream, [](bool t){ onStreamTimeout(t, fbdoDevices); });
  }

  if (Firebase.beginStream(fbdoAutomation, "/automation")) {
    Firebase.setStreamCallback(fbdoAutomation, onAutomationStream, [](bool t){ onStreamTimeout(t, fbdoAutomation); });
  }

  if (Firebase.beginStream(fbdoTimers, "/timers")) {
    Firebase.setStreamCallback(fbdoTimers, onTimersStream, [](bool t){ onStreamTimeout(t, fbdoTimers); });
  }

  gStreamsStarted = true;
  Serial.println("[firebase] Streams initialized.");
}

void onDevicesStream(StreamData data) {
  String path = data.dataPath();
  String type = data.dataType();

  if (type == "json") {
    FirebaseJson json = data.to<FirebaseJson>();
    FirebaseJsonData result;

    if (json.get(result, "light"))      gDevices.light      = parseBoolData(result);
    if (json.get(result, "fan"))        gDevices.fan        = parseBoolData(result);
    if (json.get(result, "pump"))       gDevices.pump       = parseBoolData(result);
    if (json.get(result, "humidifier")) gDevices.humidifier = parseBoolData(result);

    if (json.get(result, "fan_speed")) {
      gDevices.fanSpeed = (uint8_t)constrain(result.to<int>(), 0, 255);
    }
  }
  else {
    String payload = data.payload();
    payload.toLowerCase();
    payload.trim();
    bool boolVal = (payload == "true" || payload == "1");

    if (path == "/light")           gDevices.light      = boolVal;
    else if (path == "/fan")        gDevices.fan        = boolVal;
    else if (path == "/pump")       gDevices.pump       = boolVal;
    else if (path == "/humidifier") gDevices.humidifier = boolVal;
    else if (path == "/fan_speed")  gDevices.fanSpeed   = (uint8_t)constrain(payload.toInt(), 0, 255);
  }

  applyDevicesToHw();
}

void onAutomationStream(StreamData data) {
  String path = data.dataPath();
  String type = data.dataType();

  if (type == "json") {
    FirebaseJson json = data.to<FirebaseJson>();
    FirebaseJsonData result;

    if (json.get(result, "auto_fan"))        gAutomation.autoFan        = parseBoolData(result);
    if (json.get(result, "auto_humidifier")) gAutomation.autoHumidifier = parseBoolData(result);
    if (json.get(result, "auto_pump"))       gAutomation.autoPump       = parseBoolData(result);
    if (json.get(result, "auto_light"))      gAutomation.autoLight      = parseBoolData(result);
  } 
  else {
    String payload = data.payload();
    payload.toLowerCase();
    payload.trim();
    bool boolVal = (payload == "true" || payload == "1");

    if (path == "/auto_fan")             gAutomation.autoFan        = boolVal;
    else if (path == "/auto_humidifier") gAutomation.autoHumidifier = boolVal;
    else if (path == "/auto_pump")       gAutomation.autoPump       = boolVal;
    else if (path == "/auto_light")      gAutomation.autoLight      = boolVal;
  }
}

void onTimersStream(StreamData data) {
  String path = data.dataPath();
  String type = data.dataType();

  if (type == "json") {
    FirebaseJson json = data.to<FirebaseJson>();
    FirebaseJsonData result;

    if (json.get(result, "fan_off_time"))        gTimers.fanOffTime        = result.to<uint64_t>();
    if (json.get(result, "humidifier_off_time")) gTimers.humidifierOffTime = result.to<uint64_t>();
    if (json.get(result, "light_off_time"))      gTimers.lightOffTime      = result.to<uint64_t>();
    if (json.get(result, "pump_off_time"))       gTimers.pumpOffTime       = result.to<uint64_t>();
  } 
  else {
    uint64_t val = strtoull(data.payload().c_str(), NULL, 10);
    if (path == "/fan_off_time")             gTimers.fanOffTime        = val;
    else if (path == "/humidifier_off_time") gTimers.humidifierOffTime = val;
    else if (path == "/light_off_time")      gTimers.lightOffTime      = val;
    else if (path == "/pump_off_time")       gTimers.pumpOffTime       = val;
  }
}

void onStreamTimeout(bool timeout, FirebaseData& fb) {
  if (timeout) {
    Serial.printf("[stream] Timeout on %s, resuming...\n", fb.streamPath().c_str());
  }
}

// ===========================================================================
// SETUP & LOOP
// ===========================================================================
void setup() {
  Serial.begin(115200);

  // 1. OLED Display Init
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("[OLED] Allocation failed"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 20);
    display.println("Connecting WiFi...");
    display.display();
  }

  initPins();

  // 2. Wi-Fi Connection
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  // 3. NTP Time Sync
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    now = time(nullptr);
  }

  // 4. Firebase Config
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (strlen(DATABASE_SECRET) > 0) {
    config.signer.tokens.legacy_token = DATABASE_SECRET;
  } else {
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;
  }

  fbdoSensors.setBSSLBufferSize(2048, 512);
  fbdoAutoApply.setBSSLBufferSize(2048, 512);
  fbdoDevices.setBSSLBufferSize(2048, 512);
  fbdoAutomation.setBSSLBufferSize(2048, 512);
  fbdoTimers.setBSSLBufferSize(2048, 512);

  #if defined(USE_INSECURE_TLS) && USE_INSECURE_TLS
    config.cert.data = nullptr;
  #else
    config.cert.data = FIREBASE_GTS_ROOT_R1;
  #endif

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (Firebase.ready()) {
    if (!gStreamsStarted) {
      startStreams();
    }

    if (millis() - gLastSensorPublish > SENSOR_PERIOD_MS || gLastSensorPublish == 0) {
      gLastSensorPublish = millis();

      gSensors = readSensors();
      handleAutomation();
      handleTimers();
      publishSensors(gSensors);
      applyDevicesToHw();
    }

    if (millis() - gLastStreamRestart > STREAM_RECONNECT_MS) {
      gLastStreamRestart = millis();
      if (!fbdoDevices.httpConnected()) {
        startStreams();
      }
    }
  }
}