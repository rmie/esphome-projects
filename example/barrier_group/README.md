# Safety Interlock Scenario: Kitchen Furnace & Pellet Stove

> [!CAUTION]
> **CRITICAL SAFETY WARNING & DISCLAIMER**
> This implementation is an educational proof-of-concept for demonstrating decentralized consensus in ESPHome using the `barrier_group` component. 
> - **IT MUST NOT be used as a primary life-safety interlock.**
> - Certified hardware-level interlocks (e.g., physical power-cutoff switches approved under local fire/chimney regulations like German FeuVo) are required for safety-critical combustion appliances.
> - **A dedicated, certified physical Carbon Monoxide (CO) detector with an audible alarm MUST be installed** in any area operating these combustion appliances.

This scenario demonstrates a real-world application of the `barrier_group` component to implement a safety interlock system.

## Scenario

German safety regulations require that a **kitchen furnace (extractor hood / exhaust fan)** and a **pellet stove** in the same apartment are never operated at the same time. This prevents the exhaust fan from creating negative pressure that could pull toxic carbon monoxide from the pellet stove back into the living space.

Traditionally, this safety requirement is enforced by cutting power to the kitchen furnace using one of the following methods:
- Monitoring a window with an open/closed contact sensor (only allowing the furnace to run if the window is open).
- Measuring the exhaust temperature of the pellet stove.

### Drawbacks of Traditional Solutions
- **Lights are disabled**: Cutting power to the furnace renders its integrated lighting completely useless even when not cooking.
- **Slow response time**: Relying on exhaust temperature measurements is slow, creating a window of risk before the safety system reacts.

### The Smart Solution
Monitoring the power consumption of both the kitchen furnace and the pellet stove solves these issues:
- **Integrated light works**: We can easily distinguish between low power draw (just the lights being on) and high power draw (the exhaust fan running).
- **Rapid safety reaction**: The power consumption of the pellet stove rises above a threshold immediately upon activation, allowing for real-time safety interlock (within the maximum 7-second reaction window, which is well below the 60-second permitted threshold).

---

## Implementation Details

