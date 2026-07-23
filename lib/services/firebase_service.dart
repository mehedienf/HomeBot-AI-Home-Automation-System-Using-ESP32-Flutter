// ============================================================================
// firebase_service.dart
// Thin wrapper around Firebase Realtime Database. Exposes:
//
//   * `devicesStream`, `automationStream`, `sensorsStream` – live snapshots
//     from the ESP32. The UI listens to these via a Provider so every
//     change pushed by the microcontroller is reflected immediately.
//
//   * `setDevice()`, `setFanSpeed()`, `setAutomationFlag()`, `setTimer()`
//     – write helpers used by both the dashboard switches and the Gemini
//     service (when an AI command resolves to a device action).
//
// We keep RTDB references in one place so the path schema lives in a single
// file – if you ever rename a node in Firebase, you only edit it here.
// ============================================================================

import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/foundation.dart';

import '../models/device_state.dart';

class FirebaseService {
  // Singleton-ish – created once from main.dart and injected via Provider.
  FirebaseService();

  final FirebaseDatabase _db = FirebaseDatabase.instance;

  // --- Refs ----------------------------------------------------------------
  DatabaseReference get _devicesRef => _db.ref('devices');
  DatabaseReference get _automationRef => _db.ref('automation');
  DatabaseReference get _sensorsRef => _db.ref('sensors');
  DatabaseReference get _timersRef => _db.ref('timers');

  // --- Streams (live UI sync) ---------------------------------------------

  /// Emits a [DeviceState] every time ESP32 (or anyone else) writes to
  /// `/devices`.
  Stream<DeviceState> get devicesStream => _devicesRef.onValue.map((event) {
        return DeviceState.fromMap(event.snapshot.value as Map<dynamic, dynamic>?);
      });

  Stream<AutomationState> get automationStream => _automationRef.onValue.map((event) {
        return AutomationState.fromMap(event.snapshot.value as Map<dynamic, dynamic>?);
      });

  Stream<SensorState> get sensorsStream => _sensorsRef.onValue.map((event) {
        return SensorState.fromMap(event.snapshot.value as Map<dynamic, dynamic>?);
      });

  // --- Writes --------------------------------------------------------------

  Future<void> setDevice({
    bool? light,
    bool? fan,
    bool? pump,
    bool? humidifier,
  }) async {
    final updates = <String, dynamic>{};
    if (light != null) updates['light'] = light;
    if (fan != null) updates['fan'] = fan;
    if (pump != null) updates['pump'] = pump;
    if (humidifier != null) updates['humidifier'] = humidifier;
    if (updates.isEmpty) return;
    await _devicesRef.update(updates);
  }

  /// fanSpeed is a discrete 0..4 integer that maps to the ESP32 PWM duty.
  Future<void> setFanSpeed(int speed) {
    final clamped = speed.clamp(0, 4);
    return _devicesRef.update({'fan_speed': clamped});
  }

  Future<void> setAutomationFlag({
    bool? autoFan,
    bool? autoHumidifier,
    bool? autoPump,
  }) async {
    final updates = <String, dynamic>{};
    if (autoFan != null) updates['auto_fan'] = autoFan;
    if (autoHumidifier != null) updates['auto_humidifier'] = autoHumidifier;
    if (autoPump != null) updates['auto_pump'] = autoPump;
    if (updates.isEmpty) return;
    return _automationRef.update(updates);
  }

  /// Schedules a one-shot "turn off" for a load. ESP32 watches `/timers/*`
  /// and turns the corresponding device off when the epoch-millis is reached.
  ///
  /// We pass `atEpochMillis` (UTC). Pass `null` to clear an existing timer.
  Future<void> setTimer({
    bool light = false,
    bool fan = false,
    bool pump = false,
    bool humidifier = false,
    DateTime? atEpochMillis,
  }) async {
    final updates = <String, dynamic>{};
    if (light) updates['light_off_time'] = atEpochMillis?.millisecondsSinceEpoch ?? 0;
    if (fan) updates['fan_off_time'] = atEpochMillis?.millisecondsSinceEpoch ?? 0;
    if (pump) updates['pump_off_time'] = atEpochMillis?.millisecondsSinceEpoch ?? 0;
    if (humidifier) updates['humidifier_off_time'] = atEpochMillis?.millisecondsSinceEpoch ?? 0;
    if (updates.isEmpty) return;
    return _timersRef.update(updates);
  }

  /// Apply a generic key->value patch produced by the Gemini service.
  /// `db_update` is expected to look like:
  ///   {"devices/light": true, "devices/fan_speed": 3, "timers/pump_off_time": 0}
  Future<void> applyGeminiUpdates(Map<String, dynamic> dbUpdate) async {
    if (dbUpdate.isEmpty) return;
    try {
      await _db.ref('/').update(dbUpdate);
    } catch (e) {
      debugPrint('applyGeminiUpdates failed: $e');
    }
  }

  /// Convenience for read-only sensor queries from the chatbot.
  Future<SensorState> readSensors() async {
    final snap = await _sensorsRef.get();
    return SensorState.fromMap(snap.value as Map<dynamic, dynamic>?);
  }
}