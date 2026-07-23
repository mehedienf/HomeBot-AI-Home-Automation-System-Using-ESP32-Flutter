/// ============================================================================
/// widgets/device_switch_card.dart
/// A row card containing an icon, a name, an action button (open a timer
/// dialog) and the on/off switch. Used for Light, Pump, Humidifier.
/// ============================================================================
library;

import 'package:flutter/material.dart';

class DeviceSwitchCard extends StatelessWidget {
  final String name;
  final IconData icon;
  final Color activeColor;
  final bool value;
  final bool disabled;
  final ValueChanged<bool> onChanged;
  final VoidCallback onSetTimer;

  const DeviceSwitchCard({
    super.key,
    required this.name,
    required this.icon,
    required this.activeColor,
    required this.value,
    required this.onChanged,
    required this.onSetTimer,
    this.disabled = false,
  });

  @override
  Widget build(BuildContext context) {
    return Opacity(
      opacity: disabled ? 0.5 : 1,
      child: Container(
        margin: const EdgeInsets.only(bottom: 12),
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        decoration: BoxDecoration(
          color: value ? activeColor.withValues(alpha: 0.10) : Colors.white,
          borderRadius: BorderRadius.circular(18),
          border: Border.all(
            color: value ? activeColor : Colors.grey.shade300,
            width: 1.2,
          ),
        ),
        child: Row(
          children: [
            Container(
              width: 42,
              height: 42,
              decoration: BoxDecoration(
                color: value ? activeColor : Colors.grey.shade100,
                borderRadius: BorderRadius.circular(12),
              ),
              child: Icon(
                icon,
                color: value ? Colors.white : Colors.grey.shade600,
              ),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    name,
                    style: const TextStyle(
                      fontWeight: FontWeight.w600,
                      fontSize: 15,
                    ),
                  ),
                  Text(
                    value ? 'ON' : 'OFF',
                    style: TextStyle(
                      color: value ? activeColor : Colors.grey,
                      fontSize: 12,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ],
              ),
            ),
            IconButton(
              icon: const Icon(Icons.timer_outlined),
              tooltip: 'Set auto-off timer',
              onPressed: disabled ? null : onSetTimer,
            ),
            Switch(
              value: value,
              activeThumbColor: activeColor,
              onChanged: disabled ? null : onChanged,
            ),
          ],
        ),
      ),
    );
  }
}
