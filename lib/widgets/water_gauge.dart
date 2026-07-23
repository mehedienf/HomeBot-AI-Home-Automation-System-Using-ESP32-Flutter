/// ============================================================================
/// widgets/water_gauge.dart
/// Visual vertical water-tank indicator driven by percent_indicator.
/// Colour shifts as the level drops so a glance tells you when to refill.
/// ============================================================================
library;

import 'package:flutter/material.dart';
import 'package:percent_indicator/linear_percent_indicator.dart';

class WaterGauge extends StatelessWidget {
  final int percent; // 0..100

  const WaterGauge({super.key, required this.percent});

  Color get _color {
    if (percent <= 15) {
      return Colors.redAccent;
    }
    if (percent <= 35) {
      return Colors.orangeAccent;
    }
    return Colors.blueAccent;
  }

  String get _label {
    if (percent <= 15) {
      return 'Critically low - refill soon';
    }
    if (percent <= 35) {
      return 'Running low';
    }
    if (percent <= 70) {
      return 'Healthy';
    }
    return 'Full';
  }

  @override
  Widget build(BuildContext context) {
    final clamped = percent.clamp(0, 100);
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(20),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withValues(alpha: 0.05),
            blurRadius: 10,
            offset: const Offset(0, 4),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.water_drop, color: _color),
              const SizedBox(width: 8),
              const Text(
                'Water Tank',
                style: TextStyle(fontWeight: FontWeight.w600, fontSize: 14),
              ),
              const Spacer(),
              Text(
                '$clamped %',
                style: TextStyle(
                  color: _color,
                  fontWeight: FontWeight.bold,
                  fontSize: 16,
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          LinearPercentIndicator(
            lineHeight: 18,
            percent: clamped / 100,
            progressColor: _color,
            backgroundColor: Colors.grey.shade200,
            barRadius: const Radius.circular(12),
            animation: true,
            animationDuration: 600,
            padding: EdgeInsets.zero,
          ),
          const SizedBox(height: 10),
          Text(
            _label,
            style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
          ),
        ],
      ),
    );
  }
}