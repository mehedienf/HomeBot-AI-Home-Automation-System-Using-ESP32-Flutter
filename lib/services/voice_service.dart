/// ============================================================================
/// voice_service.dart
/// Wraps the speech_to_text and flutter_tts plugins so the rest of the app
/// doesn't have to deal with their async lifecycles directly.
///
/// Usage:
///
///   final voice = VoiceService();
///   await voice.init();
///   await voice.listen((text) => print('User said: $text'));
///   await voice.speak('Lights turned on.');
///   await voice.stopListening();
///
/// Errors are swallowed + logged; we never want a mic failure to crash the
/// chat experience.
/// ============================================================================
library;

import 'package:flutter/foundation.dart';
import 'package:flutter_tts/flutter_tts.dart';
import 'package:speech_to_text/speech_recognition_result.dart';
import 'package:speech_to_text/speech_to_text.dart';

class VoiceService {
  final SpeechToText _stt = SpeechToText();
  final FlutterTts _tts = FlutterTts();

  bool _sttReady = false;
  bool _ttsReady = false;
  bool get isListening => _stt.isListening;

  /// Initialise both engines. Safe to call multiple times.
  Future<void> init() async {
    if (!_sttReady) {
      _sttReady = await _stt.initialize(
        onError: (e) => debugPrint('STT error: ${e.errorMsg}'),
        onStatus: (s) => debugPrint('STT status: $s'),
      );
    }
    if (!_ttsReady) {
      await _tts.setLanguage('en-US');
      await _tts.setSpeechRate(0.5);
      await _tts.setVolume(1.0);
      await _tts.setPitch(1.0);
      _ttsReady = true;
    }
  }

  bool get isSttAvailable => _sttReady;

  /// Start listening and call [onFinalResult] with the recognized text once
  /// the user pauses / stops talking. Intermediate [onPartial] is optional.
  Future<void> listen({
    required ValueChanged<String> onFinalResult,
    ValueChanged<String>? onPartial,
  }) async {
    await init();
    if (!_sttReady) {
      onFinalResult('');
      return;
    }
    await _stt.listen(
      onResult: (SpeechRecognitionResult result) {
        final transcript = result.recognizedWords;
        if (result.finalResult) {
          onFinalResult(transcript);
        } else if (onPartial != null) {
          onPartial(transcript);
        }
      },
      listenFor: const Duration(seconds: 30),
      pauseFor: const Duration(seconds: 3),
      listenOptions: SpeechListenOptions(partialResults: true),
    );
  }

  Future<void> stopListening() async {
    if (_stt.isListening) {
      await _stt.stop();
    }
  }

  Future<void> speak(String text) async {
    await init();
    if (text.trim().isEmpty) return;
    await _tts.stop();
    await _tts.speak(text);
  }

  Future<void> stopSpeaking() => _tts.stop();

  Future<void> dispose() async {
    await _stt.stop();
    await _tts.stop();
  }
}
