# Release Notes

## [Unreleased] - 2026-06-13

### Changed
- **Terminology Refactoring**: Renamed all instances of `command` to `proposal` across configuration schemas, action names, and C++ source code to accurately reflect the consensus-based agreement nature of the component (e.g., `proposals:` instead of `commands:`, and `proposal: START` instead of `command: START`).

### Added
- **Type-Safe State Variables (Proposal State)**: Added support for arbitrary, compile-time typed state variables associated with each proposal (`state_vars` schema). Supported types include `float`, `double`, `bool`, and fixed-width integers (`int8_t` through `uint64_t`).
- **Dynamic Action Class Generation**: The Python generator now dynamically defines a C++ packed struct and custom `Action` subclass for each proposal instance to compile user-supplied template state values (constant or lambda).
- **Verify-Before-Parse Signature Security Model**: Restructured the `PROPOSE` wire message format to place the HMAC-SHA256 signature at a fixed 16-byte offset (bytes 20-35) right after the header, allowing complete packet authentication prior to parsing variable-length string fields to prevent buffer overrun/corruption exploits.
- **Participant Rejection Support**: Added the ability for any participant to reject a proposal by introducing an optional `accept_if` lambda to the proposal schema. If a participant's condition evaluates to `false` when a proposal is initiated or received, the participant will immediately transmit a `MSG_REJECT` packet to the multicast group instead of acknowledging it.
- **Wire Protocol MSG_REJECT**: Added a new wire message type `MSG_REJECT` (`type = 3`) to signal explicit rejection from any required node, causing all other nodes in the group to immediately and silently abort execution and clean up the proposal.
- **On Timeout Trigger**: Added an optional `on_timeout` action block to proposals. This automation triggers on each node when a proposal fails to execute within the designated `proposal_timeout_ms` duration.
- **On Reject Trigger**: Added an optional `on_reject` action block to proposals. This automation triggers on the proposing node when a proposal is explicitly rejected by a peer node (via `MSG_REJECT`), allowing the application layer to handle rejections gracefully (e.g., updating states, logging warnings, or triggering alarms).
- **Ring Buffer Deduplication**: Replaced the dynamic `std::set` tracking of seen proposals with a fixed-size 16-element ring buffer for ID deduplication. This eliminates dynamic memory allocations on proposal processing, bounds memory footprint statically, and prevents duplicate processing after proposal resolution to avoid spurious timeouts.
- **Cross-Platform ESP8266 Support**: Added compatibility for ESP8266 microcontrollers. Sockets fall back automatically to the core Arduino `WiFiUDP` library on ESP8266 to join multicast groups and send/receive datagrams.
- **Conditional Cryptography Guarding**: Conditionally compiled `mbedtls` hashing for ESP32. On non-ESP32 targets (like ESP8266), signature checking/generation is stubbed out, and a warning is printed to the logger at startup if a pre-shared `key` is configured.

### Fixed
- **Replay Protection Deadlock**: Fixed a critical bug where a rebooted node would have its sequence number reset to 0, causing peers to drop all its proposals as replay attacks. Sequence numbers are now 56-bit, with the upper 32 bits seeded with a random boot epoch to ensure monotonic progression across power cycles.
- **Sequence Number Truncation**: Fixed silent truncation of the 56-bit proposal sequence number by upgrading `peer_last_seq_` from `uint32_t` to `uint64_t`.
