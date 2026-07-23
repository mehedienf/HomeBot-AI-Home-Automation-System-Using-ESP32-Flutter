# HomeBot

Voice & text controlled smart-home automation app with Gemini AI and
Firebase Realtime Database. Interfaces with an ESP32 microcontroller that
reads sensors and drives the loads (light, fan, pump, humidifier).

## Quick start

```bash
flutter pub get
flutterfire configure      # generates firebase_options.dart + native config
flutter run --dart-define=GEMINI_API_KEY=YOUR_KEY
```

## Firebase Realtime DB schema (matches ESP32 firmware)

```json
{
  "devices":   { "light": false, "fan": false, "fan_speed": 0,
                 "pump": false, "humidifier": false },
  "automation":{ "auto_fan": true, "auto_humidifier": true, "auto_pump": true },
  "sensors":   { "temperature": 0.0, "humidity": 0.0,
                 "water_level": 0, "smoke_detected": false },
  "timers":    { "light_off_time": 0, "fan_off_time": 0,
                 "pump_off_time": 0, "humidifier_off_time": 0 }
}
```

## Architecture

* `lib/services/firebase_service.dart` — RTDB streams + writes
* `lib/services/home_state_controller.dart` — `ChangeNotifier` that fans
  RTDB streams into a single `HomeSnapshot` so the UI updates in real time
* `lib/services/gemini_service.dart` — Gemini call with a strict system
  prompt that forces a `{reply, db_update}` JSON response
* `lib/services/voice_service.dart` — `speech_to_text` + `flutter_tts`
* `lib/screens/home_screen.dart` — dashboard (sensors, switches,
  fan speed, automation, timers)
* `lib/screens/chat_screen.dart` — voice + text chatbot

## Voice / text command examples

* "Turn on the light"
* "Set fan speed to 3"
* "Turn off the pump in 15 minutes"
* "How much water is in the tank?"
* "Shut everything down"

The assistant always returns JSON, parsed in
`gemini_service.dart::_parse`, and the `db_update` map is written to RTDB
through `firebase_service.dart::applyGeminiUpdates`.


lib/
├── main.dart                                 # Firebase init + MultiProvider
├── models/device_state.dart                  # DeviceState, AutomationState, SensorState, HomeSnapshot
├── services/
│   ├── firebase_service.dart                 # RTDB streams + writes
│   ├── home_state_controller.dart            # ChangeNotifier - live UI sync
│   ├── gemini_service.dart                   # Gemini call + JSON parsing + system prompt
│   └── voice_service.dart                    # speech_to_text + flutter_tts wrapper
├── screens/
│   ├── home_screen.dart                      # Sensors, gauge, switches, fan speed, timers
│   └── chat_screen.dart                      # Voice + text Gemini chatbot
└── widgets/
    ├── sensor_card.dart                      # Temp / humidity cards
    ├── water_gauge.dart                      # percent_indicator-based tank
    ├── alert_banner.dart                     # Animated fire/smoke banner
    ├── device_switch_card.dart               # Light/pump/humidifier row
    ├── fan_speed_control.dart                # Discrete 0–4 step buttons
    └── timer_dialog.dart                     # "turn off in X minutes" picker