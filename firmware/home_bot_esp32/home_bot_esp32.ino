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
#define WIFI_RECONNECT_MS   5000    // retry WiFi every 5s if down
#define STREAM_RECONNECT_MS 30000   // restart streams every 30s as a safety net

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
//
// IMPORTANT: each Firebase operation that can run concurrently needs its
// OWN FirebaseData instance. The ESP32 Arduino core is single-threaded
// but the Firebase ESP client internally stores the last data + payload
// pointer on each FirebaseData, so reusing one (e.g. `fbdo`) across both
// the publish loop and stream reads silently corrupts the next read.
// We allocate one for sensors + one for automation writes + one for
// pump safety writes.
//
FirebaseData    fbdoSensors;       // used by publishSensors()
FirebaseData    fbdoAutoApply;     // used by handleAutomation() writes
FirebaseData    fbdoDevices;
FirebaseData    fbdoAutomation;
FirebaseAuth    auth;
FirebaseConfig  config;

unsigned long gLastSensorPublish   = 0;
unsigned long gLastWifiRetry       = 0;
unsigned long gLastStreamRestart   = 0;
bool          gStreamsStarted      = false;

// WiFi event group so we know when the link actually drops (rather than
// reading WL_CONNECTED in a tight loop and getting stale "connected"
// answers while the radio is mid-roam).
static volatile bool gWifiConnected = false;
static volatile bool gWifiJustLost  = false;

void onWifiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  gWifiConnected = true;
  gWifiJustLost  = false;
  Serial.printf("\n[wifi] connected, IP=%s, RSSI=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (gWifiConnected) {
    gWifiJustLost = true;
  }
  gWifiConnected = false;
  Serial.printf("\n[wifi] disconnected (reason=%d)\n", info.wifi_sta_disconnected.reason);
}

void initWifi() {
  WiFi.onEvent(onWifiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // `WIFI_TX_POWER_17dBm` is the highest reliable setting on a DevKit
  // powered by a noisy / current-limited supply (USB chargers that look
  // fine on a phone but can't sustain an ESP32 burst). Cranking to 19.5
  // dBm is the #1 cause of "5V external supply but WiFi keeps dropping"
  // — backing off helps both brownout and RF front-end stability.
  WiFi.setTxPower(WIFI_TX_POWER_17dBm);
  WiFi.setSleep(false);  // modem-sleep disables the radio during stream callbacks
  WiFi.setAutoReconnect(true);

  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool waitForWifi(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!gWifiConnected) {
    if (millis() - start > timeoutMs) return false;
    Serial.print('.');
    delay(500);
  }
  Serial.println();
  return gWifiConnected;
}

void initFirebase() {
  config.api_key      = API_KEY;
  // Trailing slash on the URL is tolerated by the library but historically
  // caused 404s on some ESP32 Arduino core versions — keep it trimmed.
  config.database_url = DATABASE_URL;
  auth.user.email     = USER_EMAIL;
  auth.user.password  = USER_PASSWORD;

  // Trust Firebase's TLS root CAs.
  static String gCertBuf =
      String(FIREBASE_GTS_ROOT_R1) + String(FIREBASE_GTS_CA_1C3);
  config.cert.data = gCertBuf.c_str();

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase ready.");
}

void startStreams() {
  if (!Firebase.ready()) return;
  if (gStreamsStarted) {
    Firebase.endStream(fbdoDevices);
    Firebase.endStream(fbdoAutomation);
    gStreamsStarted = false;
  }
  // beginStream is idempotent only after endStream, so the reset above
  // is what lets us recover from "stream silently died after WiFi roam".
  Firebase.beginStream(fbdoDevices,    "/devices");
  Firebase.beginStream(fbdoAutomation, "/automation");
  gStreamsStarted = true;
  Serial.println("[firebase] streams started on /devices and /automation");
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

  ledcAttach(PIN_FAN_ENABLE, 25000 /*Hz*/, 8 /*bits*/);

  // Start with everything OFF so relays don't jitter at boot before
  // applyDevicesToHw() runs.
  digitalWrite(PIN_RELAY_LIGHT, LOW);
  digitalWrite(PIN_RELAY_PUMP,  LOW);
  digitalWrite(PIN_RELAY_HUMID, LOW);
  ledcWrite(PIN_FAN_ENABLE, 0);

  dht.begin();
}

// ===========================================================================
// 5. ACTUATOR APPLY
// ===========================================================================
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
// 6. SENSOR READ
// ===========================================================================
SensorState readSensors() {
  SensorState s = gSensors;        // carry forward last known

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) s.temperature = t;
  if (!isnan(h)) s.humidity    = h;

  int raw = analogRead(PIN_WATER_LEVEL);
  s.waterLevel = (uint8_t)constrain(map(raw, 0, 4095, 0, 100), 0, 100);

  int smokeRaw = analogRead(PIN_SMOKE);
  s.smokeDetected = smokeRaw > 1500;

  return s;
}

