/// ============================================================================
/// widgets/fan_speed_control.dart
/// Continuous 0..100% slider for fan speed. The widget only knows about
/// percent; the controller converts to 0..255 before writing to
/// `/devices/fan_speed` and toggles `/devices/fan` so the ESP32 doesn't
/// need to read two nodes for state.
///
/// Smoothness strategy:
///   * Slider runs as a StatefulWidget — drag works against a local double
///     value so the thumb moves in real time without waiting for RTDB.
///   * divisions: null  → no quantization, fully continuous drag.
///   * onChangeStart captures the current value; onChangeEnd flushes the
///     final percent to the network exactly once per drag gesture. Mid-drag
///     updates still call onChanged so the UI stays in sync, but the parent
///     treats them as a stream (no extra pressure on Firebase).
/// ============================================================================
library;

import 'package:flutter/material.dart';

class FanSpeedControl extends StatefulWidget {
  /// Fan speed in percent (0..100). 0 = off.
  final int percent;
  final ValueChanged<int> onChanged;

  const FanSpeedControl({
    super.key,
    required this.percent,
    required this.onChanged,
  });

  @override
  State<FanSpeedControl> createState() => _FanSpeedControlState();
}

class _FanSpeedControlState extends State<FanSpeedControl> {
  /// Local double so drag is sub-pixel smooth. We only commit rounded ints
  /// to the parent when the value actually changes.
  late double _local;

  /// Set true while the user is actively dragging the thumb. While dragging
  /// we ignore incoming widget.percent changes from RTDB echo so the thumb
  /// does not snap back to the pre-write value.
  bool _dragging = false;

  @override
  void initState() {
    super.initState();
    _local = widget.percent.clamp(0, 100).toDouble();
  }

  @override
  void didUpdateWidget(covariant FanSpeedControl oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (_dragging) return; // Don't override local drag with stale RTDB echo.
    final incoming = widget.percent.clamp(0, 100).toDouble();
    if ((incoming - _local).abs() > 0.5) {
      _local = incoming;
    }
  }

  void _emit(double v) {
    final rounded = v.round();
    if (rounded != widget.percent) {
      widget.onChanged(rounded);
    }
  }

  @override
  Widget build(BuildContext context) {
    final displayPercent = _local.round();
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: Colors.grey.shade300),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.air, color: Colors.cyan.shade600),
              const SizedBox(width: 10),
              const Text(
                'Fan Speed',
                style: TextStyle(fontWeight: FontWeight.w600, fontSize: 15),
              ),
              const Spacer(),
              Text(
                displayPercent == 0 ? 'OFF' : '$displayPercent%',
                style: TextStyle(
                  color: Colors.cyan.shade700,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          SliderTheme(
            data: SliderTheme.of(context).copyWith(
              activeTrackColor: Colors.cyan.shade600,
              inactiveTrackColor: Colors.cyan.shade100,
              thumbColor: Colors.cyan.shade700,
              overlayColor: Colors.cyan.withValues(alpha: 0.18),
              trackHeight: 6,
              thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 10),
              showValueIndicator: ShowValueIndicator.onDrag,
            ),
            child: Slider(
              value: _local,
              min: 0,
              max: 100,
              // divisions: null  → continuous, no quantization snap.
              label: displayPercent == 0 ? 'OFF' : '$displayPercent%',
              onChangeStart: (_) => _dragging = true,
              onChanged: (v) {
                setState(() => _local = v);
                _emit(v);
              },
              onChangeEnd: (v) {
                _dragging = false;
                // Final flush — even if rounded value didn't change, ensure
                // the latest contiguous position is sent.
                _emit(v);
                _local = v;
              },
            ),
          ),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text('OFF',
                  style: TextStyle(
                      fontSize: 11,
                      color: Colors.grey.shade600,
                      fontWeight: displayPercent == 0
                          ? FontWeight.bold
                          : FontWeight.normal)),
              Text('50%',
                  style: TextStyle(
                      fontSize: 11,
                      color: Colors.grey.shade600,
                      fontWeight: displayPercent == 50
                          ? FontWeight.bold
                          : FontWeight.normal)),
              Text('100%',
                  style: TextStyle(
                      fontSize: 11,
                      color: Colors.grey.shade600,
                      fontWeight: displayPercent == 100
                          ? FontWeight.bold
                          : FontWeight.normal)),
            ],
          ),
        ],
      ),
    );
  }
}
