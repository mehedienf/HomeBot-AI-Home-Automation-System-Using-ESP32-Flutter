// ============================================================================
// main.dart
// Entry point. Three responsibilities:
//   1) WidgetsFlutterBinding.ensureInitialized() – needed before any plugin
//      call (Firebase, Speech).
//   2) Firebase.initializeApp – required before any RTDB access.
//   3) MultiProvider – exposes services + controllers to the widget tree
//      so any screen can `context.read<X>()` or `context.watch<X>()`.
// ============================================================================

import 'package:firebase_core/firebase_core.dart';
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
// Gemini API key lookup.
//
// Resolution order:
//   1) `flutter run --dart-define=GEMINI_API_KEY=...`   (CI / production)
//   2) `GEMINI_API_KEY=...` entry in the .env file      (local development)
//
// `.env` is listed under `flutter.assets:` in pubspec.yaml so it's bundled
// and read by `flutter_dotenv` at startup. The .env file itself is gitignored.
// ---------------------------------------------------------------------------
String _resolveGeminiApiKey() {
  const fromDefine = String.fromEnvironment('GEMINI_API_KEY');
  if (fromDefine.isNotEmpty) return fromDefine;

  final fromEnv = dotenv.env['GEMINI_API_KEY'];
  if (fromEnv != null && fromEnv.isNotEmpty) return fromEnv;

  throw StateError(
    'GEMINI_API_KEY is not set. Either create a .env file with '
    'GEMINI_API_KEY=... or run with --dart-define=GEMINI_API_KEY=...',
  );
}

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Load secrets from .env before anything that might need them.
  await dotenv.load(fileName: '.env');

  // Request mic permission up-front so the chat button works the first time.
  await Permission.microphone.request();

  // ---- Firebase init -----------------------------------------------------
  // Pulls the apiKey/appId/etc. from .env via `firebase_options.dart`
  // (which reads `flutter_dotenv`). Override individual values from the
  // command line with --dart-define, e.g.
  //   flutter run --dart-define=FIREBASE_PROJECT_ID=my-project
  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );

  // ---- Build the service graph -----------------------------------------
  final firebaseService = FirebaseService();
  final homeController = HomeStateController(firebaseService);
  final voiceService = VoiceService();
  await voiceService.init();

  runApp(
    MultiProvider(
      providers: [
        Provider<FirebaseService>.value(value: firebaseService),
        Provider<GeminiService>(
          create: (_) {
            return GeminiService(apiKey: _resolveGeminiApiKey());
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
