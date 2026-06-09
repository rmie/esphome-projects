# Hot Water Heat Pump Override Controller

ESPHome configuration for adding smart override capability to an existing hot water heat pump system. Allows forcing the heat pump or resistive backup heater to run based on external conditions (cheap power rates, solar excess, manual override), while the heat pump retains its native thermostat/control logic.

## Overview

This controller provides **override/force-run capability** for a hot water heat pump system. The heat pump's own thermostat and safety controls remain in place - this device simply bridges the control inputs to enable external triggering.

### Use Cases

- **Solar Divert**: Force heating when excess solar PV generation is available
- **Time-of-Use Pricing**: Override to heat during off-peak/cheap electricity rates
- **Demand Response**: Participate in grid programs by advancing heating cycles during off-peak periods
- **Manual Override**: Force heating for maintenance, testing, or unusual hot water demand

The resistive backup heater can be triggered independently for faster heating when power is abundant and cheap.

### Key Capabilities:

- **Override Control**: Independently forces the heat pump and/or resistive backup heater to run via relay outputs, regardless of normal thermostat scheduling
- **Power Monitoring**: Measures voltage, current, power, energy, frequency, and power factor via PZEM-004T AC power meter
- **Status Indication**: WS2812 RGB LEDs provide visual feedback for heating element states
- **Bluetooth Proxy**: Extends Home Assistant Bluetooth reach for nearby BLE devices
- **Smart Metrics**: Calculates apparent power (VA) and reactive power (VAr) from measured values

### Typical Automation Example

```yaml
# Home Assistant automation: Solar divert during peak generation
- alias: "Hot Water Solar Divert"
  trigger:
    - platform: numeric_state
      entity_id: sensor.inverter_power
      above: 2500  # Excess solar available
  action:
    - service: switch.turn_on
      target:
        entity_id: switch.heat_pump_override
```

## Pin Usage

| GPIO Pin | Function | Description |
|----------|----------|-------------|
| GPIO1 | UART TX | PZEM-004T power meter data transmit |
| GPIO3 | UART RX | PZEM-004T power meter data receive |
| GPIO16 | Digital Output | Heat pump relay control (active high) |
| GPIO17 | Digital Output | Resistive heater relay control (active high) |
| GPIO18 | RMT Output | WS2812 RGB status LEDs (2 LEDs in chain) |

**Note**: UART uses default ESP32 pins. Relays should be driven via optocouplers or relay modules with 3.3V compatible inputs.

## Wiring Schematic

```
                    +------------------+
                    |     ESP32        |
                    |   (esp32dev)     |
                    |                  |
    +---------------+ GPIO1  (UART TX) |
    |               + GPIO3  (UART RX) |
    |               + GPIO16 (HeatPump)|----+   +-------------+
    |               + GPIO17 (ResHeat) |    |   | Heat Pump   |
    |               + GPIO18 (WS2812)  |----+   | Relay Module|
    |               + 3.3V             |        | (NO contact)|
    |               + GND              |        +------+------+
    |               + 5V (VIN)       |               |
    |               +------------------+               |
    |                                                 |
    |   PZEM-004T            Status LEDs            |
    |  +-----------+         +------------+         |
    +->+ TX        |         | LED1       |         |
       + RX        |         | (ResHeat)  |         |
       + 5V        |         +------------+         |
       + GND       |         | LED2       |         |
       +-----------+         | (HeatPump) |         |
                              +------------+         |
                                                     |
                                              +------v------+
                                              | Heat Pump   |
                                              | Contactor   |
                                              +-------------+

    +------------------+        +------------------+
    + GPIO17 (ResHeat) +--------+ Resistive Heater |
    |                  |        | Relay Module     |
    |                  |        | (NO contact)     |
    |                  |        +--------+---------+
    |                  |                 |
    |                  |        +--------v---------+
    |                  |        | Resistive Element|
    |                  |        | (Backup Heater)    |
    |                  |        +------------------+

Power Supply:
- ESP32 + Relays: Built-in 230V PSU on dual relay board (no external 5V supply needed)
- PZEM-004T: 5V from isolated buck converter or USB adapter (separate from relay board PSU)
- WS2812: 5V from same source as PZEM or small buck converter from 230V

Current Transformer (CT):
- PZEM-004T AC includes built-in voltage measurement and better insulation than CT-only variants; clamp CT around one conductor of AC mains feeding the heat pump system
```

## Parts List

