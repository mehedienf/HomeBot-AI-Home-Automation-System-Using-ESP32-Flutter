/// ============================================================================
/// widgets/timer_dialog.dart
/// Bottom-sheet style dialog that lets the user pick a "turn off in X
/// minutes" duration for a given load. Returns the chosen Duration, or
/// null if the user cancelled.
/// ============================================================================
library;

import 'package:flutter/material.dart';

class TimerDialog extends StatefulWidget {
  final String deviceName;

  const TimerDialog({super.key, required this.deviceName});

  /// Opens the dialog and resolves with the chosen duration, or null.
  static Future<Duration?> show(BuildContext context, String deviceName) {
    return showModalBottomSheet<Duration>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) {
        return TimerDialog(deviceName: deviceName);
      },
    );
  }

  @override
  State<TimerDialog> createState() {
    return _TimerDialogState();
  }
}

class _TimerDialogState extends State<TimerDialog> {
  int _minutes = 15;
  final List<int> _presets = const [5, 15, 30, 60, 120];

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.only(
        bottom: MediaQuery.of(context).viewInsets.bottom,
      ),
      child: Container(
        decoration: const BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
        ),
        padding: const EdgeInsets.all(20),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Center(
              child: Container(
                width: 40,
                height: 4,
                decoration: BoxDecoration(
                  color: Colors.grey.shade300,
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
            ),
            const SizedBox(height: 16),
            Text(
              'Turn off ${widget.deviceName} in...',
              style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w600),
            ),
            const SizedBox(height: 16),
            Wrap(
              spacing: 8,
              children: _presets.map((m) {
                final selected = m == _minutes;
                return ChoiceChip(
                  label: Text('${m}m'),
                  selected: selected,
                  onSelected: (_) {
                    setState(() {
                      _minutes = m;
                    });
                  },
                );
              }).toList(),
            ),
            const SizedBox(height: 16),
            Row(
              children: [
                const Text('Custom:'),
                const SizedBox(width: 12),
                Expanded(
                  child: Slider(
                    min: 1,
                    max: 180,
                    divisions: 179,
                    label: '${_minutes.round()}m',
                    value: _minutes.toDouble(),
                    onChanged: (v) {
                      setState(() {
                        _minutes = v.round();
                      });
                    },
                  ),
                ),
                SizedBox(
                  width: 48,
                  child: Text(
                    '${_minutes}m',
                    style: const TextStyle(fontWeight: FontWeight.bold),
                    textAlign: TextAlign.right,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
            SizedBox(
              width: double.infinity,
              child: ElevatedButton.icon(
                onPressed: () {
                  Navigator.of(context).pop(Duration(minutes: _minutes));
                },
                icon: const Icon(Icons.timer),
                label: const Text('Start timer'),
                style: ElevatedButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 14),
                ),
              ),
            ),
            const SizedBox(height: 6),
            SizedBox(
              width: double.infinity,
              child: TextButton(
                onPressed: () {
                  Navigator.of(context).pop();
                },
                child: const Text('Cancel'),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
