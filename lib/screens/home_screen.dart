// ============================================================================
// screens/home_screen.dart
// The main dashboard. Everything is driven by HomeStateController so any
// change pushed by the ESP32 (or by the chatbot) updates the UI instantly.
//
// Layout:
//   * Animated smoke-alert banner (on top, only when active)
//   * Sensor row (Temperature / Humidity)
//   * Water-tank gauge
//   * Manual controls: Light, Fan + speed, Pump, Humidifier
//   * Automation toggles
//   * "AI Chat" FAB that navigates to ChatScreen
// ============================================================================

import 'package:flutter/material.dart';
import 'package:flutter_spinkit/flutter_spinkit.dart';
import 'package:provider/provider.dart';


import '../services/home_state_controller.dart';
import '../widgets/alert_banner.dart';
import '../widgets/device_switch_card.dart';
import '../widgets/fan_speed_control.dart';
import '../widgets/sensor_card.dart';
import '../widgets/timer_dialog.dart';
import '../widgets/water_gauge.dart';
import 'chat_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  bool _alertDismissed = false;

  // Don't sit on a spinner forever if the RTDB stream hasn't fired yet
  // (e.g. cold start, auth delay, network blip). After 2.5 s we render
  // the rest of the dashboard so the user always sees *something*.
  bool _loadingTimedOut = false;
  @override
  void initState() {
    super.initState();
    Future.delayed(const Duration(milliseconds: 2500), () {
      if (!mounted) return;
      setState(() => _loadingTimedOut = true);
    });
  }

  Future<void> _setTimerFor(DeviceId device, String label) async {
    final state = context.read<HomeStateController>();
    final duration = await TimerDialog.show(context, label);
    if (duration == null) return;
    final when = DateTime.now().add(duration);
    await state.scheduleTurnOff(device: device, when: when);
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text('$label will turn off in ${duration.inMinutes} min'),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<HomeStateController>();
    final showAlert = state.smokeDetected && !_alertDismissed;

    return Scaffold(
      backgroundColor: const Color(0xFFF4F6FA),
      appBar: AppBar(
        title: const Column(
          crossAxisAlignment: .start,
          children: [
            Text('HomeBot'),
            Text('Patuakhali Science and Technology University', style: TextStyle(fontSize: 13),)
          ],
        ),
        elevation: 0,
        backgroundColor: Colors.transparent,
        foregroundColor: Colors.black87,
        // actions: [
        //   IconButton(
        //     tooltip: 'Reset alert',
        //     icon: const Icon(Icons.refresh),
        //     onPressed: () {
        //       setState(() {
        //         _alertDismissed = false;
        //       });
        //     },
        //   ),
        // ],
      ),
      
      floatingActionButton: FloatingActionButton.extended(
        onPressed: () {
          Navigator.of(context).push(
            MaterialPageRoute(builder: (_) {
              return const ChatScreen();
            }),
          );
        },
        icon: const Icon(Icons.smart_toy),
        label: const Text('HomeBot AI'),
      ),
      body: Column(
        children: [
          FireAlertBanner(
            visible: showAlert,
            onDismiss: () {
              setState(() {
                _alertDismissed = true;
              });
            },
          ),
          if (state.lastError != null)
            Container(
              width: double.infinity,
              margin: const EdgeInsets.fromLTRB(16, 12, 16, 0),
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: const Color(0xFFFFF3E0),
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: Colors.orange.shade300),
              ),
              child: Text(
                'Realtime DB error: ${state.lastError}',
                style: const TextStyle(fontSize: 12, color: Color(0xFF6D4C41)),
              ),
            ),
          Expanded(
            child: (!state.hasReceived &&
                    !_loadingTimedOut &&
                    state.lastError == null)
                ? const Center(child: SpinKitFadingCube(color: Colors.indigo))
                : RefreshIndicator(
                    onRefresh: () async {
                      // Streams update automatically, but pull-to-refresh
                      // is a nice gesture for users.
                      await Future<void>.delayed(
                          const Duration(milliseconds: 600));
                    },
                    child: ListView(
                      padding: const EdgeInsets.all(16),
                      children: [
                        Row(
                          children: [
                            Expanded(
                              child: SensorCard(
                                title: 'TEMPERATURE',
                                value: state.temperature.toStringAsFixed(1),
                                unit: '°C',
                                icon: Icons.thermostat,
                                color: Colors.deepOrange,
                              ),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: SensorCard(
                                title: 'HUMIDITY',
                                value: state.humidity.toStringAsFixed(1),
                                unit: '%',
                                icon: Icons.water,
                                color: Colors.blue,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 16),
                        WaterGauge(percent: state.waterLevel),
                        const SizedBox(height: 20),
                        _SectionLabel('Manual Controls'),
                        DeviceSwitchCard(
                          name: 'Light',
                          icon: Icons.lightbulb,
                          activeColor: Colors.amber.shade700,
                          value: state.light,
                          disabled: state.smokeDetected,
                          onChanged: state.setLight,
                          onSetTimer: () {
                            _setTimerFor(DeviceId.light, 'Light');
                          },
                        ),
                        DeviceSwitchCard(
                          name: 'Fan',
                          icon: Icons.air,
                          activeColor: Colors.blue,
                          value: state.fan,
                          onChanged: state.setFan,
                          onSetTimer: () {
                            _setTimerFor(DeviceId.fan, 'Fan');
                          },
                        ),
                        DeviceSwitchCard(
                          name: 'Water Pump',
                          icon: Icons.opacity,
                          activeColor: Colors.indigo,
                          value: state.pump,
                          disabled: state.smokeDetected,
                          onChanged: state.setPump,
                          onSetTimer: () {
                            _setTimerFor(DeviceId.pump, 'Pump');
                          },
                        ),
                        DeviceSwitchCard(
                          name: 'Humidifier',
                          icon: Icons.invert_colors,
                          activeColor: Colors.teal,
                          value: state.humidifier,
                          onChanged: state.setHumidifier,
                          onSetTimer: () {
                            _setTimerFor(DeviceId.humidifier, 'Humidifier');
                          },
                        ),
                        FanSpeedControl(
                          percent: state.fanSpeedPercent,
                          onChanged: (pct) async {
                            await state.setFanSpeedPct(pct);
                          },
                        ),
                        const SizedBox(height: 20),
                        _SectionLabel('Automation'),
                        _AutomationTile(
                          title: 'Auto Light',
                          subtitle: 'On when room is dark',
                          icon: Icons.lightbulb,
                          value: state.autoLight,
                          onChanged: state.setAutoLight,
                        ),
                        _AutomationTile(
                          title: 'Auto Fan',
                          subtitle: 'Speed up when it gets hot',
                          icon: Icons.air,
                          value: state.autoFan,
                          onChanged: state.setAutoFan,
                        ),
                        _AutomationTile(
                          title: 'Auto Humidifier',
                          subtitle: 'Run when air is dry',
                          icon: Icons.invert_colors,
                          value: state.autoHumidifier,
                          onChanged: state.setAutoHumidifier,
                        ),
                        _AutomationTile(
                          title: 'Auto Pump',
                          subtitle: 'Top-up tank when low',
                          icon: Icons.opacity,
                          value: state.autoPump,
                          onChanged: state.setAutoPump,
                        ),
                        const SizedBox(height: 80),
                      ],
                    ),
                  ),
          ),
        ],
      ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  final String text;
  const _SectionLabel(this.text);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: 4, bottom: 10),
      child: Text(
        text,
        style: const TextStyle(
          fontSize: 13,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.5,
          color: Colors.black54,
        ),
      ),
    );
  }
}

class _AutomationTile extends StatelessWidget {
  final String title;
  final String subtitle;
  final IconData icon;
  final bool value;
  final ValueChanged<bool> onChanged;

  const _AutomationTile({
    required this.title,
    required this.subtitle,
    required this.icon,
    required this.value,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: Colors.grey.shade300),
      ),
      child: Row(
        children: [
          Icon(icon, color: Colors.indigo),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title,
                    style: const TextStyle(fontWeight: FontWeight.w600)),
                Text(subtitle,
                    style: TextStyle(
                        fontSize: 12, color: Colors.grey.shade600)),
              ],
            ),
          ),
          Switch(value: value, onChanged: onChanged),
        ],
      ),
    );
  }
}