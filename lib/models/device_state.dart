// ============================================================================
// device_state.dart
// Plain Dart models that mirror the Firebase Realtime Database schema.
// Each model has fromMap() / toMap() so we can decode RTDB snapshots and
// encode writes back to the ESP32.
// ============================================================================

class DeviceState {
  final bool light;
  final bool fan;
  final int fanSpeed; // 0..4
  final bool pump;
  final bool humidifier;

  const DeviceState({
    this.light = false,
    this.fan = false,
    this.fanSpeed = 0,
    this.pump = false,
    this.humidifier = false,
  });

  factory DeviceState.fromMap(Map<dynamic, dynamic>? map) {
    final m = map ?? const {};
    return DeviceState(
      light: m['light'] as bool? ?? false,
      fan: m['fan'] as bool? ?? false,
      fanSpeed: (m['fan_speed'] as num?)?.toInt() ?? 0,
      pump: m['pump'] as bool? ?? false,
      humidifier: m['humidifier'] as bool? ?? false,
    );
  }

  Map<String, dynamic> toMap() => {
    'light': light,
    'fan': fan,
    'fan_speed': fanSpeed,
    'pump': pump,
    'humidifier': humidifier,
  };

  DeviceState copyWith({
    bool? light,
    bool? fan,
    int? fanSpeed,
    bool? pump,
    bool? humidifier,
  }) {
    return DeviceState(
      light: light ?? this.light,
      fan: fan ?? this.fan,
      fanSpeed: fanSpeed ?? this.fanSpeed,
      pump: pump ?? this.pump,
      humidifier: humidifier ?? this.humidifier,
    );
  }
}

class AutomationState {
  final bool autoFan;
  final bool autoHumidifier;
  final bool autoPump;

  const AutomationState({
    this.autoFan = true,
    this.autoHumidifier = true,
    this.autoPump = true,
  });

  factory AutomationState.fromMap(Map<dynamic, dynamic>? map) {
    final m = map ?? const {};
    return AutomationState(
      autoFan: m['auto_fan'] as bool? ?? true,
      autoHumidifier: m['auto_humidifier'] as bool? ?? true,
      autoPump: m['auto_pump'] as bool? ?? true,
    );
  }

  Map<String, dynamic> toMap() => {
    'auto_fan': autoFan,
    'auto_humidifier': autoHumidifier,
    'auto_pump': autoPump,
  };

  AutomationState copyWith({
    bool? autoFan,
    bool? autoHumidifier,
    bool? autoPump,
  }) {
    return AutomationState(
      autoFan: autoFan ?? this.autoFan,
      autoHumidifier: autoHumidifier ?? this.autoHumidifier,
      autoPump: autoPump ?? this.autoPump,
    );
  }
}

class SensorState {
  final double temperature;
  final double humidity;
  final int waterLevel; // 0..100
  final bool smokeDetected;

  const SensorState({
    this.temperature = 0.0,
    this.humidity = 0.0,
    this.waterLevel = 0,
    this.smokeDetected = false,
  });

  factory SensorState.fromMap(Map<dynamic, dynamic>? map) {
    final m = map ?? const {};
    return SensorState(
      temperature: (m['temperature'] as num?)?.toDouble() ?? 0.0,
      humidity: (m['humidity'] as num?)?.toDouble() ?? 0.0,
      waterLevel: (m['water_level'] as num?)?.toInt() ?? 0,
      smokeDetected: m['smoke_detected'] as bool? ?? false,
    );
  }

  Map<String, dynamic> toMap() => {
    'temperature': temperature,
    'humidity': humidity,
    'water_level': waterLevel,
    'smoke_detected': smokeDetected,
  };
}

/// Aggregated "everything the dashboard cares about" snapshot.
class HomeSnapshot {
  final DeviceState devices;
  final AutomationState automation;
  final SensorState sensors;

  const HomeSnapshot({
    this.devices = const DeviceState(),
    this.automation = const AutomationState(),
    this.sensors = const SensorState(),
  });

  /// Merges fresh partial updates (from /devices, /automation, /sensors streams).
  HomeSnapshot merge({
    DeviceState? devices,
    AutomationState? automation,
    SensorState? sensors,
  }) {
    return HomeSnapshot(
      devices: devices ?? this.devices,
      automation: automation ?? this.automation,
      sensors: sensors ?? this.sensors,
    );
  }
}
