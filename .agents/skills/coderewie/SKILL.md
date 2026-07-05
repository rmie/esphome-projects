---
name: esphome-code-reviewer
description: Reviews existing ESPHome YAML configurations and embedded or external C++ code (lambdas/custom components) for hardware safety, logical bugs, and anti-patterns.
---

# ESPHome & C++ Code Reviewer Skill

## Use this skill when
- The user provides an existing ESPHome `.yaml` configuration or adjacent `.h` files and asks for a code review, optimization, or bug fix.
- Validating pull requests or local changes meant for deployment onto ESP8266/ESP32 microcontrollers.

## Do not use this skill when
- Generating a brand new ESPHome configuration from scratch without any baseline files.

## Review Checklist

### 1. Hardware & GPIO Safety (Critical Gate)
- **Strapping Pins**: Check if critical pins (e.g., GPIO0, GPIO2, GPIO15 on ESP8266/ESP32) are assigned to components that could pull them low/high during boot, causing bootloops.
- **Internal Flash Pins**: Flag the usage of internal SPI flash pins (typically GPIO6-11 on standard ESP32 chips).
- **Current Limits**: Verify that outputs (like LEDs or relays) do not overload GPIO current limits without appropriate hardware configuration.

### 2. Secrets & Security Audit
- Ensure **no plain-text passwords, Wi-Fi credentials, or API keys** exist in the YAML. Everything must be offloaded to `!secret`.
- Check that the `api:` block has an `encryption:` key configured with a preshared key (`key:`).

### 3. C++ Lambda & Performance Review
- **Blocking Code**: Scan all `lambda:` blocks for blocking calls like standard C++ `delay()` or long `while` loops. Demand asynchronous non-blocking functions (`id(x).set_timeout()`).
- **State Evaluation**: Verify that sensor states inside lambdas handle `nan` (Not a Number) values gracefully using `std::isnan(id(sensor_id).state)` to prevent bad logic cascades.
- **Component Availability**: Ensure components referenced inside a lambda via `id(comp_name)` actually exist and are defined *above* or properly linked to the calling component.

### 4. ESPHome Architecture & Efficiency
- **Component Flooding**: Check if rapid sensors (like continuous analog read or energy monitors) have a realistic `update_interval` or a `delta` filter to prevent flooding Home Assistant with MQTT/API states.
- **Substitutions Usage**: Flag hardcoded, repetitive values (like identical sensor update times or base names) that should be modularized using the `substitutions:` block.

## Execution Instructions
1. **Identify Hardware Architecture**: Parse the `esp32:` or `esp8266:` block to establish the specific chip constraint matrix (e.g., ESP32-S3 vs. ESP32-C3).
2. **Scan Ingested Files**: Inspect the target YAML and any files linked via the `includes:` section. Do not analyze generic project history.
3. **Isolate Defects**: Cross-reference the implementation with the checklist sections above.
4. **Formulate Fixes**: Provide exact structural YAML/C++ replacement diffs for any issues discovered.

## Anti-Rationalization Rules
- ❌ **No "Looks Good" Complacency**: Do not assume code works because the user says it compiles. Many ESPHome configurations compile successfully but cause runtime runtime bootloops or state flooding.
- ❌ **Strict Execution Gates**: Never ignore missing `!secret` references under the assumption that "it's just a local test file".

## Output Format

### 📄 Defect Report: [File Path or Component Name]
- **[CRITICAL]** [Hardware/Security flaw that causes bootloops, hardware damage, or credential exposure].
    - *Fix*: `[Provide exact replacement markdown code block for YAML or C++]`
- **[WARNING]** [Performance bottleneck, blocking lambda, or missing encryption].
- **[SUGGESTION]** [Code readability improvement, missing substitutions, or filter optimizations].
