# Wildlife Cam

ESPHome configuration for an ESP32-CAM board intended for wildlife and outdoor monitoring. Provides live MJPEG video streaming, snapshot capture, adjustable onboard flash LED brightness, and dynamic exposure controls.

## Hardware & Pinout

Configured for standard AI-Thinker ESP32-CAM modules with external PSRAM:

| Component / Function | Interface / Type | GPIO Pin | Description |
|----------------------|------------------|----------|-------------|
| Flash LED | LEDC (PWM) | GPIO4 | Onboard high-brightness white LED (dimmable) |
| Status LED | GPIO (inverted) | GPIO33 | Small onboard red LED |
| Camera XCLK | Clock Out | GPIO0 | 20 MHz external clock |
| Camera SCCB SDA | I²C SDA (`cam_i2c`) | GPIO26 | Camera sensor control bus SDA |
| Camera SCCB SCL | I²C SCL (`cam_i2c`) | GPIO27 | Camera sensor control bus SCL |
| Camera D0–D7 | Data bus | GPIO5, 18, 19, 21, 36, 39, 34, 35 | 8-bit parallel image data |
| Camera VSYNC | Digital In | GPIO25 | Vertical synchronization |
| Camera HREF | Digital In | GPIO23 | Horizontal reference |
| Camera PCLK | Clock In | GPIO22 | Pixel clock |
| Camera PWDN | Digital Out | GPIO32 | Power down pin |

## Features

### Video Streaming & Snapshots

Direct HTTP video streaming and snapshot endpoints are provided by `esp32_camera_web_server`:

| Endpoint | Port | Mode | URL | Description |
|----------|------|------|-----|-------------|
| MJPEG Stream | 8080 | `stream` | `http://<ip>:8080/` | Live video feed (1 client at a time) for browser, Frigate, go2rtc, or VLC |
| Snapshot | 8081 | `snapshot` | `http://<ip>:8081/` | Single still JPEG image on request |
| ESPHome Web UI | 80 | Web Server v3 | `http://<ip>/` | Device status, restart button, and interactive controls |

In addition, Home Assistant receives the camera feed directly via the native API component.

### Exposure Controls & Sliders

Live controls exposed directly in the Web UI and Home Assistant:

| Entity | Type | Default | Description |
|--------|------|---------|-------------|
| **Auto Exposure (AEC)** | Switch | `ON` | Toggles sensor Automatic Exposure Control (`AUTO` / `MANUAL`) |
| **DSP AEC2** | Switch | `OFF` | Enables sensor DSP auto-exposure algorithm (better high-contrast night handling) |
| **Auto Exposure Level** | Slider (-2 to +2) | `0` | Exposure target compensation when Auto Exposure is active |
| **Manual Exposure Time** | Slider (0 to 1200) | `300` | Direct shutter/exposure row value used when Auto Exposure is turned off |

### Dimmable LED (PWM)

- **Flash LED (`esp_cam_light`)**: Controlled via ESP32 `ledc` PWM on GPIO4 (channel 2, 1220 Hz, `gamma_correct: 0` for direct linear duty cycle).
- **Dedicated Brightness Slider (`flash_brightness`)**: A template `number` slider (0% to 100%) visible directly on both the ESPHome web dashboard (port 80) and Home Assistant dashboards without having to dig into light sub-menus. Setting `0%` turns the light off; setting `1%–100%` turns it on to that exact duty cycle.
- **Red Status LED (`esp-cam status led`)**: Inverted binary light on GPIO33.

### Framerate Management

- **Max Framerate**: Set to `10.0 fps` to provide smooth video while preventing ESP32 thermal throttling and Wi-Fi congestion.
- **Idle Framerate**: Set to `0.1 fps` when no client is actively requesting the stream to conserve power and reduce heating.

## Common Packages

Inherits common configurations via `../common/esp32-cam.yaml` → `../common/esp32.yaml` → `../common/all.yaml`:

- `esp32-cam.yaml` — PSRAM setup (`mode: quad`, `speed: 80MHz`), camera pinout and clock configuration, PWM flash LED output, and onboard status LED
- `esp32.yaml` — ESP-IDF framework, chip revision configuration (`minimum_chip_revision: "1.0"`)
- `default.yaml` — Device naming, web server on port 80, Prometheus metrics, Home Assistant API, uptime, CPU temperature, and restart button
- `wifi.yaml` — Wi-Fi network and signal sensor
- `logging.yaml` — Logging configuration

## Power Supply Recommendations

- The ESP32-CAM draws significant current spikes during Wi-Fi transmission and camera capture. Use a clean **5V @ 2A** power supply.
- Adding a 470 µF–1000 µF capacitor across the 5V and GND pins near the module helps prevent brownout resets during stream startup or flash LED activation.
