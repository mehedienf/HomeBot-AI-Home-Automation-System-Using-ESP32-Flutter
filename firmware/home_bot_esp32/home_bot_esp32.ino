// ============================================================================
// HomeBot - ESP32 Firmware
// ----------------------------------------------------------------------------
//  Companion app:  ../lib/*  (Flutter)
//  Target board :  ESP32 DevKit v1 (or any ESP32 with Wi-Fi)
//  Sensors      :  DHT22 (temp/humidity), float / ultrasonic (water level),
//                  MQ-2 or similar (smoke), all 3.3V tolerant.
//  Actuators    :  4x relays (light, fan, pump, humidifier).
//                  Fan also has 4 discrete speeds driven by an EN pin and
//                  4x MOSFET / relay ladder (PWM via LEDC).
//
//  The RTDB schema is the SINGLE source of truth shared with the Flutter
//  app - keep this file in sync with lib/services/firebase_service.dart.
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>

// ---------------------------------------------------------------------------
// 1. USER CONFIGURATION
//    Fill these four values, then upload via Arduino IDE / PlatformIO.
// ---------------------------------------------------------------------------
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// Get these from Firebase Console -> Project Settings -> Service Accounts ->
// Database secrets (legacy) OR generate a "Database" OAuth token in the
// Realtime Database tab.
#define API_KEY         "YOUR_FIREBASE_API_KEY"
#define DATABASE_URL    "https://YOUR-PROJECT-ID-default-rtdb.firebaseio.com"
#define USER_EMAIL      "device@homebot.local"
#define USER_PASSWORD   "YOUR_DATABASE_SECRET_OR_USER_PASSWORD"

// Hardware pins - change to match your wiring.
#define PIN_RELAY_LIGHT     26
#define PIN_RELAY_PUMP      27
#define PIN_RELAY_HUMID     14
#define PIN_FAN_ENABLE      25      // MOSFET gate (PWM)
#define PIN_DHT             4
#define PIN_SMOKE           34      // analog
#define PIN_WATER_LEVEL     35      // analog (or use a digital float switch)

// Timing (milliseconds).
#define SENSOR_PERIOD_MS    5000    // publish sensors every 5s
#define TIMER_TICK_MS       1000    // check timers every second

// Fan PWM: 4 speeds. 0 = off, 4 = max. Map duty cycle linearly.
static const uint8_t FAN_DUTY[5] = { 0, 64, 128, 192, 255 };
static const uint8_t FAN_PWM_CHANNEL = 0;

// ===========================================================================
// 2. STATE (mirrors lib/models/device_state.dart)
// ===========================================================================
struct DeviceState    { bool light, fan, pump, humidifier; uint8_t fanSpeed; };
struct AutomationState{ bool autoFan, autoHumidifier, autoPump; };
struct SensorState    { float temperature, humidity; uint8_t waterLevel; bool smokeDetected; };

DeviceState     gDevices    = { false, false, false, false, 0 };
AutomationState gAutomation = { true, true, true };
SensorState     gSensors    = { 0, 0, 0, false };

// Timers are absolute epoch-millis at which a load should auto-off.
// 0 means "no timer scheduled".
uint32_t gTimerLightOffAt = 0;
uint32_t gTimerFanOffAt   = 0;
uint32_t gTimerPumpOffAt  = 0;
uint32_t gTimerHumidOffAt = 0;

// ===========================================================================
// 3. FIREBASE / WIFI
// ===========================================================================
FirebaseData fbdo;
FirebaseData fbdoDevices;
FirebaseData fbdoAutomation;
FirebaseData fbdoTimers;
FirebaseAuth   auth;
FirebaseConfig config;

unsigned long gLastSensorPublish = 0;
unsigned long gLastTimerTick     = 0;

void initWifi() {
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.println();
  Serial.printf("WiFi up, IP=%s\n", WiFi.localIP().toString().c_str());
}

void initFirebase() {
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email     = USER_EMAIL;
  auth.user.password  = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // One FirebaseData per stream so we know exactly which path produced the
  // callback - the library uses the `fbdo.stream().path()` to identify it.
  Firebase.beginStream(&fbdoDevices,    "/devices");
  Firebase.beginStream(&fbdoAutomation, "/automation");
  Firebase.beginStream(&fbdoTimers,     "/timers");

  Serial.println("Firebase ready.");
}

// ===========================================================================
// 4. HARDWARE INIT
// ===========================================================================
DHT dht(PIN_DHT, DHT22);

void initPins() {
  pinMode(PIN_RELAY_LIGHT, OUTPUT);
  pinMode(PIN_RELAY_PUMP,  OUTPUT);
  pinMode(PIN_RELAY_HUMID, OUTPUT);
  pinMode(PIN_SMOKE,       INPUT);
  pinMode(PIN_WATER_LEVEL, INPUT);

  // Fan PWM
  ledcAttachPin(PIN_FAN_ENABLE, FAN_PWM_CHANNEL);
  ledcSetup(FAN_PWM_CHANNEL, 25000 /*Hz*/, 8 /*bits*/);

  dht.begin();
}