The setup consists of the following components:
- **Smart Plugs**: Both the kitchen furnace and the pellet stove are connected using smart plugs. (Example configuration based on [Athom Smart Plug](https://github.com/athom-tech/athom-configs/blob/main/athom-smart-plug.yaml)).
- **Window Sensor**: The window's open/closed state is monitored via a Bluetooth door/window sensor (e.g., [Shelly BLU Door/Window](https://www.shelly.com/de/products/shelly-blu-door-window-white?srsltid=AfmBOorKi-8RsakXHmU_EBUi_GyDm55p8OuHXsN2j7HtLOGFvLoUuM_c)).

### Safety Logic Rules

1. **Kitchen Furnace Safety Rule**: The kitchen furnace exhaust fan is permitted to run if and only if:
   - The window is **open**, OR
   - The pellet stove power consumption is **below** the safety threshold (the stove is off).

2. **Pellet Stove Safety Rule**: The pellet stove is permitted to run if and only if:
   - The window is **open**, OR
   - The kitchen furnace power consumption is **below** the safety threshold (the fan is off).

---

## Role of `barrier_group`

The `barrier_group` component is used to establish a fail-safe, unanimous consensus between both appliances:
- It ensures both devices agree on their current operational states before any transition into an active state occurs.
- If a device proposes starting up, it must receive an acknowledgement from the other device.
- If network communication fails, or if a device is offline and does not respond within `proposal_timeout_ms`, the proposal will time out. The local `on_timeout` action will safely keep or transition the device into a safe (disabled) state rather than violating safety regulations.

---

# Safety Considerations

> [!WARNING]
> **SINGLE POINT OF FAILURE: WIRELESS WINDOW SENSOR**
> The interlock bypass relies entirely on a single wireless Bluetooth window sensor. If the sensor fails (e.g., battery dies or RF signal is blocked) while the window is CLOSED, the system fails safe by preventing both appliances from running simultaneously. However, relying on a single wireless sensor for bypass validation is a single point of failure. Professional life-safety systems require **dual-channel validation** (e.g., two independent physical, hardwired switches wired to verify window status) to eliminate single-point failures. This software solution is hobby-grade and **MUST NOT** be relied upon to prevent asphyxiation.
>
> [!CAUTION]
> **BLE SPOOFING ATTACK VECTOR**
> The window sensor broadcasts data using unencrypted, unauthenticated BLE advertisements. A malicious actor within BLE range could spoof the sensor's MAC address and inject a `window_open: true` payload using a cheap microcontroller. This zero-skill attack would completely bypass the safety interlock as there is no cryptographic validation of the payloads.
>
> [!NOTE]
> As a technical exception/context note, commercial exhaust control systems in Germany with official **DIBt-Zulassung** (e.g. certified products from Broko or Schabus) usually only employ a single window contact sensor (magnetic reed contact). However, these systems use certified, fail-safe radio protocols and hardware designed to meet machinery safety standards, whereas this implementation relies on consumer-grade BLE advertisements and ESPHome.

This implementation employs a **fail-secure** design philosophy to ensure safe operation even under network, hardware, or communication failures.

### 1. Watchdog for BLE Window Tracking
Because Bluetooth Low Energy (BLE) advertisements can be lost or blocked, neither device trusts a latched "open" state indefinitely:
- A local watchdog timer (`window_watchdog`) is configured on both devices.
- Every time a BLE advertisement is successfully received directly from the window sensor, it resets a 30-second timer.
- If no BLE packet is received for 30 seconds (due to battery failure, radio interference, or range issues), the watchdog expires and automatically sets the window states to `OFF` (assuming the window is closed).

### 2. Peer-to-Peer State Synchronization (Supplementary)
To account for cases where only one plug is within BLE range of the window sensor, the window's state is shared between the devices as a **convenience supplement** to counter unreliable BLE communication. The primary safety mechanism is always the local 30-second watchdog. If peer-to-peer communication fails, the system safely falls back to the local watchdog expiration.
- Whichever device has direct BLE contact with the window sensor proposes a `WINDOW_STATE` change proposal immediately when the window opens or closes (or when its local 30-second watchdog times out).
- This proposal transmits the window's state dynamically via the `state_vars` payload (`window_open` as a boolean).
- When the peer device executes this proposal, it updates its `window_open_effective` state to match the proposal value. If the window is open, it executes/resets its local 30-second watchdog timer; if closed, it stops the watchdog timer.
- The effective window state (`window_open_effective`) remains **open** as long as either a direct local BLE advertisement or a peer's `WINDOW_STATE` proposal is received within the 30-second watchdog window.

### 3. Emergency Broadcasting
Both nodes periodically broadcast their interlock state. If a node detects a permanent hardware failure (e.g. a welded relay), it aggressively broadcasts `emergency: true` every 2 seconds. The peer receives this signal and instantly cuts power to its own relay and locks out to maintain fail-safe security.

### 4. Concurrent Relay Engagement (Lights & Idle Power)
To allow safe, low-power functionality (such as turning on the furnace's integrated lights while the pellet stove is running), both smart plug relays are kept `ON` by default in a `SAFE` state.
- This allows standby power or auxiliary functions (like lights) to remain functional on both devices simultaneously, regardless of the window state.
- Safety is enforced via continuous active permission polling rather than preemptive relay exclusion.

### 5. Active Permission Polling & Asymmetric Priority
Safety is enforced dynamically using an **Active Permission Model**. Instead of synchronizing peer states, the system uses the `barrier_group` consensus engine to explicitly "ask" for permission:
- **The Furnace Ping:** While the furnace exhaust fan is running (power > 10W), the furnace repeatedly proposes an `AUTHORIZE_FURNACE` request every 5 seconds.
- **The Stove Judge:** The pellet stove evaluates this proposal. It only allows the furnace to run if the window is open or the stove itself is off.
- **Fast Lockout:** If the stove is running with a closed window, it instantly rejects the `AUTHORIZE_FURNACE` ping. If the stove loses power or Wi-Fi, the ping times out after 2 seconds. In both cases, the furnace immediately transitions to `COMM_LOCKOUT` and forcefully cuts its relay.
- This creates an **asymmetric priority**: the stove is free to run, but the furnace must actively acquire and maintain permission to operate its exhaust fan. During normal operation, failsafe reaction time is reduced to a maximum of 7 seconds. *Note: Upon device reboot, there is a brief grace period (up to ~15-20 seconds) before the first power reading is registered and authorization is required. This brief overlap is permitted as hazardous negative pressure takes time to build up, and it crucially allows the furnace's integrated lights to function immediately without waiting for network consensus.*

### 6. Welded Relay Contacts (Emergency State)
- Since smart plug relay contacts can occasionally weld shut (sticking in the `ON` position), the system monitors power consumption continuously.
- If the relay is commanded `OFF` but power consumption remains **above a dead-band threshold (e.g., `5.0W`)** for 10 seconds, `local_emergency` is triggered.
- Once triggered, the faulty device locks itself out in non-volatile flash memory (which persists across reboots), attempts to force its relay OFF, and aggressively broadcasts `emergency: true` to the peer alongside its MAC address. Because a welded relay is a permanent hardware failure, this is a terminal state. The lockout cannot be reset via software or button; the physical smart plug must be replaced.
- The peer receives this signal, latches the faulty MAC address into its own non-volatile memory, and instantly cuts power to its own relay.
- **Smart Headless Recovery:** When the faulty smart plug is physically replaced, the new unit will broadcast its safe state with a new MAC address. The peer device detects the hardware swap and automatically clears the emergency lockout, resuming normal operation.

### 7. Independent of Home Assistant Server
All safety-critical communication, consensus checking, and state sharing happen directly between the two smart plugs using local UDP multicast. No Home Assistant server is involved in the safety interlock loop, ensuring safety is maintained even if the central home automation server is offline or restarting.

---

> [!IMPORTANT]
> **MANDATORY SECONDARY SAFETY CONTROLS**
> Always install a physical, battery-backed, certified Carbon Monoxide (CO) detector in the living area where combustion appliances are operated. Software-only interlocks running on hobbyist microcontrollers and unencrypted wireless links can fail due to RF interference, software bugs, or hardware failure. Do not rely on this code as your sole line of defense against CO poisoning.

---

## Known Behaviors

### Transient Lockout Bounce Behavior (Failsafe)
If an appliance is actively running and gets interrupted by a safety violation (window closure) or communication failure (peer goes offline), it triggers a failsafe lockout (`COMM_LOCKOUT_REJECT` or `COMM_LOCKOUT_TIMEOUT`) and cuts power to the appliance relay. 

When the safety condition clears (the window opens) or communication is restored (the peer comes back online and authorizes the request), the state machine transitions back to `SAFE`. While the relay turns back `ON` to provide standby power, the appliance itself remains off until the user manually restarts it via its physical control panel. This is intentional fail-safe behavior to prevent the unexpected automated restart of combustion or ventilation appliances after a safety interruption.