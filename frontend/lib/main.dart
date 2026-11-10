import 'dart:async';
import 'package:flutter/material.dart';
import 'services/api_service.dart';

void main() {
  runApp(const CutSolarTrackerApp());
}

class CutSolarTrackerApp extends StatelessWidget {
  const CutSolarTrackerApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'CUT Smart Solar Tracker',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF0F172A),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFFF59E0B), // Solar Amber
          secondary: Color(0xFF38BDF8), // Sky Blue
          surface: Color(0xFF1E293B),
        ),
        // Enforce underlying line indicators for all text fields (no boxed borders)
        inputDecorationTheme: const InputDecorationTheme(
          filled: false,
          border: UnderlineInputBorder(
            borderSide: BorderSide(color: Color(0xFF64748B), width: 1.5),
          ),
          enabledBorder: UnderlineInputBorder(
            borderSide: BorderSide(color: Color(0xFF64748B), width: 1.5),
          ),
          focusedBorder: UnderlineInputBorder(
            borderSide: BorderSide(color: Color(0xFFF59E0B), width: 2.5),
          ),
          errorBorder: UnderlineInputBorder(
            borderSide: BorderSide(color: Color(0xFFEF4444), width: 1.5),
          ),
          contentPadding: EdgeInsets.symmetric(vertical: 12.0, horizontal: 4.0),
        ),
        useMaterial3: true,
      ),
      home: const SolarHomeShell(),
    );
  }
}

class SolarHomeShell extends StatefulWidget {
  const SolarHomeShell({super.key});

  @override
  State<SolarHomeShell> createState() => _SolarHomeShellState();
}

class _SolarHomeShellState extends State<SolarHomeShell> {
  final SolarApiService _apiService = SolarApiService();

