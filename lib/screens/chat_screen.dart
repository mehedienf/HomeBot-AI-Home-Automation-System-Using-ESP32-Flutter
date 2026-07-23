// ============================================================================
// screens/chat_screen.dart
// AI Voice + Text assistant powered by Gemini.
//
// Flow per user turn:
//   1) text + mic input -> user utterance string
//   2) prime Gemini's context with the latest RTDB snapshot (so it can
//      answer "how warm is it?" without an extra round-trip).
//   3) call Gemini -> GeminiAction { reply, db_update }
//   4) apply db_update to Firebase (the ESP32 sees it immediately because
//      we also stream `/devices` back into the dashboard).
//   5) show reply as a chat bubble and speak it via TTS.
// ============================================================================

import 'package:flutter/material.dart';
import 'package:flutter_spinkit/flutter_spinkit.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../services/firebase_service.dart';
import '../services/gemini_service.dart';
import '../services/home_state_controller.dart';
import '../services/voice_service.dart';

class ChatMessage {
  final String text;
  final bool fromUser;
  final DateTime at;
  final bool hadAction;

  ChatMessage({
    required this.text,
    required this.fromUser,
    this.hadAction = false,
    DateTime? at,
  }) : at = at ?? DateTime.now();
}

class ChatScreen extends StatefulWidget {
  const ChatScreen({super.key});

  @override
  State<ChatScreen> createState() {
    return _ChatScreenState();
  }
}

class _ChatScreenState extends State<ChatScreen> {
  final TextEditingController _inputCtrl = TextEditingController();
  final ScrollController _scrollCtrl = ScrollController();
  final List<ChatMessage> _messages = [];
  bool _busy = false;
  bool _listening = false;
  late final VoiceService _voice;

