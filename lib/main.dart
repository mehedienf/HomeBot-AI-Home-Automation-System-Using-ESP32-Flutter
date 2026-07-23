// ============================================================================
// main.dart
// Entry point. Three responsibilities:
//   1) WidgetsFlutterBinding.ensureInitialized() – needed before any plugin
//      call (Firebase, Speech).
//   2) Firebase.initializeApp – required before any RTDB access.
//   3) MultiProvider – exposes services + controllers to the widget tree
//      so any screen can `context.read<X>()` or `context.watch<X>()`.
// ============================================================================

import 'dart:async';

import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'package:flutter_dotenv/flutter_dotenv.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:provider/provider.dart';

import 'firebase_options.dart';
import 'screens/home_screen.dart';
import 'services/firebase_service.dart';
import 'services/gemini_service.dart';
import 'services/home_state_controller.dart';
import 'services/voice_service.dart';

// ---------------------------------------------------------------------------
// LLM API key lookup (OpenRouter, OpenAI-compatible).
//
// Resolution order:
//   1) `flutter run --dart-define=OPENROUTER_API_KEY=...` (CI / production)
//   2) `OPENROUTER_API_KEY=...` entry in the .env file    (local development)
//
// `.env` is listed under `flutter.assets:` in pubspec.yaml so it's bundled
// and read by `flutter_dotenv` at startup. The .env file itself is gitignored.
// `OPENROUTER_MODEL` is optional — defaults to `meta-llama/llama-3.3-70b-instruct:free`.
// ---------------------------------------------------------------------------
String _resolveOpenRouterApiKey() {
  const fromDefine = String.fromEnvironment('OPENROUTER_API_KEY');
  if (fromDefine.isNotEmpty) return fromDefine;

  final fromEnv = dotenv.env['OPENROUTER_API_KEY'];
  if (fromEnv != null && fromEnv.isNotEmpty) return fromEnv;

  throw StateError(
    'OPENROUTER_API_KEY is not set. Either create a .env file with '
    'OPENROUTER_API_KEY=... or run with --dart-define=OPENROUTER_API_KEY=...',
  );
}

/// Reads the optional `OPENROUTER_MODEL` override. Falls back to the
/// default Llama 3.3 70B free model if not set.
String _resolveOpenRouterModel() {
  const fromDefine = String.fromEnvironment('OPENROUTER_MODEL');
  if (fromDefine.isNotEmpty) return fromDefine;

  final fromEnv = dotenv.env['OPENROUTER_MODEL'];
  if (fromEnv != null && fromEnv.isNotEmpty) return fromEnv;

  return 'meta-llama/llama-3.3-70b-instruct:free';
}

/// Reads the optional Gemini API key (Google AI Studio). Returns null when
/// the key isn't configured — the chat screen treats that as "Gemini is
/// unavailable" and disables that selector.
String? _resolveGeminiApiKey() {
  const fromDefine = String.fromEnvironment('GEMINI_API_KEY');
  if (fromDefine.isNotEmpty) return fromDefine;
  return dotenv.env['GEMINI_API_KEY'];
}

/// Reads the optional `GEMINI_MODEL` override for the Gemini provider.
/// Defaults to `gemini-flash-latest` (an alias that always points to the
/// current stable Flash release). This alias is required for newly-created
/// API keys — Google 404s on pinned versions like `gemini-2.5-flash` for
/// those keys with the message "no longer available to new users".
String _resolveGeminiModel() {
  const fromDefine = String.fromEnvironment('GEMINI_MODEL');
  if (fromDefine.isNotEmpty) return fromDefine;

  final fromEnv = dotenv.env['GEMINI_MODEL'];
  if (fromEnv != null && fromEnv.isNotEmpty) return fromEnv;

  return 'gemini-flash-latest';
}

