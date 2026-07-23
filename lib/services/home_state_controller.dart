/// ============================================================================
/// home_state_controller.dart
/// Single source of truth for the dashboard. Exposes a [HomeSnapshot] that
/// the UI watches via Provider. Subscribes to the three Firebase streams in
/// the constructor, so as soon as the ESP32 (or anyone else) writes to
/// `/devices`, `/automation` or `/sensors`, `notifyListeners()` fires and
/// every screen rebuilds – no manual refresh, no pull-to-refresh.
/// ============================================================================
library;

import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/device_state.dart';
import 'firebase_service.dart';

class HomeStateController extends ChangeNotifier {
  HomeStateController(this._firebase) {
    _devicesSub = _firebase.devicesStream.listen(
      (d) {
        _snapshot = _snapshot.merge(devices: d);
        _hasReceived = true;
        _lastError = null;
        notifyListeners();
      },
      onError: (Object e, StackTrace st) {
        // RTDB rejected the read (auth / rules / URL mismatch). Surface it
        // so the UI can show a useful message and the spinner stops.
        debugPrint('devicesStream error: $e\n$st');
        _lastError = 'devices: $e';
        _hasReceived = true;
        notifyListeners();
      },
    );
    _automationSub = _firebase.automationStream.listen(
      (a) {
        _snapshot = _snapshot.merge(automation: a);
        _hasReceived = true;
        notifyListeners();
      },
      onError: (Object e, StackTrace st) {
        debugPrint('automationStream error: $e\n$st');
        _lastError ??= 'automation: $e';
        _hasReceived = true;
        notifyListeners();
      },
    );
    _sensorsSub = _firebase.sensorsStream.listen(
      (s) {
        _snapshot = _snapshot.merge(sensors: s);
        _hasReceived = true;
        notifyListeners();
      },
      onError: (Object e, StackTrace st) {
        debugPrint('sensorsStream error: $e\n$st');
        _lastError ??= 'sensors: $e';
        _hasReceived = true;
        notifyListeners();
      },
    );
  }

  final FirebaseService _firebase;
  late final StreamSubscription<DeviceState> _devicesSub;
  late final StreamSubscription<AutomationState> _automationSub;
  late final StreamSubscription<SensorState> _sensorsSub;

  HomeSnapshot _snapshot = const HomeSnapshot();
  bool _hasReceived = false;
  String? _lastError;
  HomeSnapshot get snapshot => _snapshot;

  /// Becomes `true` the first time any of the RTDB streams fires. The home
  /// screen uses this to drop its initial "loading" spinner once data has
  /// actually flowed from Firebase (instead of comparing against the
  /// default snapshot, which can be indistinguishable from a real but
  /// uninitialized RTDB node).
  bool get hasReceived => _hasReceived;

  /// Most recent RTDB stream error, or `null` if reads have succeeded. The
  /// UI can show this above the dashboard so the user knows the blank state
  /// is a permission/URL problem, not a device problem.
  String? get lastError => _lastError;

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
  Future<void> setLight(bool v) async {
    try {
      await _firebase.setDevice(light: v);
    } catch (e) {
      _lastError = 'setLight: $e';
      notifyListeners();
    }
  }

  Future<void> setFan(bool v) async {
    try {
      await _firebase.setDevice(fan: v);
    } catch (e) {
      _lastError = 'setFan: $e';
      notifyListeners();
    }
  }

  Future<void> setPump(bool v) async {
    try {
      await _firebase.setDevice(pump: v);
    } catch (e) {
      _lastError = 'setPump: $e';
      notifyListeners();
    }
  }

  Future<void> setHumidifier(bool v) async {
    try {
      await _firebase.setDevice(humidifier: v);
    } catch (e) {
      _lastError = 'setHumidifier: $e';
      notifyListeners();
    }
  }

  Future<void> setFanSpeed(int v) async {
    try {
      await _firebase.setFanSpeed(v);
    } catch (e) {
      _lastError = 'setFanSpeed: $e';
      notifyListeners();
    }
  }

  Future<void> setAutoFan(bool v) async {
    try {
      await _firebase.setAutomationFlag(autoFan: v);
    } catch (e) {
      _lastError = 'setAutoFan: $e';
      notifyListeners();
    }
  }

  Future<void> setAutoHumidifier(bool v) async {
    try {
      await _firebase.setAutomationFlag(autoHumidifier: v);
    } catch (e) {
      _lastError = 'setAutoHumidifier: $e';
      notifyListeners();
    }
  }

  Future<void> setAutoPump(bool v) async {
    try {
      await _firebase.setAutomationFlag(autoPump: v);
    } catch (e) {
      _lastError = 'setAutoPump: $e';
      notifyListeners();
    }
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
