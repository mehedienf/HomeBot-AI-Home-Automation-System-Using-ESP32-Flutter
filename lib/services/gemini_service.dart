/// ============================================================================
/// gemini_service.dart
/// Unified LLM client supporting two providers:
///
///   * `LlmProvider.gemini`    -> google_generative_ai (replaces the original
///                                Gemini REST client; uses the `generativelanguage.googleapis.com`
///                                endpoint).
///   * `LlmProvider.openrouter` -> OpenAI-compatible chat-completions API
///                                at `https://openrouter.ai/api/v1`.
///
/// The chat screen shows a segmented selector so the user can pick which
/// upstream answers each turn without restart. Both providers share the same
/// system prompt + JSON parsing, so the rest of the app stays untouched.
///
/// The action shape is unchanged:
///   {
///     "reply":     "I've turned on the light for you.",
///     "db_update": { "devices/light": true }
///   }
/// ============================================================================
library;

import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:flutter/foundation.dart';

import '../models/device_state.dart';

/// Which upstream serves [GeminiService.sendMessage] calls.
enum LlmProvider {
  gemini,
  openrouter;

  String get label => switch (this) {
        LlmProvider.gemini => 'Gemini',
        LlmProvider.openrouter => 'OpenRouter',
      };
}

class GeminiAction {
  final String reply;
  final Map<String, dynamic> dbUpdate;

  const GeminiAction({required this.reply, required this.dbUpdate});

  bool get hasAction => dbUpdate.isNotEmpty;

  @override
  String toString() {
    return 'GeminiAction(reply: $reply, db_update: $dbUpdate)';
  }
}

/// Unified LLM client.
class GeminiService {
  GeminiService({
    required this.apiKey,
    LlmProvider? provider,
    String? baseUrl,
    String? model,
    http.Client? httpClient,
  })  : _provider = provider ?? LlmProvider.openrouter,
        _baseUrl = baseUrl ??
            ((provider ?? LlmProvider.openrouter) == LlmProvider.gemini
                ? 'https://generativelanguage.googleapis.com/v1beta'
                : 'https://openrouter.ai/api/v1'),
        _model = model ??
            ((provider ?? LlmProvider.openrouter) == LlmProvider.gemini
                ? 'gemini-flash-latest'
                : 'meta-llama/llama-3.3-70b-instruct:free'),
        _http = httpClient ?? http.Client();

  /// Currently active provider. Exposed read-only; use [setProvider] /
  /// [setApiKey] to swap at runtime from the chat screen.
  LlmProvider get provider => _provider;
  LlmProvider _provider;

  /// Base URL used for the active provider.
  String get baseUrl => _baseUrl;

  /// Model name used for the active provider.
  String get modelName => _model;

  /// Public, mutable API key so the selector can swap to a different
  /// provider without recreating the service.
  String apiKey;

  /// Optional override for Gemini key — populated by main.dart so the
  /// selector has something to fall back to when switching back from
  /// OpenRouter to Gemini.
  String? geminiApiKey;

  /// Optional override for OpenRouter key — populated by main.dart.
  String? openRouterApiKey;

  /// Model name to use when the active provider is Gemini. Populated by
  /// main.dart from `GEMINI_MODEL` (default `gemini-2.5-flash`).
  String? geminiModel;

  /// Model name to use when the active provider is OpenRouter. Defaults
  /// to whatever the constructor was given (`_model`).
  String? openRouterModel;

  String _baseUrl;
  String _model;
  final http.Client _http;

  /// Swap the active provider. If a key override is set for the target
  /// provider it is used, otherwise the existing [apiKey] is kept (which
  /// is fine when both providers share the same key — e.g. OpenRouter).
  void setProvider(LlmProvider next) {
    if (next == _provider) return;
    _provider = next;
    if (next == LlmProvider.gemini) {
      if (geminiApiKey != null && geminiApiKey!.isNotEmpty) {
        apiKey = geminiApiKey!;
      }
      _baseUrl = 'https://generativelanguage.googleapis.com/v1beta';
      // Use the stashed Gemini model if main.dart populated it; otherwise
      // keep whatever model is currently active (the caller may have
      // pinned a specific OpenRouter model via --dart-define).
      // Default falls back to `gemini-flash-latest` alias, which Google
      // recommends for new API keys (pinned versions 404 with
      // "no longer available to new users").
      _model = geminiModel ?? 'gemini-flash-latest';
    } else if (next == LlmProvider.openrouter) {
      if (openRouterApiKey != null && openRouterApiKey!.isNotEmpty) {
        apiKey = openRouterApiKey!;
      }
      _baseUrl = 'https://openrouter.ai/api/v1';
      _model = openRouterModel ?? 'meta-llama/llama-3.3-70b-instruct:free';
    }
  }

  /// True when the active provider is Gemini AND we don't have a real key.
  bool get isGeminiMisconfigured =>
      _provider == LlmProvider.gemini &&
      (apiKey.isEmpty || apiKey == 'missing-gemini-api-key');

