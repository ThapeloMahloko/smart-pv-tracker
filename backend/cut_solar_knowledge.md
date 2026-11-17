# Central University of Technology (CUT) Sports Ground Solar Installation
# Populate the bracketed placeholders below to customize the AI Guide's RAG responses.

## Campus Installation Overview
- **Institution**: Central University of Technology (CUT), Bloemfontein Campus, Free State, South Africa
- **Installation Site**: CUT University Sports Ground Solar PV Array
- **Total Installed Capacity (kWp)**: [TODO: e.g., 500 kWp]
- **Number of Solar Panels**: [TODO: e.g., 1,200 monocrystalline PV modules]
- **Inverter Configuration**: [TODO: e.g., Three-phase grid-tied string inverters]
- **Estimated Annual Energy Yield**: [TODO: e.g., 950 MWh / year]
- **Campus Facilities Powered**: Sports complex, stadium floodlights, surrounding lecture halls, and campus microgrid distribution.

## Relationship to the Keyestudio KS0530 Demonstration Unit
- The miniature ESP32-powered Keyestudio KS0530 dual-axis tracker demonstrates active solar tracking using 4 LDR photoresistors (Top, Bottom, Left, Right), Pan/Tilt micro-servos, a BH1750 digital lux sensor, and a DHT11 temperature/humidity sensor. Top is on the LCD side and Bottom is opposite the LCD. Identify Left and Right when viewing the sensor face with the LCD side at the top.
- Confirmed sensor wiring: Top = GPIO 32, Right = GPIO 34, Left = GPIO 35, Bottom = GPIO 33. Tracking compares Top with Bottom and Left with Right directly. Telemetry uses ldr_top, ldr_bottom, ldr_left, and ldr_right. Servo direction must be verified on the physical assembly.
- While large ground-mount installations on the CUT sports ground use optimized fixed-tilt or single-axis configurations for structural efficiency, this dual-axis prototype illustrates how maintaining a 90° angle of incidence maximizes solar irradiance throughout the day across Bloemfontein's high-insolation climate.

## Confirmed prototype hardware and measurements
- The BH1750 is installed and tested at I2C address 0x23. It measures illuminance in lux, not solar electrical power.
- A working 16x2 LCD uses LCD_I2C at 0x27 on SDA GPIO 21 and SCL GPIO 22.
- Charging module S connects to GPIO 36, G to GND. Its half-voltage signal measures the 18650 battery voltage, not panel voltage, current, watts, or energy. Do not infer charging status or battery percentage from voltage alone.
- Button GPIO 2 pauses tracking and centres the servos while held. Release resumes AUTO. LED GPIO 13 indicates this hold and buzzer GPIO 15 beeps when it begins. Physical hold overrides remote commands.
- Pan GPIO 18 and tilt GPIO 19 initially use 70–110 degree travel. Telemetry angles are commands, not measured positions. Startup does not wait for Wi-Fi or a button press.
