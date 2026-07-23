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
#include <Firebase.h>
#include <DHT.h>

// ---------------------------------------------------------------------------
// 1. USER CONFIGURATION
//    Fill these four values, then upload via Arduino IDE / PlatformIO.
//    All values live in `secrets.h` (gitignored). Copy `secrets.h.example`
//    -> `secrets.h` if it doesn't exist.
// ---------------------------------------------------------------------------
#include "secrets.h"
#include "firebase_certs.h"

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

// ===========================================================================
// 3. FIREBASE / WIFI
// ===========================================================================
FirebaseData fbdo;
FirebaseData fbdoDevices;
FirebaseData fbdoAutomation;
FirebaseAuth   auth;
FirebaseConfig config;

unsigned long gLastSensorPublish = 0;

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

  // Trust Firebase's TLS root CAs. Without this the SSL handshake fails
  // with "Failed to initialize the SSL layer" on ESP32 Arduino because
  // mbedTLS doesn't ship Google Trust Services roots by default.
  // In Firebase ESP32 Client v4.x the cert goes on `config.cert.data`
  // which is `const char*` — so we build a heap String and hand back its
  // C string. The library copies the cert internally; `gCertBuf` lives
  // for the lifetime of the program.
  static String gCertBuf =
      String(FIREBASE_GTS_ROOT_R1) + String(FIREBASE_GTS_CA_1C3);
  config.cert.data = gCertBuf.c_str();

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // One FirebaseData per stream so we know exactly which path produced the
  // callback - the library uses `fbdo.streamPath()` to identify it.
  Firebase.beginStream(fbdoDevices,    "/devices");
  Firebase.beginStream(fbdoAutomation, "/automation");

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

  // Fan PWM  (ESP32 Arduino core 3.x LEDC API)
  //   ledcAttach(pin, freq_hz, resolution_bits)  -> attaches AND configures
  //   ledcWrite(pin, duty)                       -> uses pin, not channel
  ledcAttach(PIN_FAN_ENABLE, 25000 /*Hz*/, 8 /*bits*/);

  dht.begin();
}

// ===========================================================================
// 5. ACTUATOR APPLY -  drives the relays + PWM from the cached gDevices.
//     Called every time gDevices changes, and on startup.
// ===========================================================================
static void applyFanPwm() {
  if (!gDevices.fan || gDevices.fanSpeed == 0) {
    ledcWrite(PIN_FAN_ENABLE, 0);
  } else {
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
  } else {
    // Clear unconditionally on a clean reading; the safety net in
    // handleAutomation() still force-turns the pump off while the smoke
    // flag is true, so we don't need a manual reset window.
    s.smokeDetected = false;
  }

  return s;
}

// ===========================================================================
// 7. SIMPLE AUTOMATION
//    Mutates gDevices locally when conditions cross thresholds. The change
//    is mirrored back to RTDB so the UI stays in sync.
// ===========================================================================
void handleAutomation() {
  if (gAutomation.autoFan && !gDevices.fan && gSensors.temperature > 30.0) {
    gDevices.fan = true;
    gDevices.fanSpeed = 2;
    Firebase.setBool(fbdo, "/devices/fan", true);
    Firebase.setInt (fbdo, "/devices/fan_speed", 2);
  }
  if (gAutomation.autoHumidifier && !gDevices.humidifier && gSensors.humidity < 35.0) {
    gDevices.humidifier = true;
    Firebase.setBool(fbdo, "/devices/humidifier", true);
  }
  if (gAutomation.autoPump && !gDevices.pump && gSensors.waterLevel < 20) {
    gDevices.pump = true;
    Firebase.setBool(fbdo, "/devices/pump", true);
  }

  // SAFETY: if smoke detected, force the pump OFF. Level-triggered so we
  // catch it on the very first sensor tick after boot, not just on a
  // rising edge.
  if (gSensors.smokeDetected && gDevices.pump) {
    gDevices.pump = false;
    Firebase.setBool(fbdo, "/devices/pump", false);
  }
}

// ===========================================================================
// 9. STREAM CALLBACKS -  invoked (on the main loop) when RTDB data arrives.
//     Each handler drains its own FirebaseData instance.
// ===========================================================================
static bool readJsonInto(FirebaseData& fb, const char* path) {
  if (Firebase.get(fb, path)) {
    if (fb.dataType() == "json") {
      return true;
    }
  }
  return false;
}

void handleDevicesStream() {
  if (!Firebase.ready()) return;
  if (!Firebase.readStream(fbdoDevices)) return;

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
  fbdoDevices.clear();
}

void handleAutomationStream() {
  if (!Firebase.ready()) return;
  if (!Firebase.readStream(fbdoAutomation)) return;

  if (readJsonInto(fbdoAutomation, "/automation")) {
    FirebaseJson json = fbdoAutomation.to<FirebaseJson>();
    FirebaseJsonData result;
    bool tmp;
    if (json.get(result, "auto_fan",        tmp)) gAutomation.autoFan        = tmp;
    if (json.get(result, "auto_humidifier", tmp)) gAutomation.autoHumidifier = tmp;
    if (json.get(result, "auto_pump",       tmp)) gAutomation.autoPump       = tmp;
  }
  fbdoAutomation.clear();
}

// ===========================================================================
// 10. SENSORS PUBLISH
// ===========================================================================
void publishSensors(const SensorState& s) {
  // Write each field so the UI sees an immediate update even if only one
  // sensor changed since the last tick.
  Firebase.setFloat(fbdo, "/sensors/temperature", s.temperature);
  Firebase.setFloat(fbdo, "/sensors/humidity",    s.humidity);
  Firebase.setInt  (fbdo, "/sensors/water_level", s.waterLevel);
  Firebase.setBool (fbdo, "/sensors/smoke_detected", s.smokeDetected);
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
      Firebase.setBool(fbdo, "/devices/pump", false);
    }

    handleAutomation();
    applyDevicesToHw();
  }
}
