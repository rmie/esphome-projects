# barrier_group

ESPHome external component for **unanimous distributed automation**: a named command
executes on all required nodes, or on none of them.

No external library required. ~400 lines of C++.

---

## Protocol: Flood + Barrier

```
         Node A                Node B                Node C
   (proposes START)       (required)             (required)
         │                       │                  │
         ├──── PROPOSE(id,START) ─────────────────►MC
         ├──── ACK(id,A) ──────────────────────────MC
         │                       │                  │
         │               ◄───────┤                  │
         │             ACK(id,B)─┼──────────────────MC
         │                       │                  │
         │                       │         ACK(id,C)┼──MC
         │                       │                  │
      [A,B,C ∈ acked_by]   [A,B,C ∈ acked_by]  [A,B,C ∈ acked_by]
         │                       │                  │
      EXECUTE                EXECUTE             EXECUTE
```

(MC = UDP multicast group, e.g. `239.1.2.3:6543`)

**Steps:**
1. Any node calls `barrier_group.propose: COMMAND`
2. Proposer sends `PROPOSE` + its own `ACK` to the multicast group
3. Each required node that receives `PROPOSE`: sends its own `ACK` to the multicast group
4. As each node accumulates ACKs, it checks if all `required_nodes` have ACK'd
5. When unanimous → `on_execute` automation fires locally on that node

**No EXECUTE broadcast needed** — each node makes the unanimous decision independently
from the ACKs it has collected.

---

## Guarantees

| Property | Behaviour |
|----------|-----------|
| **All-or-nothing** | Execute only fires when ALL required nodes ACK |
| **Any node may propose** | No leader required |
| **Offline node** | Remaining nodes never reach unanimous → timeout, no execute |
| **Node rejoins** | New proposals succeed; old timed-out ones do not re-fire |
| **Duplicate proposals** | `proposal_id` dedup prevents double-execution |
| **Concurrent proposals** | Each tracked independently by `proposal_id` |

---

## Wire Format (UDP, packed structs)

```
Magic: 0x42475250  ("BGRP")

PROPOSE message (fixed 68 bytes):
  uint32   magic
  uint32   group_id           ← stable hash of group configuration
  uint8    type = 1
  uint8    sender_node_id
  uint16   pad
  uint64_t proposal_id        ← (node_id << 56) | seq
  char[32] command_name       ← null-terminated, max 31 chars
  uint8[16] signature         ← truncated HMAC-SHA256 of above using PSK (if set)

ACK message (fixed 40 bytes):
  uint32   magic
  uint32   group_id           ← stable hash of group configuration
  uint8    type = 2
  uint8    sender_node_id
  uint16   pad
  uint64_t proposal_id
  uint8    acking_node_id
  uint8[3] pad
  uint8[16] signature         ← truncated HMAC-SHA256 of above using PSK (if set)
```

All multi-byte fields little-endian (ESP32 native).

---

## Configuration

The component is configured as a list of barrier groups, each with a unique `id`.

```yaml
barrier_group:
  - id: dehumidifiers
    # All nodes share this exact block — no per-device changes needed.
    nodes:
      - dehumidifier-1     # ESPHome device names (== App.get_name())
      - dehumidifier-2
      - dehumidifier-3
    commands:
      - name: START
        required_nodes: [dehumidifier-1, dehumidifier-2, dehumidifier-3]
        on_execute:
          - output.turn_on: relay
      - name: STOP
        required_nodes: [dehumidifier-1, dehumidifier-2, dehumidifier-3]
        on_execute:
          - output.turn_off: relay

    # Optional (defaults shown):
    port: 6543
    multicast_group: 239.1.2.3   # LAN-local multicast, TTL=1
    proposal_timeout_ms: 2000
    key: "my-preshared-secret"   # Optional pre-shared key for packet signing (HMAC-SHA256-128)
```

Each group is automatically isolated from other groups by a 32-bit FNV-1a hash of its configuration (including `id`, `nodes`, `port`, `multicast_group`, etc.), ensuring they can co-exist on the same port and multicast group without interfering.

### Security
If the `key` parameter is provided:
- **Packet Authentication**: All packets are signed using a truncated **HMAC-SHA256 (128-bit / 16-byte)** hash. Packets with invalid or missing signatures are immediately discarded.
- **Replay Protection**: The component tracks monotonic sequence numbers from each peer ID. Any replayed packet with a duplicate or older sequence number is rejected.

**Actions:**
Propose a command to a specific group by targeting its `id`:
```yaml
- barrier_group.propose:
    id: dehumidifiers
    command: START
```

**How nodes are identified:**
- At compile time the `nodes` list is sorted alphabetically; each name gets an integer ID (0, 1, 2, …).
- At runtime each device calls `App.get_name()` and finds its own ID in that list.
- No `node_id:` or IP address configuration is needed anywhere.

**Shared config pattern:**

```yaml
# common/my_group.yaml — identical content included by every device
barrier_group:
  - id: dehumidifiers
    nodes: [dehumidifier-1, dehumidifier-2, dehumidifier-3]
    commands:
      - name: START
        required_nodes: [dehumidifier-1, dehumidifier-2, dehumidifier-3]
        on_execute:
          - output.turn_on: relay
```

```yaml
# dehumidifier-1.yaml (and -2, -3 — only the substitution differs)
substitutions:
  main_name: dehumidifier-1
packages:
  group: !include common/my_group.yaml
```

---

## Known Limitations (v1)

- **ACK-before-PROPOSE race**: if an ACK arrives before the PROPOSE (extremely unlikely
  on LAN), the ACK is dropped and the proposal times out. Mitigation: retry logic
  (future work).
- **UDP fire-and-forget**: packet loss on noisy Wi-Fi may cause timeout. Mitigation:
  increase `proposal_timeout_ms` and rely on ESPHome's Wi-Fi reconnect.
- **No persistence**: if a node reboots mid-proposal, that proposal times out on peers.
- **`seen_proposals_` set**: entries are removed on successful execution and on timeout,
  so memory is bounded.
