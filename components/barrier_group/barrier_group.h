#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#ifdef USE_ESP8266
#include <WiFiUdp.h>
#else
#include "lwip/sockets.h"
#endif

#include <vector>
#include <set>
#include <string>
#include <functional>

namespace esphome {
namespace barrier_group {

static constexpr uint32_t MAGIC       = 0x42475250U;  // "BGRP"
static constexpr uint8_t  MSG_PROPOSE = 1;
static constexpr uint8_t  MSG_ACK     = 2;
static constexpr uint8_t  MSG_REJECT  = 3;

struct __attribute__((packed)) WireHeader {
    uint32_t magic;
    uint32_t group_id;
    uint8_t  type;
    uint8_t  sender_id;
    uint16_t pad;
};

struct __attribute__((packed)) ProposeFixedHeader {
    WireHeader header;
    uint64_t   proposal_id;
    uint8_t    signature[16]; // Fixed location signature
}; // Size is exactly 36 bytes

struct __attribute__((packed)) AckMsg {
    WireHeader header;
    uint64_t   proposal_id;
    uint8_t    acking_node_id;
    uint8_t    pad[3];
    uint8_t    signature[16]; // truncated HMAC-SHA256
};

struct __attribute__((packed)) RejectMsg {
    WireHeader header;
    uint64_t   proposal_id;
    uint8_t    rejecting_node_id;
    uint8_t    pad[3];
    uint8_t    signature[16]; // truncated HMAC-SHA256
};

// ---------------------------------------------------------------------------

struct Node {
    uint8_t     id;    // index in sorted nodes list — same on every device
    std::string name;  // ESPHome device name (== App.get_name() on that device)
};

struct Proposal {
    std::string          name;
    std::vector<uint8_t> required_nodes;
    size_t               state_size{0};
    std::function<void(const void *)> on_execute_callback{nullptr};
    Trigger<>           *on_timeout_trigger{nullptr};
    std::function<bool(const void *)> accept_if_lambda{nullptr};
};

struct PendingProposal {
    uint64_t           proposal_id;
    std::string        proposal_name;
    std::string        state; // Stores raw bytes of state struct
    std::set<uint8_t>  acked_by;
    uint32_t           created_ms;
};

// ---------------------------------------------------------------------------

class BarrierGroupComponent : public Component {
 public:
    // --- Config setters (called from generated code) ----------------------
    void set_group_id(uint32_t group_id)      { group_id_ = group_id; }
    void set_key(const std::string &key)      { key_ = key; }
    void set_port(uint16_t port)              { port_ = port; }
    void set_proposal_timeout_ms(uint32_t ms) { proposal_timeout_ms_ = ms; }
    void set_multicast_group(uint32_t addr)   { multicast_addr_ = addr; }

    void add_node(uint8_t id, const std::string &name) {
        nodes_.push_back({id, name});
    }
    void add_proposal(const std::string &name,
                      const std::vector<uint8_t> &required_nodes,
                      size_t state_size,
                      std::function<void(const void *)> on_execute_callback,
                      Trigger<> *timeout_trigger,
                      std::function<bool(const void *)> accept_if_lambda) {
        proposals_.push_back({name, required_nodes, state_size, std::move(on_execute_callback), timeout_trigger, std::move(accept_if_lambda)});
    }

    // --- Component lifecycle -----------------------------------------------
    void setup() override;
    void loop() override;
    float get_setup_priority() const override;

    // --- Public API ---------------------------------------------------------
    void propose(const std::string &proposal_name, const void *state_ptr = nullptr, size_t state_size = 0);

 protected:
    uint32_t    group_id_{0};
    std::string key_;
    uint32_t    peer_last_seq_[256];
    uint8_t     my_node_id_{255};  // resolved in setup() via App.get_name()
    uint16_t    port_{6543};
    uint32_t    proposal_timeout_ms_{2000};
    uint32_t    multicast_addr_{0};
    uint32_t    seq_{0};

    std::vector<Node>            nodes_;
    std::vector<Proposal>        proposals_;
    std::vector<PendingProposal> pending_;
    std::set<uint64_t>           seen_proposals_;

#ifdef USE_ESP8266
    WiFiUDP udp_;
#else
    int sock_{-1};
#endif


    // --- Internal -----------------------------------------------------------
    void recv_udp_();
    void handle_propose_(const uint8_t *buf, size_t len);
    void handle_ack_(const AckMsg &msg);
    void handle_reject_(const RejectMsg &msg);
    void send_multicast_(const void *data, size_t len);
    void check_unanimous_(PendingProposal &p);
    void check_timeouts_();

    Proposal *find_proposal_(const std::string &name);
    void compute_signature_(const void *data, size_t len, uint8_t *sig_out);
    bool verify_signature_(const void *data, size_t len, const uint8_t *sig_expected);
};

// ---------------------------------------------------------------------------
// Triggers: templated for compile-time type-safety
// ---------------------------------------------------------------------------
template<typename T>
class BarrierGroupOnExecuteTrigger : public Trigger<const T &> {
 public:
    explicit BarrierGroupOnExecuteTrigger() {}
};

template<>
class BarrierGroupOnExecuteTrigger<void> : public Trigger<> {
 public:
    explicit BarrierGroupOnExecuteTrigger() {}
};

class BarrierGroupOnTimeoutTrigger : public Trigger<> {
 public:
    explicit BarrierGroupOnTimeoutTrigger() {}
};

}  // namespace barrier_group
}  // namespace esphome
