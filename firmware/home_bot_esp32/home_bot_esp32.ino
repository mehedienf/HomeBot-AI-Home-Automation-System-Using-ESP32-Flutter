// ============================================================================
// HomeBot - ESP32 Firmware (Refined & Fixed with Ultrasonic)
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "time.h"

#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <DHT.h>

#include "secrets.h"
#include "firebase_certs.h"

// Hardware pins
#define PIN_RELAY_LIGHT     26
#define PIN_RELAY_PUMP      27
#define PIN_RELAY_HUMID     14
#define PIN_FAN_ENABLE      25      // MOSFET gate (PWM)
#define PIN_DHT             4
#define PIN_SMOKE           34      // analog
#define PIN_ECHO            35      // Ultrasonic Echo (Input)
#define PIN_TRIG            32      // Ultrasonic Trig (Output)

// Timing (milliseconds)
#define SENSOR_PERIOD_MS    5000    // publish sensors every 5s
#define STREAM_RECONNECT_MS 30000   // restart streams every 30s safety net

// Water Tank Calibration (Centimeters)
#define TANK_DEPTH_CM       30     // সেন্সর থেকে পানির সর্বোচ্চ দূরত্ব যখন ট্যাংক খালি (0%)
#define TANK_FULL_GAP_CM    1      // সেন্সর থেকে পানির সর্বনিম্ন দূরত্ব যখন ট্যাংক ভর্তি (100%)

// Fan PWM
const uint8_t FAN_DUTY[5] = { 0, 64, 128, 192, 255 };

// ===========================================================================
// STATE STRUCTS
// ===========================================================================
struct DeviceState     { bool light, fan, pump, humidifier; uint8_t fanSpeed; };
struct AutomationState { bool autoFan, autoHumidifier, autoPump; };
struct SensorState     { float temperature, humidity; uint8_t waterLevel; bool smokeDetected; int smokeRaw; };
struct TimerState { 
  uint64_t fanOffTime; 
  uint64_t humidifierOffTime; 
  uint64_t lightOffTime; 
  uint64_t pumpOffTime; 
};

TimerState gTimers = {0, 0, 0, 0};
FirebaseData fbdoTimers; // টাইমারের জন্য নতুন ফায়ারবেস স্ট্রিম

DeviceState     gDevices    = { false, false, false, false, 0 };
AutomationState gAutomation = { true, true, true };
SensorState     gSensors    = { 0.0, 0.0, 0, false, 0 };


// ===========================================================================
// FIREBASE & INSTANCES
// ===========================================================================
FirebaseData    fbdoSensors;       
FirebaseData    fbdoAutoApply;     
FirebaseData    fbdoDevices;       
FirebaseData    fbdoAutomation;    
FirebaseAuth    auth;
FirebaseConfig  config;

unsigned long gLastSensorPublish = 0;
unsigned long gLastStreamRestart = 0;
bool          gStreamsStarted    = false;

DHT dht(PIN_DHT, DHT22);

// Forward Declarations
void applyDevicesToHw();
void onDevicesStream(StreamData data);
void onAutomationStream(StreamData data);
void onTimersStream(StreamData data);
void onStreamTimeout(bool timeout, FirebaseData& fb);

// ===========================================================================
// HARDWARE INIT & CONTROL
// ===========================================================================
void initPins() {
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  pinMode(PIN_RELAY_PUMP,  OUTPUT);
  pinMode(PIN_RELAY_HUMID, OUTPUT);
  pinMode(PIN_SMOKE,       INPUT);
  
  // Ultrasonic Pins
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  ledcAttach(PIN_FAN_ENABLE, 25000, 8);

  digitalWrite(PIN_RELAY_LIGHT, LOW);
  digitalWrite(PIN_RELAY_PUMP,  LOW);
  digitalWrite(PIN_RELAY_HUMID, LOW);
  ledcWrite(PIN_FAN_ENABLE, 0);

  dht.begin();
}

void applyFanPwm() {
  if (!gDevices.fan || gDevices.fanSpeed == 0) {
    ledcWrite(PIN_FAN_ENABLE, 0);
  } else {
    if (gDevices.fanSpeed > 4) gDevices.fanSpeed = 4;
    ledcWrite(PIN_FAN_ENABLE, FAN_DUTY[gDevices.fanSpeed]);
  }
}

void applyDevicesToHw() {
  digitalWrite(PIN_RELAY_LIGHT, gDevices.light ? HIGH : LOW);
  digitalWrite(PIN_RELAY_PUMP,  gDevices.pump  ? HIGH : LOW);
  digitalWrite(PIN_RELAY_HUMID, gDevices.humidifier ? HIGH : LOW);
  applyFanPwm();
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

  // Smoke Sensor
  s.smokeRaw      = analogRead(PIN_SMOKE);
  s.smokeDetected = s.smokeRaw > 1500;

  // Ultrasonic Water Level Reading
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, 30000); // 30ms timeout 
  if (duration > 0) {
    float distance_cm = duration * 0.034 / 2.0;
    
    // Convert distance to percentage (0% - 100%)
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

  if (ok) {
    Serial.printf("[publish] Sensors updated -> T:%.1f°C, H:%.1f%%, Water:%d%%\n", 
                  s.temperature, s.humidity, s.waterLevel);
  } else {
    Serial.printf("[publish] /sensors FAIL: %s\n", fbdoSensors.errorReason().c_str());
  }
}