// ===========================================================================
// 7. SIMPLE AUTOMATION
//    Each side-effect write goes through its own FirebaseData so we never
//    collide with the stream reads / publishSensors().
// ===========================================================================
void handleAutomation() {
  if (gAutomation.autoFan && !gDevices.fan && gSensors.temperature > 30.0) {
    gDevices.fan = true;
    gDevices.fanSpeed = 2;
    Firebase.setBool(fbdoAutoApply, "/devices/fan", true);
    Firebase.setInt (fbdoAutoApply, "/devices/fan_speed", 2);
  }
  if (gAutomation.autoHumidifier && !gDevices.humidifier && gSensors.humidity < 35.0) {
    gDevices.humidifier = true;
    Firebase.setBool(fbdoAutoApply, "/devices/humidifier", true);
  }
  if (gAutomation.autoPump && !gDevices.pump && gSensors.waterLevel < 20) {
    gDevices.pump = true;
    Firebase.setBool(fbdoAutoApply, "/devices/pump", true);
  }

  if (gSensors.smokeDetected && gDevices.pump) {
    gDevices.pump = false;
    Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
  }
}

// ===========================================================================
// 9. STREAM CALLBACKS
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
  if (!gStreamsStarted) return;
  if (!Firebase.readStream(fbdoDevices)) return;

  if (readJsonInto(fbdoDevices, "/devices")) {
    FirebaseJson json = fbdoDevices.to<FirebaseJson>();
    FirebaseJsonData result;
    bool tmp;

    if (json.get(result, "light",      tmp)) gDevices.light      = tmp;
    if (json.get(result, "fan",        tmp)) gDevices.fan        = tmp;
    if (json.get(result, "pump",       tmp)) gDevices.pump       = tmp;
    if (json.get(result, "humidifier", tmp)) gDevices.humidifier = tmp;

    int speed;
    if (json.get(result, "fan_speed", speed)) {
      if (speed < 0) speed = 0;
      if (speed > 4) speed = 4;
      gDevices.fanSpeed = (uint8_t)speed;
    }
    applyDevicesToHw();
    Serial.printf("[stream] /devices -> light=%d fan=%d spd=%d pump=%d humid=%d\n",
                  gDevices.light, gDevices.fan, gDevices.fanSpeed,
                  gDevices.pump, gDevices.humidifier);
  }
  fbdoDevices.clear();
}

void handleAutomationStream() {
  if (!gStreamsStarted) return;
  if (!Firebase.readStream(fbdoAutomation)) return;

  if (readJsonInto(fbdoAutomation, "/automation")) {
    FirebaseJson json = fbdoAutomation.to<FirebaseJson>();
    FirebaseJsonData result;
    bool tmp;
    if (json.get(result, "auto_fan",        tmp)) gAutomation.autoFan        = tmp;
    if (json.get(result, "auto_humidifier", tmp)) gAutomation.autoHumidifier = tmp;
    if (json.get(result, "auto_pump",       tmp)) gAutomation.autoPump       = tmp;
    Serial.printf("[stream] /automation -> autoFan=%d autoHumid=%d autoPump=%d\n",
                  gAutomation.autoFan, gAutomation.autoHumidifier,
                  gAutomation.autoPump);
  }
  fbdoAutomation.clear();
}