  // ---------------------------------------------------------------------
  // SYSTEM PROMPT (unchanged from the Gemini version so the behaviour is
  // identical regardless of which model answers).
  // ---------------------------------------------------------------------
  static String get systemPrompt {
    final b = StringBuffer()
      ..writeln('You are "HomeBot", a friendly voice/text smart-home assistant.')
      ..writeln('')
      ..writeln(
          'You can ONLY control the home by returning a strict JSON object - nothing else. '
          "The user's app parses your reply and writes the db_update keys straight into "
          'Firebase Realtime Database.')
      ..writeln('')
      ..writeln('OUTPUT FORMAT (return ONLY this JSON, no markdown, no commentary):')
      ..writeln('{')
      ..writeln('  "reply": "<short natural-language answer shown and spoken to the user>",')
      ..writeln('  "db_update": {')
      ..writeln('    "<firebase_path>": <value>,')
      ..writeln('    ...')
      ..writeln('  }')
      ..writeln('}')
      ..writeln('')
      ..writeln('ALLOWED FIREBASE PATHS AND VALUE TYPES:')
      ..writeln('  devices/light             : boolean  (true = ON, false = OFF)')
      ..writeln('  devices/fan               : boolean')
      ..writeln('  devices/fan_speed         : integer  (0..255, 0 = off)')
      ..writeln('  devices/pump              : boolean')
      ..writeln('  devices/humidifier        : boolean')
      ..writeln('  automation/auto_fan       : boolean')
      ..writeln('  automation/auto_humidifier: boolean')
      ..writeln('  automation/auto_pump      : boolean')
      ..writeln('  timers/light_off_time     : integer  (epoch millis, 0 = clear)')
      ..writeln('  timers/fan_off_time       : integer')
      ..writeln('  timers/pump_off_time      : integer')
      ..writeln('  timers/humidifier_off_time: integer')
      ..writeln('')
      ..writeln('For sensor / status questions (temperature, humidity, water level, smoke):')
      ..writeln('  - Return db_update as {} (no device change required).')
      ..writeln('  - The app will inject the live sensor values into a follow-up turn and')
      ..writeln('    you will then produce the natural-language answer.')
      ..writeln('')
      ..writeln('BEHAVIOUR RULES:')
      ..writeln(
          '  - Never invent paths. If the request cannot be expressed with the allowed')
      ..writeln('    paths, set db_update to {} and explain in reply.')
      ..writeln(
          '  - Map synonyms: "lamp","bulb" -> devices/light; "AC","cooler" -> devices/fan;')
      ..writeln(
          '    "motor","water motor" -> devices/pump; "mister" -> devices/humidifier.')
      ..writeln(
          '  - "all off" / "shut everything down" -> set every devices/* boolean to false.')
      ..writeln('  - Speed words: off=0, low=1, medium=2, high=3, max/turbo=4.')
      ..writeln(
          '  - For timer commands ("turn off in 10 minutes"), you MAY compute the epoch')
      ..writeln(
          '    millis yourself and put it under timers/<device>_off_time. If you cannot')
      ..writeln(
          '    compute it confidently, set db_update to {} and ask for clarification.')
      ..writeln(
          '  - If unsure, prefer a safe answer: db_update = {} and a clarifying reply.')
      ..writeln('  - Keep reply under 25 words. Be warm and concise.');
    return b.toString();
  }

  // ---------------------------------------------------------------------
  // Chat history support (so the assistant remembers the last few turns).
  // ---------------------------------------------------------------------
  final List<Map<String, String>> _history = [];

  /// Optional pre-seed (e.g. inject latest sensor values) - call before
  /// [sendMessage] when you want the model to know the current state.
  void primeContext({
    required SensorState sensors,
    required DeviceState devices,
    required AutomationState automation,
  }) {
    _history.clear();
    final summary = jsonEncode({
      'sensors': sensors.toMap(),
      'devices': devices.toMap(),
      'automation': automation.toMap(),
      'note':
          'These are the LIVE values from Firebase right now. Use them to answer status questions and to avoid redundant writes.',
    });
    _history.add({
      'role': 'user',
      'content': 'CURRENT HOME STATE (read-only context):\n$summary',
    });
    _history.add({
      'role': 'assistant',
      'content': '{"reply":"Got it, I have the latest state.","db_update":{}}',
    });
  }

  /// Sends [userMessage] and returns a parsed [GeminiAction].
  /// Throws on network / non-2xx / non-parseable responses.
  Future<GeminiAction> sendMessage(String userMessage) async {
    _history.add({'role': 'user', 'content': userMessage});

    final raw = switch (provider) {
      LlmProvider.openrouter => await _sendOpenRouter(),
      LlmProvider.gemini => await _sendGemini(),
    };

    final action = _parse(raw);

    // Mirror the parsed action back into history so the model "remembers".
    _history.add({
      'role': 'assistant',
      'content': jsonEncode({
        'reply': action.reply,
        'db_update': action.dbUpdate,
      }),
    });

    return action;
  }

