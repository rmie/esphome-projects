# Dehumidifier

ESPHome configuration for an ESP32-based smart dehumidifier controller. Monitors temperature and humidity, controls a relay based on configurable setpoints, and tracks power consumption and operation hours.

## Hardware

| Component | Interface | GPIO |
|-----------|-----------|------|
| Relay output | GPIO | GPIO16 |
| AHT20 temp/humidity sensor | I²C SDA | GPIO21 |
| AHT20 temp/humidity sensor | I²C SCL | GPIO22 |
| PZEM-004T power meter | UART TX | GPIO01 |
| PZEM-004T power meter | UART RX | GPIO03 |

## Features

### Operating Modes

Selectable via the **Mode** select entity:

| Mode | Behaviour |
|------|-----------|
| `on` | Relay always on |
| `off` | Relay always off |
| `auto` | Relay controlled automatically by humidity setpoint |

### State Machine

Uses [`esphome-state-machine`](https://github.com/muxa/esphome-state-machine) to manage internal states:

```
ALWAYS_OFF  ←→  ALWAYS_ON
    ↕               ↕
ALWAYS_OFF_FULL ←→ ALWAYS_ON_FULL
    ↕               ↕
 AUTO_OFF   ←→   AUTO_ON
                    ↕
                 AUTO_FULL
```

States transition on inputs: `MODE_OFF`, `MODE_ON`, `MODE_AUTO`, `POWER_LOW`, `POWER_HIGH`, `BELOW`, `ABOVE`.

- **`_FULL`** states are entered when the power meter reports low power draw (tank likely full), triggering a 30 s delay before publishing `"full"` status.
- **Auto** states toggle the relay based on humidity vs. setpoint + hysteresis.

### Configurable Parameters

| Entity | Default | Description |
|--------|---------|-------------|
| **Setpoint** | — | Target humidity (20–80 %, step 5) |
| **Hysteresis** | — | Deadband above setpoint before dehumidifier turns on (5–15 %, step 1) |

### Sensors

**Environment**
- Temperature (AHT20)
- Humidity (AHT20)
- Absolute Humidity (g/m³)
- Saturation Vapor Pressure (hPa)
- Vapor Pressure (hPa)
- Dew Point (°C)

**Power (PZEM-004T)**
- Power (W)
- Energy (kWh)
- Current (A)
- Voltage (V)
- Frequency (Hz)
- Power Factor
- Apparent Power (VA)
- Reactive Power (VAr)

**Diagnostics**
- Operation Hours (h) — persisted across reboots
- Relay Status text sensor (`off` / `on` / `idle` / `running` / `full`)
- State Machine State

### Safety

On both boot and shutdown the relay is forced off and the state machine is reset to `ALWAYS_OFF`, preventing the dehumidifier from running uncontrolled after a reboot.

## Common Packages

Inherits via `common/esp32.yaml` → `common/all.yaml`:

- `default.yaml` — device name, web server, Prometheus, HA API, uptime/version sensors
- `wifi.yaml` — Wi-Fi credentials
- `logging.yaml` — log level
- `bt_proxy.yaml` — Bluetooth proxy