// ===========================================================================
// 10. SENSORS PUBLISH
//    One FirebaseData (`fbdoSensors`) reserved exclusively for these writes.
// ===========================================================================
void publishSensors(const SensorState& s) {
  bool ok = true;
  ok &= Firebase.setFloat(fbdoSensors, "/sensors/temperature",      s.temperature);
  ok &= Firebase.setFloat(fbdoSensors, "/sensors/humidity",         s.humidity);
  ok &= Firebase.setInt  (fbdoSensors, "/sensors/water_level",      s.waterLevel);
  ok &= Firebase.setBool (fbdoSensors, "/sensors/smoke_detected",   s.smokeDetected);
  if (!ok) {
    Serial.printf("[publish] /sensors FAIL (reason='%s')\n",
                  fbdoSensors.errorReason().c_str());
  }
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

  // Wait up to 15 s for the first connection — if the user replaced the
  // board on a flaky 5V supply we still want a chance to enter loop() and
  // show diagnostics, not get stuck in initWifi().
  if (!waitForWifi(15000)) {
    Serial.println("[wifi] initial connect timed out, will retry in loop()");
  }

  // Try Firebase init even if WiFi isn't connected yet — the client
  // re-attaches the SSL socket as soon as WiFi comes back up.
  initFirebase();

  // Give the auth + initial TCP handshake up to 10 s before giving up
  // for this boot cycle. If it fails we'll just keep looping.
  for (int i = 0; i < 20 && !Firebase.ready(); i++) {
    delay(500);
  }
  if (Firebase.ready()) {
    startStreams();
  } else {
    Serial.println("[firebase] not ready at boot, will restart streams in loop()");
  }

  applyDevicesToHw();
  Serial.println("Setup done.");
}

void loop() {
  unsigned long now = millis();

  // ---- WiFi watchdog --------------------------------------------------
  if (!gWifiConnected) {
    if (now - gLastWifiRetry >= WIFI_RECONNECT_MS) {
      gLastWifiRetry = now;
      Serial.println("[wifi] retrying connection...");
      WiFi.reconnect();
    }
  } else if (gWifiJustLost) {
    gWifiJustLost = false;
    Serial.println("[wifi] link re-established, restarting Firebase streams");
    gStreamsStarted = false;
  }

  // ---- Firebase readiness + streams watchdog --------------------------
  if (Firebase.ready()) {
    if (!gStreamsStarted) {
      startStreams();
    } else if (now - gLastStreamRestart >= STREAM_RECONNECT_MS) {
      // Safety-net: every 30 s tear down + restart the streams. Without
      // this the ESP32 happily keeps a "connected" handle while the
      // server-side long-poll has silently dropped (no callback → app's
      // toggles never reach us).
      gLastStreamRestart = now;
      startStreams();
    }
  }

  // ---- Drain stream callbacks ----------------------------------------
  if (Firebase.ready() && gStreamsStarted) {
    handleDevicesStream();
    handleAutomationStream();
  }

  // ---- Sensor publish (every 5 s) -------------------------------------
  if (now - gLastSensorPublish >= SENSOR_PERIOD_MS) {
    gLastSensorPublish = now;
    if (!Firebase.ready()) {
      Serial.println("[publish] skip — Firebase not ready");
      return;
    }
    SensorState s = readSensors();

    bool smokeEdge = (s.smokeDetected != gSensors.smokeDetected);
    gSensors = s;
    publishSensors(s);

    if (smokeEdge && s.smokeDetected) {
      gDevices.pump = false;
      Firebase.setBool(fbdoAutoApply, "/devices/pump", false);
    }

    handleAutomation();
    applyDevicesToHw();
  }
}