// ===========================================================================
// AUTOMATION LOGIC
// ===========================================================================
// ===========================================================================
// AUTOMATION LOGIC
// ===========================================================================
void handleAutomation() {
  // Fan automation
  if (gAutomation.autoFan && !gDevices.fan && gSensors.temperature > 30.0) {
    gDevices.fan = true;
    gDevices.fanSpeed = 2;
    Firebase.setBool(fbdoAutoApply, "/devices/fan", true);
    Firebase.setInt (fbdoAutoApply, "/devices/fan_speed", 2);
    applyDevicesToHw();
  }
  
  // Humidifier automation
  if (gAutomation.autoHumidifier && !gDevices.humidifier && gSensors.humidity < 35.0) {
    gDevices.humidifier = true;
    Firebase.setBool(fbdoAutoApply, "/devices/humidifier", true);
    applyDevicesToHw();
  }
  
  // Water pump automation (Hysteresis control)
  if (gAutomation.autoPump) {
    // যদি পানির লেভেল ২০% এর নিচে নামে এবং পাম্প বন্ধ থাকে, তাহলে পাম্প চালু করো
    if (!gDevices.pump && gSensors.waterLevel < 20) {
      gDevices.pump = true;
      Firebase.setBool(fbdoAutoApply, "/devices/pump", true);
      applyDevicesToHw();
    }
    // যদি পানির লেভেল ৯০% বা তার বেশি হয় এবং পাম্প চালু থাকে, তাহলে পাম্প বন্ধ করো
    else if (gDevices.pump && gSensors.waterLevel >= 90) {
      gDevices.pump = false;
      Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
      applyDevicesToHw();
    }
  }

  // Safety: Smoke detected shuts off pump (যেকোনো অবস্থায় ধোঁয়া দেখলে পাম্প বন্ধ)
  if (gSensors.smokeDetected && gDevices.pump) {
    gDevices.pump = false;
    Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
    applyDevicesToHw();
  }
}

// TIMER LOGIC
void handleTimers() {
  time_t nowSeconds = time(nullptr);
  if (nowSeconds < 100000) return; // NTP সিংক না হওয়া পর্যন্ত অপেক্ষা 

  // NTP থেকে পাওয়া সেকেন্ডকে মিলি-সেকেন্ডে কনভার্ট করা (কারণ ফায়ারবেসের ডাটা মিলি-সেকেন্ডে)
  uint64_t currentMillis = (uint64_t)nowSeconds * 1000ULL; 

  // Light Timer Check
  if (gTimers.lightOffTime > 0 && currentMillis >= gTimers.lightOffTime) {
    gDevices.light = false;
    gTimers.lightOffTime = 0; // টাইমার রিসেট
    Firebase.setBool(fbdoAutoApply, "/devices/light", false); // অ্যাপে লাইট অফ দেখানোর জন্য
    Firebase.setInt(fbdoAutoApply, "/timers/light_off_time", 0); // ফায়ারবেসে টাইমার জিরো করা
    applyDevicesToHw(); // হার্ডওয়্যার রিলে অফ করা
    Serial.println("[timer] Light turned OFF via timer.");
  }

  // Fan Timer Check
  if (gTimers.fanOffTime > 0 && currentMillis >= gTimers.fanOffTime) {
    gDevices.fan = false;
    gTimers.fanOffTime = 0;
    Firebase.setBool(fbdoAutoApply, "/devices/fan", false);
    Firebase.setInt(fbdoAutoApply, "/timers/fan_off_time", 0);
    applyDevicesToHw();
    Serial.println("[timer] Fan turned OFF via timer.");
  }

  // Humidifier Timer Check
  if (gTimers.humidifierOffTime > 0 && currentMillis >= gTimers.humidifierOffTime) {
    gDevices.humidifier = false;
    gTimers.humidifierOffTime = 0;
    Firebase.setBool(fbdoAutoApply, "/devices/humidifier", false);
    Firebase.setInt(fbdoAutoApply, "/timers/humidifier_off_time", 0);
    applyDevicesToHw();
    Serial.println("[timer] Humidifier turned OFF via timer.");
  }

  // Pump Timer Check
  if (gTimers.pumpOffTime > 0 && currentMillis >= gTimers.pumpOffTime) {
    gDevices.pump = false;
    gTimers.pumpOffTime = 0;
    Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
    Firebase.setInt(fbdoAutoApply, "/timers/pump_off_time", 0);
    applyDevicesToHw();
    Serial.println("[timer] Pump turned OFF via timer.");
  }
}

