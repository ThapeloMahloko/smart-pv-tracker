import 'dart:convert';
import 'package:http/http.dart' as http;

class TelemetryData {
  final double temperature;
  final double humidity;
  final double lux;
  final int pan;
  final int tilt;
  final String currentMode;

  const TelemetryData({
    required this.temperature,
    required this.humidity,
    required this.lux,
    required this.pan,
    required this.tilt,
    required this.currentMode,
  });

  factory TelemetryData.fromJson(Map<String, dynamic> json) {
    return TelemetryData(
      temperature: (json['temperature'] as num?)?.toDouble() ?? 0.0,
      humidity: (json['humidity'] as num?)?.toDouble() ?? 0.0,
      lux: (json['lux'] as num?)?.toDouble() ?? 0.0,
      pan: (json['servo_pan'] ?? json['pan'] ?? 90) as int,
      tilt: (json['servo_tilt'] ?? json['tilt'] ?? 90) as int,
      currentMode: (json['current_mode'] as String?) ?? 'AUTO',
    );
  }
}

class ChatResult {
  final String reply;
  final TelemetryData? telemetry;

  const ChatResult({required this.reply, this.telemetry});
}

class SolarApiService {
  String baseUrl;

  SolarApiService({
    this.baseUrl = const String.fromEnvironment(
      'API_BASE_URL',
      defaultValue: 'http://localhost:8000',
    ),
  });

  Future<TelemetryData?> fetchLatestTelemetry() async {
    final uri = Uri.parse('$baseUrl/telemetry/latest');
    final response = await http.get(uri).timeout(const Duration(seconds: 5));
    if (response.statusCode == 200 && response.body != 'null') {
      final decoded = jsonDecode(response.body);
      if (decoded is Map<String, dynamic>) {
        return TelemetryData.fromJson(decoded);
      }
    }
    return null;
  }

  Future<ChatResult> sendChatQuery(String query) async {
    final uri = Uri.parse('$baseUrl/chat');
    final response = await http
        .post(
          uri,
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'query': query}),
        )
        .timeout(const Duration(seconds: 25));

    if (response.statusCode == 200) {
      final data = jsonDecode(response.body) as Map<String, dynamic>;
      final telemetryJson = data['latest_telemetry'] as Map<String, dynamic>?;
      return ChatResult(
        reply: data['reply'] as String? ?? '',
        telemetry: telemetryJson != null
            ? TelemetryData.fromJson(telemetryJson)
            : null,
      );
    } else {
      throw Exception('Server returned ${response.statusCode}: ${response.body}');
    }
  }

  Future<void> sendOverrideCommand(String action, {int step = 5}) async {
    final uri = Uri.parse('$baseUrl/override');
    final response = await http
        .post(
          uri,
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'action': action, 'step': step}),
        )
        .timeout(const Duration(seconds: 8));

    if (response.statusCode != 200) {
      throw Exception('Override failed (${response.statusCode}): ${response.body}');
    }
  }
}
