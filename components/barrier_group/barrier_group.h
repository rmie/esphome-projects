#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "lwip/sockets.h"
#include <vector>
#include <set>
#include <string>

namespace esphome {
namespace barrier_group {

static constexpr uint32_t MAGIC       = 0x42475250U;  // "BGRP"
static constexpr uint8_t  MSG_PROPOSE = 1;
static constexpr uint8_t  MSG_ACK     = 2;

struct __attribute__((packed)) WireHeader {
    uint32_t magic;
    uint32_t group_id;
    uint8_t  type;
    uint8_t  sender_id;
    uint16_t pad;
};

struct __attribute__((packed)) ProposeMsg {
    WireHeader header;
    uint64_t   proposal_id;
    char       command[32];   // null-terminated, max 31 chars
    uint8_t    signature[16]; // truncated HMAC-SHA256
};

struct __attribute__((packed)) AckMsg {
    WireHeader header;
    uint64_t   proposal_id;
    uint8_t    acking_node_id;
    uint8_t    pad[3];
    uint8_t    signature[16]; // truncated HMAC-SHA256
};

// ---------------------------------------------------------------------------

struct Node {
    uint8_t     id;    // index in sorted nodes list — same on every device
    std::string name;  // ESPHome device name (== App.get_name() on that device)
};

struct Command {
    std::string          name;
    std::vector<uint8_t> required_nodes;
    Trigger<>           *on_execute_trigger{nullptr};
};

struct PendingProposal {
    uint64_t           proposal_id;
    std::string        command_name;
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
    void add_command(const std::string &name,
                     const std::vector<uint8_t> &required_nodes,
                     Trigger<> *trigger) {
        commands_.push_back({name, required_nodes, trigger});
    }

    // --- Component lifecycle -----------------------------------------------
    void setup() override;
    void loop() override;
    float get_setup_priority() const override;

    // --- Public API ---------------------------------------------------------
    void propose(const std::string &command_name);

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
    std::vector<Command>         commands_;
    std::vector<PendingProposal> pending_;
    std::set<uint64_t>           seen_proposals_;

    int sock_{-1};

    // --- Internal -----------------------------------------------------------
    void recv_udp_();
    void handle_propose_(const ProposeMsg &msg);
    void handle_ack_(const AckMsg &msg);
    void send_multicast_(const void *data, size_t len);
    void check_unanimous_(PendingProposal &p);
    void check_timeouts_();

    Command *find_command_(const std::string &name);
    void compute_signature_(const void *data, size_t len, uint8_t *sig_out);
    bool verify_signature_(const void *data, size_t len, const uint8_t *sig_expected);
};

// ---------------------------------------------------------------------------
// Action: barrier_group.propose: COMMAND_NAME
// ---------------------------------------------------------------------------
template<typename... Ts>
class BarrierGroupProposeAction : public Action<Ts...> {
 public:
    BarrierGroupProposeAction(BarrierGroupComponent *parent, std::string command)
        : parent_(parent), command_(std::move(command)) {}
    void play(Ts... x) override { this->parent_->propose(this->command_); }
 private:
    BarrierGroupComponent *parent_;
    std::string            command_;
};

// ---------------------------------------------------------------------------
// Trigger: on_execute (fires on every required node when unanimous)
// ---------------------------------------------------------------------------
class BarrierGroupOnExecuteTrigger : public Trigger<> {
 public:
    explicit BarrierGroupOnExecuteTrigger(BarrierGroupComponent * /*parent*/) {}
};

}  // namespace barrier_group
}  // namespace esphome