// ===========================================================================
// 5. ACTUATOR APPLY -  drives the relays + PWM from the cached gDevices.
//     Called every time gDevices changes, and on startup.
// ===========================================================================
static void applyFanPwm() {
  if (!gDevices.fan || gDevices.fanSpeed == 0) {
    ledcWrite(FAN_PWM_CHANNEL, 0);
  } else {
    ledcWrite(FAN_PWM_CHANNEL, FAN_DUTY[gDevices.fanSpeed]);
  }
}

void applyDevicesToHw() {
  digitalWrite(PIN_RELAY_LIGHT, gDevices.light ? HIGH : LOW);
  digitalWrite(PIN_RELAY_PUMP,  gDevices.pump  ? HIGH : LOW);
  digitalWrite(PIN_RELAY_HUMID, gDevices.humidifier ? HIGH : LOW);
  applyFanPwm();
}

// ===========================================================================
// 6. SENSOR READ
// ===========================================================================
SensorState readSensors() {
  SensorState s = gSensors;        // carry forward last known

  // DHT22 returns NaN on failure - skip if so.
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) s.temperature = t;
  if (!isnan(h)) s.humidity    = h;

  // Water level - simple analog mapping. Replace with your sensor's
  // calibration constants. Clamp to 0..100.
  int raw = analogRead(PIN_WATER_LEVEL);
  s.waterLevel = (uint8_t)constrain(map(raw, 0, 4095, 0, 100), 0, 100);

  // Smoke - threshold + simple debounce. Replace with your calibration.
  int smokeRaw = analogRead(PIN_SMOKE);
  bool smoke = smokeRaw > 1500; // adjust per sensor
  if (smoke) {
    s.smokeDetected = true;
  } else if (!gDevices.pump /*only clear after manual reset*/) {
    s.smokeDetected = false;
  }

  return s;
}

// ===========================================================================
// 7. TIMERS - 1 Hz tick. Check each /timers/*_off_time epoch and turn off
//    the corresponding load if it has passed.
// ===========================================================================
void handleTimers() {
  uint32_t now = (uint32_t)(millis() / 1000UL) * 1000UL; // optional: use NTP

  if (gTimerLightOffAt && now >= gTimerLightOffAt) {
    gDevices.light = false;
    gTimerLightOffAt = 0;
    Firebase.RTDB.setBool(&fbdo, "/devices/light", false);
  }
  if (gTimerFanOffAt && now >= gTimerFanOffAt) {
    gDevices.fan = false;
    gDevices.fanSpeed = 0;
    gTimerFanOffAt = 0;
    Firebase.RTDB.setBool(&fbdo, "/devices/fan", false);
    Firebase.RTDB.setInt (&fbdo, "/devices/fan_speed", 0);
  }
  if (gTimerPumpOffAt && now >= gTimerPumpOffAt) {
    gDevices.pump = false;
    gTimerPumpOffAt = 0;
    Firebase.RTDB.setBool(&fbdo, "/devices/pump", false);
  }
  if (gTimerHumidOffAt && now >= gTimerHumidOffAt) {
    gDevices.humidifier = false;
    gTimerHumidOffAt = 0;
    Firebase.RTDB.setBool(&fbdo, "/devices/humidifier", false);
  }
}

// ===========================================================================
// 8. SIMPLE AUTOMATION
//    Mutates gDevices locally when conditions cross thresholds. The change
//    is mirrored back to RTDB so the UI stays in sync.
// ===========================================================================
void handleAutomation() {
  if (gAutomation.autoFan && !gDevices.fan && gSensors.temperature > 30.0) {
    gDevices.fan = true;
    gDevices.fanSpeed = 2;
    Firebase.RTDB.setBool(&fbdo, "/devices/fan", true);
    Firebase.RTDB.setInt (&fbdo, "/devices/fan_speed", 2);
  }
  if (gAutomation.autoHumidifier && !gDevices.humidifier && gSensors.humidity < 35.0) {
    gDevices.humidifier = true;
    Firebase.RTDB.setBool(&fbdo, "/devices/humidifier", true);
  }
  if (gAutomation.autoPump && !gDevices.pump && gSensors.waterLevel < 20) {
    gDevices.pump = true;
    Firebase.RTDB.setBool(&fbdo, "/devices/pump", true);
  }

  // SAFETY: if smoke detected, force the pump OFF.
  if (gSensors.smokeDetected && gDevices.pump) {
    gDevices.pump = false;
    gTimerPumpOffAt = 0;
    Firebase.RTDB.setBool(&fbdo, "/devices/pump", false);
  }
}