  void _showEndpointDialog() {
    final controller = TextEditingController(text: _apiService.baseUrl);
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('FastAPI Server URL'),
        content: TextField(
          controller: controller,
          decoration: const InputDecoration(
            labelText: 'Backend Base URL',
            hintText: 'http://192.168.1.100:8000',
            border: UnderlineInputBorder(),
            enabledBorder: UnderlineInputBorder(
              borderSide: BorderSide(color: Color(0xFF64748B)),
            ),
            focusedBorder: UnderlineInputBorder(
              borderSide: BorderSide(color: Color(0xFFF59E0B), width: 2),
            ),
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              setState(() {
                _apiService.baseUrl = controller.text.trim();
              });
              Navigator.pop(ctx);
            },
            child: const Text('Save'),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    // Constrain to mobile viewport width even when opened on wider browsers
    return Scaffold(
      body: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 480),
          child: DefaultTabController(
            length: 2,
            child: Scaffold(
              appBar: AppBar(
                backgroundColor: const Color(0xFF1E293B),
                elevation: 0,
                title: const Row(
                  children: [
                    Icon(Icons.wb_sunny_rounded, color: Color(0xFFF59E0B)),
                    SizedBox(width: 10),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'CUT Solar Tracker',
                            style: TextStyle(
                              fontSize: 17,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                          Text(
                            'Keyestudio KS0530 • Sports Ground PV Guide',
                            style: TextStyle(
                              fontSize: 11,
                              color: Color(0xFF94A3B8),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                actions: [
                  IconButton(
                    tooltip: 'Configure API Endpoint',
                    icon: const Icon(Icons.settings_ethernet, size: 20),
                    onPressed: _showEndpointDialog,
                  ),
                ],
                bottom: const TabBar(
                  indicatorColor: Color(0xFFF59E0B),
                  labelColor: Color(0xFFF59E0B),
                  unselectedLabelColor: Color(0xFF94A3B8),
                  tabs: [
                    Tab(
                      icon: Icon(Icons.auto_awesome_outlined, size: 20),
                      text: 'AI Guide',
                    ),
                    Tab(
                      icon: Icon(Icons.gamepad_outlined, size: 20),
                      text: 'Manual Override',
                    ),
                  ],
                ),
              ),
              body: TabBarView(
                children: [
                  AiGuideTab(apiService: _apiService),
                  ManualOverrideTab(apiService: _apiService),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class ChatMessage {
  final String sender;
  final String text;
  final bool isUser;

  const ChatMessage({
    required this.sender,
    required this.text,
    required this.isUser,
  });
}

class AiGuideTab extends StatefulWidget {
  final SolarApiService apiService;

  const AiGuideTab({super.key, required this.apiService});

  @override
  State<AiGuideTab> createState() => _AiGuideTabState();
}

class _AiGuideTabState extends State<AiGuideTab>
    with AutomaticKeepAliveClientMixin {
  final TextEditingController _queryController = TextEditingController();
  final ScrollController _scrollController = ScrollController();
  bool _isLoading = false;

  final List<ChatMessage> _messages = [
    const ChatMessage(
      sender: 'CUT Solar AI Guide',
      text:
          'Welcome to the Central University of Technology (CUT) Smart Solar Project! '
          'Ask me anything about this dual-axis Keyestudio KS0530 tracker, its live '
          'sensor telemetry, or how it models the full-scale solar panels on the '
          'CUT university sports ground.',
      isUser: false,
    ),
  ];

  @override
  bool get wantKeepAlive => true;

  Future<void> _sendMessage([String? presetQuery]) async {
    final text = (presetQuery ?? _queryController.text).trim();
    if (text.isEmpty || _isLoading) return;

    if (presetQuery == null) {
      _queryController.clear();
    }

    setState(() {
      _messages.add(ChatMessage(sender: 'You', text: text, isUser: true));
      _isLoading = true;
    });
    _scrollToBottom();

    try {
      final result = await widget.apiService.sendChatQuery(text);
      if (!mounted) return;
      setState(() {
        _messages.add(
          ChatMessage(
            sender: 'CUT Solar AI Guide',
            text: result.reply,
            isUser: false,
          ),
        );
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _messages.add(
          ChatMessage(
            sender: 'System',
            text: 'Unable to reach the FastAPI /chat endpoint: $e',
            isUser: false,
          ),
        );
      });
    } finally {
      if (mounted) {
        setState(() {
          _isLoading = false;
        });
        _scrollToBottom();
      }
    }
  }

  void _scrollToBottom() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scrollController.hasClients) {
        _scrollController.animateTo(
          _scrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 250),
          curve: Curves.easeOut,
        );
      }
    });
  }

  @override
  void dispose() {
    _queryController.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    super.build(context);
    return Column(
      children: [
        // Quick suggestion chips for mobile visitors scanning the QR code
        SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          child: Row(
            children: [
              _PromptChip(
                label: 'CUT Sports Ground Solar',
                onTap: () => _sendMessage(
                  'How does this tracker relate to the real solar panels on the CUT sports ground?',
                ),
              ),
              const SizedBox(width: 8),
              _PromptChip(
                label: 'Current Telemetry',
                onTap: () => _sendMessage(
                  'What are the current light intensity, temperature, and panel angles?',
                ),
              ),
              const SizedBox(width: 8),
              _PromptChip(
                label: 'How LDR Tracking Works',
                onTap: () => _sendMessage(
                  'How do the 4 LDR sensors track the sun on this KS0530 kit?',
                ),
              ),
            ],
          ),
        ),
        const Divider(height: 1, color: Color(0xFF334155)),
        Expanded(
          child: ListView.builder(
            controller: _scrollController,
            padding: const EdgeInsets.all(14),
            itemCount: _messages.length,
            itemBuilder: (context, index) {
              final msg = _messages[index];
              return Align(
                alignment:
                    msg.isUser ? Alignment.centerRight : Alignment.centerLeft,
                child: Container(
                  margin: const EdgeInsets.symmetric(vertical: 6),
                  padding: const EdgeInsets.symmetric(
                    horizontal: 14,
                    vertical: 11,
                  ),
                  constraints: const BoxConstraints(maxWidth: 340),
                  decoration: BoxDecoration(
                    color: msg.isUser
                        ? const Color(0xFFF59E0B).withValues(alpha: 0.2)
                        : const Color(0xFF1E293B),
                    borderRadius: BorderRadius.circular(14),
                    border: Border.all(
                      color: msg.isUser
                          ? const Color(0xFFF59E0B).withValues(alpha: 0.5)
                          : const Color(0xFF334155),
                    ),
                  ),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        msg.sender,
                        style: TextStyle(
                          fontSize: 11,
                          fontWeight: FontWeight.w600,
                          color: msg.isUser
                              ? const Color(0xFFFBBF24)
                              : const Color(0xFF38BDF8),
                        ),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        msg.text,
                        style: const TextStyle(fontSize: 14, height: 1.4),
                      ),
                    ],
                  ),
                ),
              );
            },
          ),
        ),
        if (_isLoading)
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            child: LinearProgressIndicator(
              minHeight: 2,
              color: Color(0xFFF59E0B),
            ),
          ),
        Container(
          color: const Color(0xFF1E293B),
          padding: const EdgeInsets.fromLTRB(16, 6, 12, 16),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Expanded(
                // Explicitly uses underlying line indicator (UnderlineInputBorder)
                child: TextField(
                  controller: _queryController,
                  textInputAction: TextInputAction.send,
                  onSubmitted: (_) => _sendMessage(),
                  decoration: const InputDecoration(
                    hintText: 'Ask the CUT Solar AI Guide...',
                    hintStyle: TextStyle(color: Color(0xFF64748B)),
                    border: UnderlineInputBorder(
                      borderSide: BorderSide(color: Color(0xFF64748B), width: 1.5),
                    ),
                    enabledBorder: UnderlineInputBorder(
                      borderSide: BorderSide(color: Color(0xFF64748B), width: 1.5),
                    ),
                    focusedBorder: UnderlineInputBorder(
                      borderSide: BorderSide(color: Color(0xFFF59E0B), width: 2.5),
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 10),
              IconButton(
                onPressed: _isLoading ? null : () => _sendMessage(),
                icon: const Icon(Icons.send_rounded),
                color: const Color(0xFFF59E0B),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _PromptChip extends StatelessWidget {
  final String label;
  final VoidCallback onTap;

  const _PromptChip({required this.label, required this.onTap});

  @override
  Widget build(BuildContext context) {
    return ActionChip(
      label: Text(label, style: const TextStyle(fontSize: 12)),
      backgroundColor: const Color(0xFF1E293B),
      side: const BorderSide(color: Color(0xFF334155)),
      onPressed: onTap,
    );
  }
}

class ManualOverrideTab extends StatefulWidget {
  final SolarApiService apiService;

  const ManualOverrideTab({super.key, required this.apiService});

  @override
  State<ManualOverrideTab> createState() => _ManualOverrideTabState();
}

class _ManualOverrideTabState extends State<ManualOverrideTab> {
  TelemetryData _telemetry = const TelemetryData(
    temperature: 26.4,
    humidity: 42.0,
    lux: 840.0,
    pan: 90,
    tilt: 90,
    currentMode: 'AUTO',
  );

  Timer? _telemetryPollTimer;
  Timer? _overrideCountdownTimer;
  int _remainingOverrideSeconds = 0;
  bool _isSendingCommand = false;
  String _statusBanner = 'Tracking sun automatically via 4x LDRs';

  bool get _isManualActive => _remainingOverrideSeconds > 0;

  @override
  void initState() {
    super.initState();
    _pollTelemetry();
    _telemetryPollTimer = Timer.periodic(
      const Duration(seconds: 2),
      (_) => _pollTelemetry(),
    );
  }

  Future<void> _pollTelemetry() async {
    try {
      final latest = await widget.apiService.fetchLatestTelemetry();
      if (latest != null && mounted) {
        setState(() {
          _telemetry = latest;
        });
      }
    } catch (_) {
      // Keep displaying the last known telemetry snapshot if offline
    }
  }

  void _startOrReset60sWatchdog() {
    _overrideCountdownTimer?.cancel();
    setState(() {
      _remainingOverrideSeconds = 60;
    });
    _overrideCountdownTimer = Timer.periodic(const Duration(seconds: 1), (timer) {
      if (!mounted) {
        timer.cancel();
        return;
      }
      setState(() {
        if (_remainingOverrideSeconds > 1) {
          _remainingOverrideSeconds--;
        } else {
          _remainingOverrideSeconds = 0;
          _statusBanner = '60s timeout elapsed — reverted to AUTO mode';
          timer.cancel();
        }
      });
    });
  }

  Future<void> _triggerOverrideCommand(String action) async {
    setState(() {
      _isSendingCommand = true;
    });

    try {
      await widget.apiService.sendOverrideCommand(action, step: 5);
      if (!mounted) return;
      _startOrReset60sWatchdog();
      setState(() {
        _statusBanner = action == 'TAKE_CONTROL'
            ? 'MANUAL control active (60s watchdog started)'
            : 'Sent D-pad command: $action (60s timer reset)';
      });
      await _pollTelemetry();
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _statusBanner = 'Command error: $e';
      });
    } finally {
      if (mounted) {
        setState(() {
          _isSendingCommand = false;
        });
      }
    }
  }

  @override
  void dispose() {
    _telemetryPollTimer?.cancel();
    _overrideCountdownTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Live Telemetry Header Cards (Temp, Humidity, Lux)
          Row(
            children: [
              Expanded(
                child: _TelemetryMetricCard(
                  icon: Icons.thermostat_rounded,
                  iconColor: const Color(0xFFF97316),
                  label: 'Temperature',
                  value: '${_telemetry.temperature.toStringAsFixed(1)} °C',
                  subtitle: 'DHT11 Sensor',
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: _TelemetryMetricCard(
                  icon: Icons.water_drop_rounded,
                  iconColor: const Color(0xFF38BDF8),
                  label: 'Humidity',
                  value: '${_telemetry.humidity.toStringAsFixed(0)} %',
                  subtitle: 'DHT11 Sensor',
                ),
              ),
              const SizedBox(width: 10),
              Expanded(
                child: _TelemetryMetricCard(
                  icon: Icons.light_mode_rounded,
                  iconColor: const Color(0xFFFACC15),
                  label: 'Illuminance',
                  value: '${_telemetry.lux.toStringAsFixed(0)} lx',
                  subtitle: 'BH1750 I2C',
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),

          // Servo orientation & mode status bar
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
            decoration: BoxDecoration(
              color: const Color(0xFF1E293B),
              borderRadius: BorderRadius.circular(12),
              border: Border.all(color: const Color(0xFF334155)),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Row(
                  children: [
                    Container(
                      width: 10,
                      height: 10,
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: _isManualActive ||
                                _telemetry.currentMode == 'MANUAL'
                            ? const Color(0xFFF59E0B)
                            : const Color(0xFF22C55E),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Text(
                      'Mode: ${_isManualActive ? "MANUAL" : _telemetry.currentMode}',
                      style: const TextStyle(
                        fontWeight: FontWeight.w600,
                        fontSize: 13,
                      ),
                    ),
                  ],
                ),
                Text(
                  'Pan: ${_telemetry.pan}°  |  Tilt: ${_telemetry.tilt}°',
                  style: const TextStyle(
                    fontSize: 13,
                    color: Color(0xFFCBD5E1),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 18),

          // Take Control (60s) Primary Action Button
          SizedBox(
            height: 50,
            child: FilledButton.icon(
              style: FilledButton.styleFrom(
                backgroundColor: _isManualActive
                    ? const Color(0xFF0284C7)
                    : const Color(0xFFF59E0B),
                foregroundColor:
                    _isManualActive ? Colors.white : const Color(0xFF0F172A),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(12),
                ),
              ),
              onPressed: _isSendingCommand
                  ? null
                  : () => _triggerOverrideCommand('TAKE_CONTROL'),
              icon: const Icon(Icons.bolt_rounded),
              label: Text(
                _isManualActive
                    ? 'Manual Control Active (${_remainingOverrideSeconds}s remaining)'
                    : 'Take Control (60s)',
                style: const TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
          ),
          const SizedBox(height: 24),

          // 4-Way D-Pad Controller (Up / Down / Left / Right)
          Container(
            padding: const EdgeInsets.symmetric(vertical: 22, horizontal: 16),
            decoration: BoxDecoration(
              color: const Color(0xFF1E293B),
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: const Color(0xFF334155)),
            ),
            child: Column(
              children: [
                const Text(
                  'Dual-Axis Servo Steering (D-Pad)',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    color: Color(0xFFE2E8F0),
                  ),
                ),
                const SizedBox(height: 4),
                const Text(
                  'Each press steers 5° via /override and resets the 60s timer',
                  style: TextStyle(fontSize: 11, color: Color(0xFF94A3B8)),
                ),
                const SizedBox(height: 20),

                // UP Button (Tilt Up)
                _DPadButton(
                  icon: Icons.keyboard_arrow_up_rounded,
                  label: 'UP',
                  enabled: !_isSendingCommand,
                  onPressed: () => _triggerOverrideCommand('UP'),
                ),
                const SizedBox(height: 10),

                // LEFT / Center Indicator / RIGHT Row
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    _DPadButton(
                      icon: Icons.keyboard_arrow_left_rounded,
                      label: 'LEFT',
                      enabled: !_isSendingCommand,
                      onPressed: () => _triggerOverrideCommand('LEFT'),
                    ),
                    Container(
                      width: 68,
                      height: 68,
                      margin: const EdgeInsets.symmetric(horizontal: 10),
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        color: const Color(0xFF0F172A),
                        border: Border.all(color: const Color(0xFF334155)),
                      ),
                      alignment: Alignment.center,
                      child: Text(
                        _isManualActive
                            ? '${_remainingOverrideSeconds}s'
                            : 'AUTO',
                        style: TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.bold,
                          color: _isManualActive
                              ? const Color(0xFFF59E0B)
                              : const Color(0xFF22C55E),
                        ),
                      ),
                    ),
                    _DPadButton(
                      icon: Icons.keyboard_arrow_right_rounded,
                      label: 'RIGHT',
                      enabled: !_isSendingCommand,
                      onPressed: () => _triggerOverrideCommand('RIGHT'),
                    ),
                  ],
                ),
                const SizedBox(height: 10),

                // DOWN Button (Tilt Down)
                _DPadButton(
                  icon: Icons.keyboard_arrow_down_rounded,
                  label: 'DOWN',
                  enabled: !_isSendingCommand,
                  onPressed: () => _triggerOverrideCommand('DOWN'),
                ),
              ],
            ),
          ),
          const SizedBox(height: 14),
          Text(
            _statusBanner,
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 12, color: Color(0xFF94A3B8)),
          ),
        ],
      ),
    );
  }
}

class _TelemetryMetricCard extends StatelessWidget {
  final IconData icon;
  final Color iconColor;
  final String label;
  final String value;
  final String subtitle;

  const _TelemetryMetricCard({
    required this.icon,
    required this.iconColor,
    required this.label,
    required this.value,
    required this.subtitle,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: const Color(0xFF1E293B),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xFF334155)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: iconColor, size: 22),
          const SizedBox(height: 8),
          Text(
            value,
            style: const TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w700,
            ),
          ),
          const SizedBox(height: 2),
          Text(
            label,
            style: const TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w500,
              color: Color(0xFFCBD5E1),
            ),
          ),
          Text(
            subtitle,
            style: const TextStyle(
              fontSize: 10,
              color: Color(0xFF64748B),
            ),
          ),
        ],
      ),
    );
  }
}

class _DPadButton extends StatelessWidget {
  final IconData icon;
  final String label;
  final bool enabled;
  final VoidCallback onPressed;

  const _DPadButton({
    required this.icon,
    required this.label,
    required this.enabled,
    required this.onPressed,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 76,
      height: 76,
      child: ElevatedButton(
        style: ElevatedButton.styleFrom(
          backgroundColor: const Color(0xFF0F172A),
          foregroundColor: const Color(0xFFF8FAFC),
          padding: EdgeInsets.zero,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(14),
            side: const BorderSide(color: Color(0xFF475569)),
          ),
        ),
        onPressed: enabled ? onPressed : null,
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, size: 30, color: const Color(0xFFF59E0B)),
            Text(
              label,
              style: const TextStyle(
                fontSize: 10,
                fontWeight: FontWeight.w700,
                letterSpacing: 0.6,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