  /// OpenAI-compatible chat-completions call. Works for OpenRouter and any
  /// provider that speaks the same protocol.
  Future<String> _sendOpenRouter() async {
    final body = jsonEncode({
      'model': _model,
      'temperature': 0.2,
      'messages': [
        {'role': 'system', 'content': systemPrompt},
        ..._history,
      ],
    });

    final uri = Uri.parse('$_baseUrl/chat/completions');
    final response = await _http.post(
      uri,
      headers: {
        'Authorization': 'Bearer $apiKey',
        'Content-Type': 'application/json',
        // OpenRouter recommends these two for ranking on the leaderboard.
        'HTTP-Referer': 'https://homebot.local',
        'X-Title': 'HomeBot',
      },
      body: body,
    );

    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw Exception(
          'OpenRouter HTTP ${response.statusCode}: ${_truncate(response.body, 300)}');
    }

    final json = jsonDecode(response.body) as Map<String, dynamic>;
    final choices = json['choices'] as List?;
    if (choices == null || choices.isEmpty) {
      throw Exception(
          'OpenRouter returned no choices: ${_truncate(response.body, 200)}');
    }
    final message =
        (choices.first as Map<String, dynamic>)['message'] as Map<String, dynamic>?;
    return (message?['content'] as String?)?.trim() ?? '';
  }

  /// Google Gemini REST call (`generativelanguage.googleapis.com`). We use
  /// `?key=...` auth (no SDK) so the app stays free of the
  /// `google_generative_ai` package and works on a single `http.Client`.
  Future<String> _sendGemini() async {
    // Convert our internal history (OpenAI-style) into Gemini's
    // `contents` shape: each turn becomes one Content with a single `text`
    // part. The system prompt is sent as `systemInstruction`.
    final contents = <Map<String, dynamic>>[];
    for (final msg in _history) {
      contents.add({
        'role': msg['role'] == 'assistant' ? 'model' : 'user',
        'parts': [
          {'text': msg['content']},
        ],
      });
    }

    final body = jsonEncode({
      'systemInstruction': {
        'parts': [
          {'text': systemPrompt},
        ],
      },
      'contents': contents,
      'generationConfig': {
        'temperature': 0.2,
        'responseMimeType': 'application/json',
      },
    });

    final uri = Uri.parse('$_baseUrl/models/$_model:generateContent?key=$apiKey');
    final response = await _http.post(
      uri,
      headers: {'Content-Type': 'application/json'},
      body: body,
    );

    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw Exception(
          'Gemini HTTP ${response.statusCode}: ${_truncate(response.body, 300)}');
    }

    final json = jsonDecode(response.body) as Map<String, dynamic>;
    final candidates = json['candidates'] as List?;
    if (candidates == null || candidates.isEmpty) {
      throw Exception(
          'Gemini returned no candidates: ${_truncate(response.body, 200)}');
    }
    final content =
        (candidates.first as Map<String, dynamic>)['content'] as Map<String, dynamic>?;
    final parts = content?['parts'] as List?;
    if (parts == null || parts.isEmpty) return '';
    return (parts.first as Map<String, dynamic>)['text']?.toString().trim() ?? '';
  }

  static String _truncate(String s, int n) =>
      s.length <= n ? s : '${s.substring(0, n)}...';

  GeminiAction _parse(String raw) {
    // Models occasionally wrap the JSON in ```json fences - strip them.
    var cleaned = raw.trim();
    if (cleaned.startsWith('```')) {
      cleaned = cleaned
          .replaceAll(RegExp(r'^```(?:json)?', multiLine: true), '')
          .replaceAll('```', '')
          .trim();
    }

    Map<String, dynamic> json;
    try {
      json = jsonDecode(cleaned) as Map<String, dynamic>;
    } catch (e) {
      debugPrint('OpenRouter response was not JSON: $raw');
      // Fallback: model ignored instructions. Treat the whole reply as text.
      final text = raw.trim().isEmpty ? "I didn't catch that." : raw.trim();
      return GeminiAction(reply: text, dbUpdate: const {});
    }

    final reply = (json['reply'] as String?)?.trim() ?? 'Done.';
    final rawUpdate = (json['db_update'] as Map?) ?? const {};
    final dbUpdate = <String, dynamic>{};

    rawUpdate.forEach((key, value) {
      final k = key.toString();
      switch (k) {
        case 'devices/light':
        case 'devices/fan':
        case 'devices/pump':
        case 'devices/humidifier':
        case 'automation/auto_fan':
        case 'automation/auto_humidifier':
        case 'automation/auto_pump':
          dbUpdate[k] = value == true || value.toString().toLowerCase() == 'true';
          break;
        case 'devices/fan_speed':
        case 'timers/light_off_time':
        case 'timers/fan_off_time':
        case 'timers/pump_off_time':
        case 'timers/humidifier_off_time':
          final n = (value is num) ? value.toInt() : int.tryParse(value.toString()) ?? 0;
          dbUpdate[k] = (k == 'devices/fan_speed') ? n.clamp(0, 255) : n;
          break;
        default:
          // Unknown path - silently drop to avoid corrupting the DB.
          break;
      }
    });

    return GeminiAction(reply: reply, dbUpdate: dbUpdate);
  }

  void dispose() {
    _http.close();
  }
}