// ============================================================================
// HomeBot - ESP32 Firmware (Refined & Fixed)
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
#define PIN_WATER_LEVEL     35      // analog

// Timing (milliseconds)
#define SENSOR_PERIOD_MS    5000    // publish sensors every 5s
#define STREAM_RECONNECT_MS 30000   // restart streams every 30s safety net

// Fan PWM
static const uint8_t FAN_DUTY[5] = { 0, 64, 128, 192, 255 };

// ===========================================================================
// STATE STRUCTS
// ===========================================================================
struct DeviceState     { bool light, fan, pump, humidifier; uint8_t fanSpeed; };
struct AutomationState { bool autoFan, autoHumidifier, autoPump; };
struct SensorState     { float temperature, humidity; uint8_t waterLevel; bool smokeDetected; int smokeRaw; };

DeviceState     gDevices    = { false, false, false, false, 0 };
AutomationState gAutomation = { true, true, true };
SensorState     gSensors    = { 0.0, 0.0, 0, false, 0 };

// ===========================================================================
// FIREBASE & INSTANCES
// ===========================================================================
FirebaseData    fbdoSensors;       // publishSensors()
FirebaseData    fbdoAutoApply;     // handleAutomation()
FirebaseData    fbdoDevices;       // Stream /devices
FirebaseData    fbdoAutomation;    // Stream /automation
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
void onStreamTimeout(bool timeout, FirebaseData& fb);

// ===========================================================================
// HARDWARE INIT & CONTROL
// ===========================================================================
void initPins() {
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  pinMode(PIN_RELAY_PUMP,  OUTPUT);
  pinMode(PIN_RELAY_HUMID, OUTPUT);
  pinMode(PIN_SMOKE,       INPUT);
  pinMode(PIN_WATER_LEVEL, INPUT);

  ledcAttach(PIN_FAN_ENABLE, 25000, 8);

  digitalWrite(PIN_RELAY_LIGHT, LOW);
  digitalWrite(PIN_RELAY_PUMP,  LOW);
  digitalWrite(PIN_RELAY_HUMID, LOW);
  ledcWrite(PIN_FAN_ENABLE, 0);

  dht.begin();
}

static void applyFanPwm() {
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

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) s.temperature = t;
  if (!isnan(h)) s.humidity    = h;

  int rawWater = analogRead(PIN_WATER_LEVEL);
  s.waterLevel = (uint8_t)constrain(map(rawWater, 0, 4095, 0, 100), 0, 100);

  s.smokeRaw      = analogRead(PIN_SMOKE);
  s.smokeDetected = s.smokeRaw > 1500;

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
void handleAutomation() {
  if (gAutomation.autoFan && !gDevices.fan && gSensors.temperature > 30.0) {
    gDevices.fan = true;
    gDevices.fanSpeed = 2;
    Firebase.setBool(fbdoAutoApply, "/devices/fan", true);
    Firebase.setInt (fbdoAutoApply, "/devices/fan_speed", 2);
    applyDevicesToHw();
  }
  if (gAutomation.autoHumidifier && !gDevices.humidifier && gSensors.humidity < 35.0) {
    gDevices.humidifier = true;
    Firebase.setBool(fbdoAutoApply, "/devices/humidifier", true);
    applyDevicesToHw();
  }
  if (gAutomation.autoPump && !gDevices.pump && gSensors.waterLevel < 20) {
    gDevices.pump = true;
    Firebase.setBool(fbdoAutoApply, "/devices/pump", true);
    applyDevicesToHw();
  }

  // Safety: Smoke detected shuts off pump
  if (gSensors.smokeDetected && gDevices.pump) {
    gDevices.pump = false;
    Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
    applyDevicesToHw();
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
    gStreamsStarted = false;
  }

  if (Firebase.beginStream(fbdoDevices, "/devices")) {
    Firebase.setStreamCallback(fbdoDevices, onDevicesStream, [](bool t){ onStreamTimeout(t, fbdoDevices); });
  }

  if (Firebase.beginStream(fbdoAutomation, "/automation")) {
    Firebase.setStreamCallback(fbdoAutomation, onAutomationStream, [](bool t){ onStreamTimeout(t, fbdoAutomation); });
  }

  gStreamsStarted = true;
  Serial.println("[firebase] Streams initialized.");
}

void onDevicesStream(StreamData data) {
  if (data.dataType() != "json") return;
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
  
  applyDevicesToHw();
  Serial.printf("[stream] /devices -> Light:%d Fan:%d (Spd:%d) Pump:%d Humid:%d\n",
                gDevices.light, gDevices.fan, gDevices.fanSpeed, gDevices.pump, gDevices.humidifier);
}

void onAutomationStream(StreamData data) {
  if (data.dataType() != "json") return;
  FirebaseJson json = data.to<FirebaseJson>();
  FirebaseJsonData result;
  bool tmp;

  if (json.get(result, "auto_fan",        tmp)) gAutomation.autoFan        = tmp;
  if (json.get(result, "auto_humidifier", tmp)) gAutomation.autoHumidifier = tmp;
  if (json.get(result, "auto_pump",       tmp)) gAutomation.autoPump       = tmp;

  Serial.printf("[stream] /automation -> AutoFan:%d AutoHumid:%d AutoPump:%d\n",
                gAutomation.autoFan, gAutomation.autoHumidifier, gAutomation.autoPump);
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

  // ১. ওয়াইফাই কানেকশন
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

  #if defined(USE_INSECURE_TLS) && USE_INSECURE_TLS
    config.cert.data = nullptr;
  #else
    config.cert.data = FIREBASE_GTS_ROOT_R1;
  #endif

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  // Firebase তৈরি হলে প্রথমবার Stream চালু করবে
  if (Firebase.ready()) {
    if (!gStreamsStarted) {
      startStreams();
    }

    // প্রতি ৫ সেকেন্ড পর পর সেন্সর রিড করবে, অটোমেশন চেক করবে ও ফায়ারবেসে পাঠাবে
    if (millis() - gLastSensorPublish > SENSOR_PERIOD_MS || gLastSensorPublish == 0) {
      gLastSensorPublish = millis();

      gSensors = readSensors();
      handleAutomation();
      publishSensors(gSensors);
    }

    // সেফটি নেট: প্রতি ৩০ সেকেন্ডে স্ট্রিম রিস্টার্ট বা হেলথ চেক
    if (millis() - gLastStreamRestart > STREAM_RECONNECT_MS) {
      gLastStreamRestart = millis();
      if (!fbdoDevices.httpConnected()) {
        startStreams();
      }
    }
  }
}