#include "barrier_group.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include <fcntl.h>
#include <errno.h>
#include <mbedtls/md.h>

namespace esphome {
namespace barrier_group {

static const char *TAG = "barrier_group";

// ---------------------------------------------------------------------------
// Component lifecycle
// ---------------------------------------------------------------------------

float BarrierGroupComponent::get_setup_priority() const {
    return setup_priority::AFTER_WIFI;
}

void BarrierGroupComponent::setup() {
    memset(peer_last_seq_, 0, sizeof(peer_last_seq_));
    // ── Identify self ───────────────────────────────────────────────────────
    // Nodes are sorted alphabetically at codegen; their index is their ID.
    // We find ourselves by matching App.get_name() to the nodes list.
    const std::string my_name = App.get_name();
    for (const auto &n : nodes_) {
        if (n.name == my_name) {
            my_node_id_ = n.id;
            break;
        }
    }
    if (my_node_id_ == 255) {
        ESP_LOGE(TAG, "This device ('%s') is not listed in nodes — check your config",
                 my_name.c_str());
        mark_failed();
        return;
    }

    // ── Create UDP socket ────────────────────────────────────────────────────
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        mark_failed();
        return;
    }

    // Reuse address so we can rebind after a reboot without TIME_WAIT issues.
    int yes = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Non-blocking — loop() must never stall.
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

    // Bind to any interface on our port.
    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);
    if (bind(sock_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        mark_failed();
        return;
    }

    // ── Join multicast group ─────────────────────────────────────────────────
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = htonl(multicast_addr_);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        ESP_LOGE(TAG, "IP_ADD_MEMBERSHIP failed: %d", errno);
        mark_failed();
        return;
    }

    // TTL=1: LAN only, packets don't leave the subnet.
    uint8_t ttl = 1;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    ESP_LOGI(TAG, "[%08X] ready  name='%s'  node_id=%d  port=%d  multicast=%d.%d.%d.%d  "
                  "nodes=%zu  commands=%zu",
             group_id_, my_name.c_str(), my_node_id_, port_,
             (multicast_addr_ >> 24) & 0xFF, (multicast_addr_ >> 16) & 0xFF,
             (multicast_addr_ >>  8) & 0xFF, (multicast_addr_      ) & 0xFF,
             nodes_.size(), commands_.size());
}

