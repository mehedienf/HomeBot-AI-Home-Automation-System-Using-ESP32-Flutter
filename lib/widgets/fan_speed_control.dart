/// ============================================================================
/// widgets/fan_speed_control.dart
/// Five step buttons (0..4) for discrete fan speed. Writes the chosen level
/// to `/devices/fan_speed` and toggles `/devices/fan` to true at the same
/// time so the ESP32 doesn't need to read two nodes for state.
/// ============================================================================

import 'package:flutter/material.dart';

class FanSpeedControl extends StatelessWidget {
  final int speed;
  final ValueChanged<int> onChanged;

  const FanSpeedControl({
    super.key,
    required this.speed,
    required this.onChanged,
  });

  static const _labels = ['OFF', 'LOW', 'MED', 'HIGH', 'MAX'];

  @override
  Widget build(BuildContext context) {
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
                speed == 0 ? 'OFF' : _labels[speed],
                style: TextStyle(
                  color: Colors.cyan.shade700,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: List.generate(5, (i) {
              final selected = speed == i;
              return Expanded(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 3),
                  child: ElevatedButton(
                    onPressed: () {
                      onChanged(i);
                    },
                    style: ElevatedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(vertical: 12),
                      backgroundColor: selected
                          ? Colors.cyan.shade600
                          : Colors.grey.shade100,
                      foregroundColor:
                          selected ? Colors.white : Colors.grey.shade700,
                      elevation: selected ? 2 : 0,
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12),
                      ),
                    ),
                    child: Text(
                      '$i',
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                  ),
                ),
              );
            }),
          ),
        ],
      ),
    );
  }
}