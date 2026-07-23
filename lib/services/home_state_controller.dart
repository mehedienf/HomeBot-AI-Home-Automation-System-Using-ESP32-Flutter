/// ============================================================================
/// home_state_controller.dart
/// Single source of truth for the dashboard. Exposes a [HomeSnapshot] that
/// the UI watches via Provider. Subscribes to the three Firebase streams in
/// the constructor, so as soon as the ESP32 (or anyone else) writes to
/// `/devices`, `/automation` or `/sensors`, `notifyListeners()` fires and
/// every screen rebuilds – no manual refresh, no pull-to-refresh.
/// ============================================================================

import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/device_state.dart';
import 'firebase_service.dart';

class HomeStateController extends ChangeNotifier {
  HomeStateController(this._firebase) {
    _devicesSub = _firebase.devicesStream.listen((d) {
      _snapshot = _snapshot.merge(devices: d);
      notifyListeners();
    });
    _automationSub = _firebase.automationStream.listen((a) {
      _snapshot = _snapshot.merge(automation: a);
      notifyListeners();
    });
    _sensorsSub = _firebase.sensorsStream.listen((s) {
      _snapshot = _snapshot.merge(sensors: s);
      notifyListeners();
    });
  }

  final FirebaseService _firebase;
  late final StreamSubscription<DeviceState> _devicesSub;
  late final StreamSubscription<AutomationState> _automationSub;
  late final StreamSubscription<SensorState> _sensorsSub;

  HomeSnapshot _snapshot = const HomeSnapshot();
  HomeSnapshot get snapshot {
    return _snapshot;
  }

  // Convenience getters so widgets can write `state.light` etc.
  bool get light {
    return _snapshot.devices.light;
  }

  bool get fan {
    return _snapshot.devices.fan;
  }

  int get fanSpeed {
    return _snapshot.devices.fanSpeed;
  }

  bool get pump {
    return _snapshot.devices.pump;
  }

  bool get humidifier {
    return _snapshot.devices.humidifier;
  }

  bool get autoFan {
    return _snapshot.automation.autoFan;
  }

  bool get autoHumidifier {
    return _snapshot.automation.autoHumidifier;
  }

  bool get autoPump {
    return _snapshot.automation.autoPump;
  }

  double get temperature {
    return _snapshot.sensors.temperature;
  }

  double get humidity {
    return _snapshot.sensors.humidity;
  }

  int get waterLevel {
    return _snapshot.sensors.waterLevel;
  }

  bool get smokeDetected {
    return _snapshot.sensors.smokeDetected;
  }

  // ---- Writes (delegate to FirebaseService) -------------------------------
  Future<void> setLight(bool v) {
    return _firebase.setDevice(light: v);
  }

  Future<void> setFan(bool v) {
    return _firebase.setDevice(fan: v);
  }

  Future<void> setPump(bool v) {
    return _firebase.setDevice(pump: v);
  }

  Future<void> setHumidifier(bool v) {
    return _firebase.setDevice(humidifier: v);
  }

  Future<void> setFanSpeed(int v) {
    return _firebase.setFanSpeed(v);
  }

  Future<void> setAutoFan(bool v) {
    return _firebase.setAutomationFlag(autoFan: v);
  }

  Future<void> setAutoHumidifier(bool v) {
    return _firebase.setAutomationFlag(autoHumidifier: v);
  }

  Future<void> setAutoPump(bool v) {
    return _firebase.setAutomationFlag(autoPump: v);
  }

  /// Schedule a one-shot "turn off" for a device at [when].
  Future<void> scheduleTurnOff({required DeviceId device, required DateTime when}) {
    return _firebase.setTimer(
      light: device == DeviceId.light,
      fan: device == DeviceId.fan,
      pump: device == DeviceId.pump,
      humidifier: device == DeviceId.humidifier,
      atEpochMillis: when,
    );
  }

  Future<void> clearTimer(DeviceId device) => _firebase.setTimer(
        light: device == DeviceId.light,
        fan: device == DeviceId.fan,
        pump: device == DeviceId.pump,
        humidifier: device == DeviceId.humidifier,
      );

  @override
  void dispose() {
    _devicesSub.cancel();
    _automationSub.cancel();
    _sensorsSub.cancel();
    super.dispose();
  }
}

enum DeviceId { light, fan, pump, humidifier }