// ===========================================================================
// STREAMS & CALLBACKS
// ===========================================================================
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
  String path = data.dataPath(); // যেমন: "/light", "/fan", ইত্যাদি
  String type = data.dataType(); // "json", "boolean", "int"

  // ১. পুরো JSON আসলে (প্রথম কানেকশনে)
  if (type == "json") {
    FirebaseJson json = data.to<FirebaseJson>();
    FirebaseJsonData result;
    bool tmp;

    if (json.get(result, "light",      tmp)) gDevices.light      = tmp;
    if (json.get(result, "fan",        tmp)) gDevices.fan        = tmp;
    if (json.get(result, "pump",       tmp)) gDevices.pump       = tmp;
    if (json.get(result, "humidifier", tmp)) gDevices.humidifier = tmp;

    int speed;
    if (json.get(result, "fan_speed", speed)) {
      gDevices.fanSpeed = (uint8_t)constrain(speed, 0, 4);
    }
  } 
  // ২. অ্যাপ থেকে একটি সিঙ্গেল সুইচ পরিবর্তন করলে (Boolean)
  else if (type == "boolean") {
    bool val = data.boolData();
    if (path == "/light")           gDevices.light      = val;
    else if (path == "/fan")        gDevices.fan        = val;
    else if (path == "/pump")       gDevices.pump       = val;
    else if (path == "/humidifier") gDevices.humidifier = val;
  } 
  // ৩. ফ্যান স্পিড ইত্যাদি পরিবর্তন করলে (Int)
  else if (type == "int") {
    int val = data.intData();
    if (path == "/fan_speed")       gDevices.fanSpeed   = (uint8_t)constrain(val, 0, 4);
    else if (path == "/light")      gDevices.light      = (val == 1);
    else if (path == "/fan")        gDevices.fan        = (val == 1);
    else if (path == "/pump")       gDevices.pump       = (val == 1);
    else if (path == "/humidifier") gDevices.humidifier = (val == 1);
  }

  // হার্ডওয়্যার রিলে/PWM আপডেট করা
  applyDevicesToHw();
  Serial.printf("[stream] /devices -> Light:%d Fan:%d (Spd:%d) Pump:%d Humid:%d\n",
                gDevices.light, gDevices.fan, gDevices.fanSpeed, gDevices.pump, gDevices.humidifier);
}

void onAutomationStream(StreamData data) {
  String path = data.dataPath();
  String type = data.dataType();

  if (type == "json") {
    FirebaseJson json = data.to<FirebaseJson>();
    FirebaseJsonData result;
    bool tmp;

    if (json.get(result, "auto_fan",        tmp)) gAutomation.autoFan        = tmp;
    if (json.get(result, "auto_humidifier", tmp)) gAutomation.autoHumidifier = tmp;
    if (json.get(result, "auto_pump",       tmp)) gAutomation.autoPump       = tmp;
  } 
  else if (type == "boolean") {
    bool val = data.boolData();
    if (path == "/auto_fan")             gAutomation.autoFan        = val;
    else if (path == "/auto_humidifier") gAutomation.autoHumidifier = val;
    else if (path == "/auto_pump")       gAutomation.autoPump       = val;
  }

  Serial.printf("[stream] /automation -> AutoFan:%d AutoHumid:%d AutoPump:%d\n",
                gAutomation.autoFan, gAutomation.autoHumidifier, gAutomation.autoPump);
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
  else if (type == "int" || type == "double") {
    uint64_t val = data.intData(); // 13-digit Unix timestamp
    if (path == "/fan_off_time")        gTimers.fanOffTime        = val;
    else if (path == "/humidifier_off_time") gTimers.humidifierOffTime = val;
    else if (path == "/light_off_time") gTimers.lightOffTime      = val;
    else if (path == "/pump_off_time")  gTimers.pumpOffTime       = val;
  }

  Serial.println("[stream] /timers updated.");
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
  initPins();

  // ১. ওয়াইফাই কানেকশন
  WiFi.setTxPower(WIFI_POWER_17dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

  // ২. NTP Time Sync (Email/Password Auth এর জন্য আবশ্যক)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime Synced Successfully!");

  // ৩. Firebase কনফিগারেশন
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (strlen(DATABASE_SECRET) > 0) {
    config.signer.tokens.legacy_token = DATABASE_SECRET;
  } else {
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;
  }

  // SSL ও Buffer সেটআপ
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

    // প্রতি ৫ সেকেন্ড পর পর সেন্সর রিড ও ডাটা পাবলিশ
    if (millis() - gLastSensorPublish > SENSOR_PERIOD_MS || gLastSensorPublish == 0) {
      gLastSensorPublish = millis();

      gSensors = readSensors();
      handleAutomation();
      handleTimers();
      publishSensors(gSensors);
    }

    // সেফটি নেট
    if (millis() - gLastStreamRestart > STREAM_RECONNECT_MS) {
      gLastStreamRestart = millis();
      if (!fbdoDevices.httpConnected()) {
        startStreams();
      }
    }
  }
}