void BarrierGroupComponent::loop() {
    recv_udp_();
    check_timeouts_();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BarrierGroupComponent::propose(const std::string &command_name) {
    if (my_node_id_ == 255) return;  // setup failed

    Command *cmd = find_command_(command_name);
    if (!cmd) {
        ESP_LOGW(TAG, "[%08X] propose: unknown command '%s'", group_id_, command_name.c_str());
        return;
    }

    // Unique proposal id: top 8 bits = my node id, remaining 56 = counter.
    uint64_t proposal_id =
        (static_cast<uint64_t>(my_node_id_) << 56) |
        (static_cast<uint64_t>(++seq_) & 0x00FFFFFFFFFFFFFFull);

    ESP_LOGD(TAG, "[%08X] propose '%s'  id=%llu", group_id_, command_name.c_str(), proposal_id);

    // Record locally, self-ACK.
    PendingProposal p;
    p.proposal_id  = proposal_id;
    p.command_name = command_name;
    p.created_ms   = millis();
    p.acked_by.insert(my_node_id_);
    pending_.push_back(std::move(p));
    seen_proposals_.insert(proposal_id);

    // Flood PROPOSE + our ACK to the multicast group.
    ProposeMsg pmsg{};
    pmsg.header.magic     = MAGIC;
    pmsg.header.group_id  = group_id_;
    pmsg.header.type      = MSG_PROPOSE;
    pmsg.header.sender_id = my_node_id_;
    pmsg.proposal_id      = proposal_id;
    strncpy(pmsg.command, command_name.c_str(), sizeof(pmsg.command) - 1);
    if (!key_.empty()) {
        compute_signature_(&pmsg, sizeof(pmsg) - 16, pmsg.signature);
    } else {
        memset(pmsg.signature, 0, 16);
    }
    send_multicast_(&pmsg, sizeof(pmsg));

    AckMsg amsg{};
    amsg.header.magic     = MAGIC;
    amsg.header.group_id  = group_id_;
    amsg.header.type      = MSG_ACK;
    amsg.header.sender_id = my_node_id_;
    amsg.proposal_id      = proposal_id;
    amsg.acking_node_id   = my_node_id_;
    if (!key_.empty()) {
        compute_signature_(&amsg, sizeof(amsg) - 16, amsg.signature);
    } else {
        memset(amsg.signature, 0, 16);
    }
    send_multicast_(&amsg, sizeof(amsg));

    // May already be unanimous if this node is the only required node.
    check_unanimous_(pending_.back());
}

// ---------------------------------------------------------------------------
// UDP receive
// ---------------------------------------------------------------------------

void BarrierGroupComponent::recv_udp_() {
    uint8_t buf[sizeof(ProposeMsg)];
    struct sockaddr_in src{};
    socklen_t src_len = sizeof(src);

    int len = recvfrom(sock_, buf, sizeof(buf), 0,
                       reinterpret_cast<struct sockaddr *>(&src), &src_len);
    if (len < 0) return;  // EAGAIN / EWOULDBLOCK — nothing waiting

    if (len < static_cast<int>(sizeof(WireHeader))) {
        ESP_LOGV(TAG, "[%08X] Dropped packet: too short (%d bytes)", group_id_, len);
        return;
    }
    auto *hdr = reinterpret_cast<WireHeader *>(buf);
    if (hdr->magic != MAGIC) {
        ESP_LOGV(TAG, "[%08X] Dropped packet: bad magic (0x%08X)", group_id_, hdr->magic);
        return;
    }
    if (hdr->group_id != group_id_) {
        ESP_LOGV(TAG, "[%08X] Dropped packet: mismatched group_id (0x%08X)", group_id_, hdr->group_id);
        return;
    }
    if (hdr->sender_id == my_node_id_) {
        ESP_LOGV(TAG, "[%08X] Dropped packet: own echo", group_id_);
        return;
    }

    switch (hdr->type) {
        case MSG_PROPOSE:
            if (len >= static_cast<int>(sizeof(ProposeMsg))) {
                auto &msg = *reinterpret_cast<ProposeMsg *>(buf);
                if (!key_.empty() && !verify_signature_(&msg, sizeof(msg) - 16, msg.signature)) {
                    ESP_LOGW(TAG, "[%08X] Invalid PROPOSE signature from node %d", group_id_, hdr->sender_id);
                    return;
                }
                handle_propose_(msg);
            } else {
                ESP_LOGV(TAG, "[%08X] Dropped PROPOSE: packet too short (%d bytes)", group_id_, len);
            }
            break;
        case MSG_ACK:
            if (len >= static_cast<int>(sizeof(AckMsg))) {
                auto &msg = *reinterpret_cast<AckMsg *>(buf);
                if (!key_.empty() && !verify_signature_(&msg, sizeof(msg) - 16, msg.signature)) {
                    ESP_LOGW(TAG, "[%08X] Invalid ACK signature from node %d", group_id_, hdr->sender_id);
                    return;
                }
                handle_ack_(msg);
            } else {
                ESP_LOGV(TAG, "[%08X] Dropped ACK: packet too short (%d bytes)", group_id_, len);
            }
            break;
        default:
            ESP_LOGW(TAG, "[%08X] unknown msg type %d", group_id_, hdr->type);
    }
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

void BarrierGroupComponent::handle_propose_(const ProposeMsg &msg) {
    if (seen_proposals_.count(msg.proposal_id)) return;  // dedup
    seen_proposals_.insert(msg.proposal_id);

    uint8_t sender_id = msg.header.sender_id;
    uint64_t seq = msg.proposal_id & 0x00FFFFFFFFFFFFFFull;
    if (!key_.empty()) {
        if (seq <= peer_last_seq_[sender_id]) {
            ESP_LOGW(TAG, "[%08X] Replay attack detected from node %d (seq %llu <= %llu)",
                     group_id_, sender_id, seq, peer_last_seq_[sender_id]);
            return;
        }
        peer_last_seq_[sender_id] = seq;
    }

    std::string cmd_name(msg.command, strnlen(msg.command, sizeof(msg.command)));
    Command *cmd = find_command_(cmd_name);
    if (!cmd) {
        ESP_LOGW(TAG, "[%08X] PROPOSE for unknown command '%s'", group_id_, cmd_name.c_str());
        return;
    }

    // Ignore if we're not in this command's required_nodes.
    bool in_required = false;
    for (uint8_t id : cmd->required_nodes)
        if (id == my_node_id_) { in_required = true; break; }
    if (!in_required) return;

    ESP_LOGD(TAG, "[%08X] PROPOSE '%s' id=%llu from node %d",
             group_id_, cmd_name.c_str(), msg.proposal_id, msg.header.sender_id);

    // Record and self-ACK.
    PendingProposal p;
    p.proposal_id  = msg.proposal_id;
    p.command_name = cmd_name;
    p.created_ms   = millis();
    p.acked_by.insert(my_node_id_);
    pending_.push_back(std::move(p));

    // Broadcast our ACK to the multicast group.
    AckMsg amsg{};
    amsg.header.magic     = MAGIC;
    amsg.header.group_id  = group_id_;
    amsg.header.type      = MSG_ACK;
    amsg.header.sender_id = my_node_id_;
    amsg.proposal_id      = msg.proposal_id;
    amsg.acking_node_id   = my_node_id_;
    if (!key_.empty()) {
        compute_signature_(&amsg, sizeof(amsg) - 16, amsg.signature);
    } else {
        memset(amsg.signature, 0, 16);
    }
    send_multicast_(&amsg, sizeof(amsg));

    check_unanimous_(pending_.back());
}

void BarrierGroupComponent::handle_ack_(const AckMsg &msg) {
    for (auto &p : pending_) {
        if (p.proposal_id != msg.proposal_id) continue;

        p.acked_by.insert(msg.acking_node_id);

        Command *cmd = find_command_(p.command_name);
        size_t total = cmd ? cmd->required_nodes.size() : 0;
        ESP_LOGD(TAG, "[%08X] ACK node %d  proposal=%llu  acked=%zu/%zu",
                 group_id_, msg.acking_node_id, msg.proposal_id, p.acked_by.size(), total);

        check_unanimous_(p);
        return;
    }
    ESP_LOGD(TAG, "[%08X] ACK for unknown proposal %llu (race or already executed)", group_id_, msg.proposal_id);
}

// ---------------------------------------------------------------------------
// Unanimous check
// ---------------------------------------------------------------------------

void BarrierGroupComponent::check_unanimous_(PendingProposal &p) {
    Command *cmd = find_command_(p.command_name);
    if (!cmd) return;

    for (uint8_t req_id : cmd->required_nodes)
        if (!p.acked_by.count(req_id)) return;  // still waiting

    ESP_LOGD(TAG, "[%08X] unanimous! executing '%s'", group_id_, p.command_name.c_str());
    if (cmd->on_execute_trigger)
        cmd->on_execute_trigger->trigger();

    // Remove from pending; erase from seen so the ID can't block future proposals.
    uint64_t id = p.proposal_id;
    seen_proposals_.erase(id);
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
                       [id](const PendingProposal &q) { return q.proposal_id == id; }),
        pending_.end());
}