  @override
  void initState() {
    super.initState();
    _voice = VoiceService();
    _voice.init();

    // Greet the user once we have live data.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final snap = context.read<HomeStateController>().snapshot;
      _addAssistant(
          'Hi, I\'m HomeBot. You can say things like "turn on the light", '
          '"set fan speed to 3", or "how full is the tank?". '
          'Latest sensors: '
          '${snap.sensors.temperature.toStringAsFixed(1)}°C, '
          '${snap.sensors.humidity.toStringAsFixed(1)}% RH, '
          'tank ${snap.sensors.waterLevel}%.',
          speak: false);
    });
  }

  void _addAssistant(String text, {bool speak = true}) {
    setState(() {
      _messages.add(ChatMessage(text: text, fromUser: false));
    });
    _scrollToBottom();
    if (speak) {
      _voice.speak(text);
    }
  }

  void _addUser(String text) {
    setState(() {
      _messages.add(ChatMessage(text: text, fromUser: true));
    });
    _scrollToBottom();
  }

  void _scrollToBottom() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scrollCtrl.hasClients) {
        _scrollCtrl.animateTo(
          _scrollCtrl.position.maxScrollExtent,
          duration: const Duration(milliseconds: 250),
          curve: Curves.easeOut,
        );
      }
    });
  }

  Future<void> _toggleListening() async {
    if (_listening) {
      await _voice.stopListening();
      setState(() {
        _listening = false;
      });
      return;
    }
    setState(() {
      _listening = true;
    });
    await _voice.listen(
      onFinalResult: (text) async {
        setState(() {
          _listening = false;
        });
        if (text.trim().isNotEmpty) {
          await _handleUserInput(text);
        }
      },
      onPartial: (text) {
        _inputCtrl.text = text;
        _inputCtrl.selection =
            TextSelection.collapsed(offset: _inputCtrl.text.length);
      },
    );
  }

  Future<void> _handleUserInput(String raw) async {
    final text = raw.trim();
    if (text.isEmpty) return;
    _inputCtrl.clear();

    _addUser(text);
    setState(() {
      _busy = true;
    });

    try {
      final gemini = context.read<GeminiService>();
      final firebase = context.read<FirebaseService>();
      final home = context.read<HomeStateController>();

      // Re-prime the model with the latest home state so it answers
      // status questions accurately and avoids redundant writes.
      gemini.primeContext(
        sensors: home.snapshot.sensors,
        devices: home.snapshot.devices,
        automation: home.snapshot.automation,
      );

      final action = await gemini.sendMessage(text);

      if (action.hasAction) {
        await firebase.applyGeminiUpdates(action.dbUpdate);
      }

      _addAssistant(action.reply, speak: true);
      if (action.hasAction) {
        _addAssistant(
            'Applied: ${action.dbUpdate.entries.map((e) => "${e.key}=${e.value}").join(", ")}',
            speak: false);
      }
    } catch (e) {
      _addAssistant('Sorry, I had trouble reaching the assistant. ($e)');
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
        });
      }
    }
  }

  @override
  void dispose() {
    _voice.dispose();
    _inputCtrl.dispose();
    _scrollCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('AI Assistant'),
        actions: [
          IconButton(
            tooltip: 'Stop speaking',
            icon: const Icon(Icons.volume_up),
            onPressed: () {
              _voice.stopSpeaking();
            },
          ),
        ],
      ),
      body: Column(
        children: [
          Expanded(
            child: ListView.builder(
              controller: _scrollCtrl,
              padding: const EdgeInsets.all(12),
              itemCount: _messages.length,
              itemBuilder: (context, i) => _Bubble(message: _messages[i]),
            ),
          ),
          if (_busy)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 6),
              child: SpinKitThreeBounce(color: Colors.indigo, size: 18),
            ),
          SafeArea(
            top: false,
            child: Container(
              padding:
                  const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              decoration: BoxDecoration(
                color: Colors.white,
                border: Border(top: BorderSide(color: Colors.grey.shade200)),
              ),
              child: Row(
                children: [
                  IconButton(
                    icon: Icon(_listening ? Icons.mic : Icons.mic_none,
                        color: _listening ? Colors.red : null),
                    onPressed: _busy ? null : _toggleListening,
                    tooltip: _listening ? 'Stop listening' : 'Speak',
                  ),
                  Expanded(
                    child: TextField(
                      controller: _inputCtrl,
                      minLines: 1,
                      maxLines: 4,
                      textInputAction: TextInputAction.send,
                      onSubmitted: _busy ? null : _handleUserInput,
                      decoration: const InputDecoration(
                        hintText: 'Ask HomeBot...',
                        border: OutlineInputBorder(
                          borderRadius:
                              BorderRadius.all(Radius.circular(20)),
                        ),
                        contentPadding: EdgeInsets.symmetric(
                            horizontal: 14, vertical: 10),
                      ),
                    ),
                  ),
                  const SizedBox(width: 6),
                  IconButton(
                    icon: const Icon(Icons.send),
                    color: Colors.indigo,
                    onPressed: _busy ? null : () {
                      _handleUserInput(_inputCtrl.text);
                    },
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _Bubble extends StatelessWidget {
  final ChatMessage message;
  const _Bubble({required this.message});

  @override
  Widget build(BuildContext context) {
    final isUser = message.fromUser;
    final bg = isUser ? Colors.indigo : Colors.grey.shade100;
    final fg = isUser ? Colors.white : Colors.black87;

    return Container(
      margin: const EdgeInsets.symmetric(vertical: 4),
      alignment: isUser ? Alignment.centerRight : Alignment.centerLeft,
      child: Container(
        constraints:
            BoxConstraints(maxWidth: MediaQuery.of(context).size.width * 0.78),
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
        decoration: BoxDecoration(
          color: bg,
          borderRadius: BorderRadius.circular(16),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(message.text, style: TextStyle(color: fg)),
            const SizedBox(height: 2),
            Text(
              DateFormat('HH:mm').format(message.at),
              style: TextStyle(
                fontSize: 10,
                color: isUser ? Colors.white70 : Colors.black45,
              ),
            ),
          ],
        ),
      ),
    );
  }
}