| Item | Specification | Purpose | Notes |
|------|---------------|---------|-------|
| **ESP32 Dual Relay Board** | ESP32-WROOM-32 with 2x relays, 230V PSU | Main controller + relay driver | All-in-one board with integrated 230V power supply and optocoupled relays (e.g., ESP32-DevKitC + relay shield) |
| **PZEM-004T V3 (AC)** | AC 80-260V, 100A with CT coil | Power/energy measurement | UART 9600 baud, includes split-core CT; AC version has better insulation than basic CT-only variant |
| **Relay Module** | (Built-in to dual relay board) | Switching heat pump and resistive heater | 10A+ rating for resistive heater; verify relay ratings match your heat pump current draw |
| **WS2812 LEDs** | 5050 SMD, 2 LEDs | Status indication | Any addressable RGB LED strip works; GRB order configured |
| **Power Supply** | Built-in 230V PSU | ESP32 and relay power | Use ESP32 dual relay board with integrated 230V power supply (e.g., ESP32-DevKitC + relay shield or similar all-in-one board) |
| **Enclosure** | IP65+ rated | Weather protection | Vented if in humid environment |
| **Wiring** | 18-22 AWG stranded | Signal and low-power connections | Use ferrules for terminal connections |
| **Terminal Blocks** | 5.08mm pitch screw terminals | Secure field wiring | WAGO or Phoenix Contact style |

### Optional but Recommended

| Item | Purpose |
|------|---------|
| Fuse holder + fuses | Overcurrent protection for heater circuits |
| MOV (Metal Oxide Varistor) | Surge protection on relay outputs |
| 470 Ohm resistor | Series resistor on WS2812 data line (GPIO18 to LED DIN) |
| Cable glands | Strain relief and sealing for enclosure |

## Home Assistant Integration

When connected to Home Assistant via ESPHome, the following entities are exposed:

### Switches
- **Heat Pump Override**: Force heat pump to run (bypasses normal thermostat scheduling)
- **Resistive Heater Override**: Force resistive backup heater to run

### Sensors
- **Power**: Real power consumption (W)
- **Energy**: Cumulative energy usage (kWh)
- **Current**: RMS current (A)
- **Voltage**: RMS voltage (V)
- **Frequency**: AC frequency (Hz)
- **Power Factor**: Cosine of phase angle
- **Apparent Power**: VA (calculated)
- **Reactive Power**: VAr (calculated)
- **WiFi Signal**: RSSI (dBm)

### Status Indication

The two WS2812 LEDs indicate the state of the heating elements:

| LED | Function | On (Green) | Off |
|-----|----------|------------|-----|
| LED 0 | Resistive Heater status | Heater ON | Heater OFF |
| LED 1 | Heat Pump status | Heat Pump ON | Heat Pump OFF |

Both LEDs use 25% brightness green when active to avoid being overly bright in a utility space.

## Configuration Notes

1. **Flash Mode**: Uses `dio` flash mode for compatibility with various ESP32 modules
2. **Minimum Chip Revision**: 3.1+ required for specific features
3. **Framework**: ESP-IDF (not Arduino) for better Bluetooth coexistence
4. **Bluetooth Proxy**: 3 connection slots, active connections enabled
5. **Power Sensor**: Values throttled to 15s updates to reduce Home Assistant database load
6. **Energy Persistence**: PZEM-004T internally stores energy count across power cycles

## Safety Considerations

⚠️ **WARNING**: This device controls high-voltage AC equipment. 

- All mains voltage wiring must be performed by a qualified electrician
- Maintain proper clearances between low-voltage (ESP32) and high-voltage (relay contacts) conductors
- Use appropriately rated relays and wiring for your heat pump's current draw
- The resistive heater typically draws significant current (15-30A @ 240V) - ensure relay and wiring ratings exceed this
- Include overcurrent protection (circuit breaker/fuse) appropriate for the load
- Use an IP-rated enclosure when installed in utility areas
- Install emergency stop or disconnect within reach of the equipment

## Installation

1. Flash ESP32 with this configuration
2. Install in enclosure with appropriate cable glands
3. Mount CT clamp around the line conductor feeding the heat pump (not neutral, not both)
4. Connect relay module control inputs to GPIO16 and GPIO17
5. Route relay outputs through appropriate contactors for the heat pump and resistive heater
6. Connect WS2812 LEDs via short wires (or add level shifter for longer runs)
7. Power on and verify in Home Assistant

## Troubleshooting

| Symptom | Check |
|---------|-------|
| No power readings | Verify UART wiring (GPIO1↔RX on PZEM, GPIO3↔TX on PZEM), PZEM has 5V |
| Relays not triggering | Verify 3.3V control signal, check relay module jumper settings |
| LEDs not working | Check 5V supply, verify GPIO18 connection, test with multimeter |
| Bluetooth proxy not working | Check Home Assistant ESPHome add-on version, ensure WiFi is stable |