Future<void> main() async {
  // Catch every startup exception and convert it into a visible error screen,
  // so we never silently sit on a black screen again.
  runZonedGuarded<Future<void>>(
    () async {
      WidgetsFlutterBinding.ensureInitialized();

      FlutterError.onError = (FlutterErrorDetails details) {
        FlutterError.dumpErrorToConsole(details);
      };

      // Load secrets from .env before anything that might need them.
      try {
        await dotenv.load(fileName: '.env');
      } catch (e) {
        debugPrint('dotenv.load failed: $e');
        runApp(_StartupErrorApp(message: 'Failed to load .env:\n$e'));
        return;
      }

      // Request mic permission up-front so the chat button works the first
      // time. Failure here should never block startup.
      try {
        await Permission.microphone.request();
      } catch (e) {
        debugPrint('microphone permission request failed: $e');
      }

      // ---- Firebase init ---------------------------------------------------
      // Pulls apiKey/appId/etc. from .env via `firebase_options.dart`. Any
      // throw here (missing key, malformed URL, etc.) is surfaced to the UI
      // instead of leaving a black screen.
      //
      // `main()` is re-run on every hot-restart (`R`), and on Android the
      // native `firebase_core` plugin survives the Dart-isolate restart
      // with the previous [DEFAULT] app still registered — so calling
      // `Firebase.initializeApp` again throws `[core/duplicate-app]`. We
      // treat that one specific code as a benign no-op; every other error
      // is surfaced.
      try {
        await Firebase.initializeApp(
          options: DefaultFirebaseOptions.currentPlatform,
        );
      } on FirebaseException catch (e) {
        if (e.code != 'duplicate-app') {
          debugPrint('Firebase.initializeApp failed: $e');
          runApp(_StartupErrorApp(message: 'Firebase init failed:\n$e'));
          return;
        }
        // duplicate-app → [DEFAULT] is already registered (hot restart).
      } catch (e, st) {
        debugPrint('Firebase.initializeApp failed: $e\n$st');
        runApp(_StartupErrorApp(message: 'Firebase init failed:\n$e'));
        return;
      }

      // ---- Force the RTDB host on every platform ---------------------------
      // `Firebase.initializeApp(options: ...)` passes `databaseURL` through
      // to the native `firebase_database` plugin, but on Android we've seen
      // the plugin ignore it and try the global host, which the server then
      // rejects with: "Database lives in a different region. Please change
      // your database URL to https://...asia-southeast1.firebasedatabase.app"
      // Setting the URL explicitly here makes the plugin use the regional
      // host on every reconnect. No-op if it's already correct.
      try {
        FirebaseDatabase.instance.databaseURL =
            'https://homebot-home-automation-default-rtdb.asia-southeast1.firebasedatabase.app';
      } catch (e) {
        debugPrint('Failed to set RTDB databaseURL: $e');
      }

      // ---- Anonymous auth -------------------------------------------------
      // RTDB rules typically require `auth != null`. We sign in anonymously
      // so the Flutter app can read/write even before the user logs in. If
      // rules are open (`.read: true`) this is a no-op; if they require a
      // custom token, the error here is non-fatal — the orange RTDB error
      // banner will surface it to the user.
      try {
        final auth = FirebaseAuth.instance;
        if (auth.currentUser == null) {
          await auth.signInAnonymously();
        }
      } on FirebaseException catch (e) {
        if (e.code != 'user-disabled') {
          debugPrint('Anonymous sign-in failed: $e');
        }
      } catch (e) {
        debugPrint('Anonymous sign-in threw: $e');
      }

      // ---- Build the service graph ----------------------------------------
      final firebaseService = FirebaseService();
      final homeController = HomeStateController(firebaseService);
      final voiceService = VoiceService();
      try {
        await voiceService.init();
      } catch (e) {
        debugPrint('voiceService.init failed: $e');
      }

      runApp(
        MultiProvider(
          providers: [
            Provider<FirebaseService>.value(value: firebaseService),
            Provider<GeminiService>(
              create: (_) {
                // One mutable service shared by both providers — the chat
                // screen swaps via `setProvider(...)`. We stash both keys
                // so the selector can flip either way without re-reading
                // the environment.
                final orKey = _resolveOpenRouterApiKey();
                final geminiKey = _resolveGeminiApiKey();
                final orModel = _resolveOpenRouterModel();
                final svc = GeminiService(
                  apiKey: orKey,
                  provider: LlmProvider.openrouter,
                  model: orModel,
                );
                svc.openRouterApiKey = orKey;
                svc.openRouterModel = orModel;
                svc.geminiApiKey =
                    (geminiKey != null && geminiKey.isNotEmpty) ? geminiKey : null;
                // Stash the resolved Gemini model so `setProvider()` knows
                // which model name to swap in when the user picks Gemini.
                svc.geminiModel = _resolveGeminiModel();
                return svc;
              },
            ),
            Provider<VoiceService>.value(value: voiceService),
            ChangeNotifierProvider<HomeStateController>.value(
              value: homeController,
            ),
          ],
          child: const HomeBotApp(),
        ),
      );
    },
    (error, stack) {
      debugPrint('Uncaught zoned error: $error\n$stack');
      runApp(_StartupErrorApp(message: 'Uncaught error:\n$error'));
    },
  );
}

/// Last-resort screen rendered when something throws during startup. Shows
/// the actual error in red so we never see a black screen again.
class _StartupErrorApp extends StatelessWidget {
  const _StartupErrorApp({required this.message});
  final String message;
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        backgroundColor: const Color(0xFFFFEBEE),
        body: SafeArea(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'HomeBot failed to start',
                    style: TextStyle(
                      color: Color(0xFFB71C1C),
                      fontSize: 22,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'Copy the message below and paste it back to the assistant:',
                    style: TextStyle(fontSize: 14),
                  ),
                  const SizedBox(height: 16),
                  SelectableText(
                    message,
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 12,
                      color: Color(0xFF4A0E0E),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class HomeBotApp extends StatelessWidget {
  const HomeBotApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'HomeBot',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.indigo,
          brightness: Brightness.light,
        ),
        scaffoldBackgroundColor: const Color(0xFFF4F6FA),
        appBarTheme: const AppBarTheme(
          backgroundColor: Colors.transparent,
          foregroundColor: Colors.black87,
          elevation: 0,
        ),
      ),
      home: const HomeScreen(),
    );
  }
}
