# HomeBot ESP32 Firmware

Pair this with the Flutter app in `../../lib/`. The RTDB schema they share
is documented in the project root `README.md`.

## Setup

1. Install Arduino IDE + ESP32 board support, or use PlatformIO.
2. Install the required libraries:
   * `Firebase ESP Client` by Mobizt (`FirebaseESP32`)
   * `DHT sensor library` by Adafruit
   * (built-in) `WiFi`, `Firebase_ESP_Client`
3. Open `home_bot_esp32.ino` and fill in:
   ```cpp
   #define WIFI_SSID       "YOUR_WIFI_SSID"
   #define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
   #define API_KEY         "your-firebase-api-key"
   #define DATABASE_URL    "https://your-project.firebaseio.com"
   #define USER_EMAIL      "device@homebot.local"
   #define USER_PASSWORD   "your-database-secret-or-user-password"
   ```
4. Upload to the ESP32 and open the serial monitor (115200 baud). You should
   see `WiFi up` -> `Firebase ready` -> sensor data appearing in RTDB.

## Wiring

```
ESP32 GPIO 26  -> Relay 1 IN (Light)        5V module supply
ESP32 GPIO 27  -> Relay 2 IN (Pump)         5V module supply
ESP32 GPIO 14  -> Relay 3 IN (Humidifier)   5V module supply
ESP32 GPIO 25  -> MOSFET gate (Fan PWM)     + pull-down 10k to GND
ESP32 GPIO 4   -> DHT22 DATA                10k pull-up to 3.3V
ESP32 GPIO 34  -> MQ-2 AO (smoke, analog)  smoke threshold ~1500
ESP32 GPIO 35  -> Water-level sensor AO     (or digital float switch to GND)
ESP32 GND      -> common ground with all 5V modules
```

## Generating the Firebase credentials

In the Firebase Console:

1. **Project Settings -> General -> Your apps -> Web app** to get the
   `apiKey`.
2. **Project Settings -> Service Accounts -> Database secrets** to get
   `USER_PASSWORD`. (Legacy Firebase auth.)
3. The `DATABASE_URL` is shown at the top of the **Realtime Database** tab.

> Security tip: in the Firebase Rules give the device's email only
> read+write on `/devices`, `/automation`, `/timers`, `/sensors`.
> The Flutter app uses its own auth or anonymous sign-in.

```json
{
  "rules": {
    "devices":   { ".read": "auth != null", ".write": "auth != null" },
    "automation":{ ".read": "auth != null", ".write": "auth != null" },
    "timers":    { ".read": "auth != null", ".write": "auth != null" },
    "sensors":   { ".read": "auth != null", ".write": "auth != null" }
  }
}
```

## Behaviour at runtime

* Every 5 s the firmware reads sensors and publishes them to `/sensors`.
* When the Flutter app writes `devices/*` or `automation/*`, the streaming
  callback applies them to the GPIOs and to the fan PWM.
* Timers (`/timers/*_off_time` epoch-millis) are checked every second; once
  one expires, the corresponding load is force-set to `false` and the
  timer is cleared.
* Automation policy:
    * `auto_fan` -> turns fan on (speed 2) when temp > 30°C
    * `auto_humidifier` -> turns humidifier on when RH < 35%
    * `auto_pump` -> turns pump on when water < 20%
    * *any* `smoke_detected = true` -> forces the pump OFF immediately
      (overrides auto_pump too).

## Threshold tuning

Open `home_bot_esp32.ino` and adjust:

```cpp
if (gAutomation.autoFan && !gDevices.fan && gSensors.temperature > 30.0) ...
if (gAutomation.autoHumidifier && !gDevices.humidifier && gSensors.humidity < 35.0) ...
if (gAutomation.autoPump && !gDevices.pump && gSensors.waterLevel < 20) ...
if (smokeRaw > 1500) // smoke threshold
```

These mirror the constants in `lib/services/firebase_service.dart` so any
tweak should be made in both places (or pushed to `/automation` instead, and
the firmware treats those as overrides).
