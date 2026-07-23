/// ============================================================================
/// gemini_service.dart
/// Sends the user's voice/text command to Google Gemini with a strict
/// system prompt. The prompt forces Gemini to ALWAYS reply with a single JSON
/// object so the app can execute the resulting Firebase writes mechanically:
///
///   {
///     "reply":     "I've turned on the light for you.",
///     "db_update": { "devices/light": true }
///   }
///
/// `db_update` uses the full RTDB path as the key (e.g. "devices/fan_speed").
/// Empty `{}` means "no device action required" (e.g. status queries).
/// ============================================================================

import 'dart:convert';

import 'package:google_generative_ai/google_generative_ai.dart';

import '../models/device_state.dart';

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

class GeminiService {
  GeminiService({required this.apiKey, GenerativeModel? model}) {
    _model = model ??
        GenerativeModel(
          model: 'gemini-1.5-flash',
          apiKey: apiKey,
          systemInstruction: Content.system(systemPrompt),
          generationConfig: GenerationConfig(
            // Force JSON-only output so we can parse safely.
            responseMimeType: 'application/json',
            temperature: 0.2,
          ),
        );
  }

  final String apiKey;
  late final GenerativeModel _model;

  // ---------------------------------------------------------------------
  // SYSTEM PROMPT
  //
  // The whole behaviour of the assistant is encoded here. We tell Gemini:
  //   1) the exact schema it must produce,
  //   2) the full menu of valid RTDB keys so it never invents paths,
  //   3) the JSON-only constraint (also reinforced by responseMimeType).
  //
  // Tweak the "personality" or "examples" sections without changing the
  // schema and the rest of the app keeps working.
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
      ..writeln('  devices/fan_speed         : integer  (0..4, 0 = off)')
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
  final List<Content> _history = [];

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
    _history.add(Content.text('CURRENT HOME STATE (read-only context):\n$summary'));
  }

  /// Sends [userMessage] and returns a parsed [GeminiAction].
  /// Throws if the response is not valid JSON.
  Future<GeminiAction> sendMessage(String userMessage) async {
    final userPart = Content.text(userMessage);
    _history.add(userPart);

    final response = await _model.generateContent(_history);
    final raw = response.text ?? '{}';

    return _parse(raw);
  }

  GeminiAction _parse(String raw) {
    // Gemini occasionally wraps the JSON in ```json fences - strip them.
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
    } catch (_) {
      // Fallback: model ignored instructions. Treat the whole reply as text.
      final text = raw.trim().isEmpty ? "I didn't catch that." : raw.trim();
      return GeminiAction(reply: text, dbUpdate: const {});
    }

    final reply = (json['reply'] as String?)?.trim() ?? 'Done.';
    final rawUpdate = (json['db_update'] as Map?) ?? const {};
    final dbUpdate = <String, dynamic>{};

    rawUpdate.forEach((key, value) {
      final k = key.toString();
      // Coerce values to the type Firebase expects for each known path.
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
          dbUpdate[k] = (k == 'devices/fan_speed') ? n.clamp(0, 4) : n;
          break;
        default:
          // Unknown path - silently drop to avoid corrupting the DB.
          break;
      }
    });

    // Keep the model's natural-language reply in history (without the JSON).
    _history.add(Content.text('Assistant said: $reply'));

    return GeminiAction(reply: reply, dbUpdate: dbUpdate);
  }
}