// ===========================================================================
// 9. STREAM CALLBACKS -  invoked (on the main loop) when RTDB data arrives.
//     Each handler drains its own FirebaseData instance.
// ===========================================================================
static bool readJsonInto(FirebaseData& fb, const char* path) {
  if (Firebase.RTDB.get(&fb, path)) {
    if (fb.dataType() == "json") {
      return true;
    }
  }
  return false;
}

void handleDevicesStream() {
  if (!Firebase.ready()) return;
  if (!fbdoDevices.stream().available()) return;

  // Re-fetch the whole tree so we never miss a sibling update that the
  // stream event happened to consolidate.
  if (readJsonInto(fbdoDevices, "/devices")) {
    FirebaseJson json = fbdoDevices.to<FirebaseJson>();
    FirebaseJsonData result;
    bool tmp;

    if (json.get(result, "light",     tmp)) gDevices.light      = tmp;
    if (json.get(result, "fan",       tmp)) gDevices.fan        = tmp;
    if (json.get(result, "pump",      tmp)) gDevices.pump       = tmp;
    if (json.get(result, "humidifier",tmp)) gDevices.humidifier = tmp;

    int speed;
    if (json.get(result, "fan_speed", speed)) {
      if (speed < 0) speed = 0;
      if (speed > 4) speed = 4;
      gDevices.fanSpeed = (uint8_t)speed;
    }
    applyDevicesToHw();
  }
  fbdoDevices.stream().clear();
}

void handleAutomationStream() {
  if (!Firebase.ready()) return;
  if (!fbdoAutomation.stream().available()) return;

  if (readJsonInto(fbdoAutomation, "/automation")) {
    FirebaseJson json = fbdoAutomation.to<FirebaseJson>();
    FirebaseJsonData result;
    bool tmp;
    if (json.get(result, "auto_fan",        tmp)) gAutomation.autoFan        = tmp;
    if (json.get(result, "auto_humidifier", tmp)) gAutomation.autoHumidifier = tmp;
    if (json.get(result, "auto_pump",       tmp)) gAutomation.autoPump       = tmp;
  }
  fbdoAutomation.stream().clear();
}

void handleTimersStream() {
  if (!Firebase.ready()) return;
  if (!fbdoTimers.stream().available()) return;

  if (readJsonInto(fbdoTimers, "/timers")) {
    FirebaseJson json = fbdoTimers.to<FirebaseJson>();
    FirebaseJsonData result;
    int v;

    if (json.get(result, "light_off_time",      v)) gTimerLightOffAt = (uint32_t)v;
    if (json.get(result, "fan_off_time",        v)) gTimerFanOffAt   = (uint32_t)v;
    if (json.get(result, "pump_off_time",       v)) gTimerPumpOffAt  = (uint32_t)v;
    if (json.get(result, "humidifier_off_time", v)) gTimerHumidOffAt = (uint32_t)v;
  }
  fbdoTimers.stream().clear();
}

// ===========================================================================
// 10. SENSORS PUBLISH
// ===========================================================================
void publishSensors(const SensorState& s) {
  // Write each field so the UI sees an immediate update even if only one
  // sensor changed since the last tick.
  Firebase.RTDB.setFloat(&fbdo, "/sensors/temperature", s.temperature);
  Firebase.RTDB.setFloat(&fbdo, "/sensors/humidity",    s.humidity);
  Firebase.RTDB.setInt  (&fbdo, "/sensors/water_level", s.waterLevel);
  Firebase.RTDB.setBool (&fbdo, "/sensors/smoke_detected", s.smokeDetected);
}

// ===========================================================================
// 11. SETUP / LOOP
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("HomeBot ESP32 booting...");

  initPins();
  initWifi();
  initFirebase();
  applyDevicesToHw();
}

void loop() {
  // Drain any pending stream callbacks first.
  if (Firebase.ready()) {
    handleDevicesStream();
    handleAutomationStream();
    handleTimersStream();
  }

  unsigned long now = millis();

  if (now - gLastSensorPublish >= SENSOR_PERIOD_MS) {
    gLastSensorPublish = now;
    SensorState s = readSensors();

    // Detect state edges (e.g. smoke detected just crossed).
    bool smokeEdge = (s.smokeDetected != gSensors.smokeDetected);
    gSensors = s;
    publishSensors(s);

    if (smokeEdge && s.smokeDetected) {
      // Hard-stop pump on smoke - reflected in handleAutomation() too.
      gDevices.pump = false;
      Firebase.RTDB.setBool(&fbdo, "/devices/pump", false);
    }

    handleAutomation();
    applyDevicesToHw();
  }

  if (now - gLastTimerTick >= TIMER_TICK_MS) {
    gLastTimerTick = now;
    handleTimers();
    applyDevicesToHw();
  }
}