// ---------------------------------------------------------------------------
// Timeout sweep
// ---------------------------------------------------------------------------

void BarrierGroupComponent::check_timeouts_() {
    uint32_t now = millis();
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
                       [&](const PendingProposal &p) {
                           if (now - p.created_ms < proposal_timeout_ms_) return false;
                           ESP_LOGW(TAG, "[%08X] proposal '%s' id=%llu timed out (acked %zu nodes)",
                                    group_id_, p.command_name.c_str(), p.proposal_id, p.acked_by.size());
                           seen_proposals_.erase(p.proposal_id);
                           return true;
                       }),
        pending_.end());
}

// ---------------------------------------------------------------------------
// Multicast send
// ---------------------------------------------------------------------------

void BarrierGroupComponent::send_multicast_(const void *data, size_t len) {
    struct sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_addr.s_addr = htonl(multicast_addr_);
    dest.sin_port        = htons(port_);
    sendto(sock_, data, len, 0,
           reinterpret_cast<struct sockaddr *>(&dest), sizeof(dest));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

Command *BarrierGroupComponent::find_command_(const std::string &name) {
    for (auto &c : commands_)
        if (c.name == name) return &c;
    return nullptr;
}

void BarrierGroupComponent::compute_signature_(const void *data, size_t len, uint8_t *sig_out) {
    uint8_t hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, reinterpret_cast<const uint8_t *>(key_.c_str()), key_.size());
    mbedtls_md_hmac_update(&ctx, reinterpret_cast<const uint8_t *>(data), len);
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);
    memcpy(sig_out, hmac, 16); // Truncate to 16 bytes
}

bool BarrierGroupComponent::verify_signature_(const void *data, size_t len, const uint8_t *sig_expected) {
    uint8_t sig_actual[16];
    compute_signature_(data, len, sig_actual);
    // Constant-time comparison to prevent timing attacks
    uint8_t diff = 0;
    for (size_t i = 0; i < 16; i++) {
        diff |= sig_actual[i] ^ sig_expected[i];
    }
    return diff == 0;
}

}  // namespace barrier_group
}  // namespace esphome
