#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome {
namespace wifi_ftm_rover {

static const char *const TAG = "wifi_ftm_rover";

class WifiFtmRover : public PollingComponent {
 public:
  void set_target_mac(const std::string &mac_str) {
    int values[6];
    if (sscanf(mac_str.c_str(), "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6) {
      for (int i = 0; i < 6; ++i) {
        this->target_mac_[i] = static_cast<uint8_t>(values[i]);
      }
      this->mac_configured_ = true;
    } else {
      ESP_LOGE(TAG, "Invalid MAC address format: %s", mac_str.c_str());
    }
  }

  void set_channel(uint8_t channel) { this->channel_ = channel; }
  void set_frame_count(uint8_t count) { this->frm_count_ = count; }
  void set_distance_sensor(sensor::Sensor *s) { this->distance_sensor_ = s; }
  void set_rtt_sensor(sensor::Sensor *s) { this->rtt_sensor_ = s; }

  void setup() override {
    s_instance_ = this;
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
    }

    err = esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_FTM_REPORT, &WifiFtmRover::ftm_event_handler, this, &this->instance_any_id_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register FTM event handler: %s", esp_err_to_name(err));
    } else {
      this->handler_registered_ = true;
      ESP_LOGI(TAG, "FTM Rover Initiator initialized and event handler registered.");
    }
  }

  void update() override {
    if (!this->mac_configured_) {
      ESP_LOGW(TAG, "Target Anchor MAC not configured.");
      return;
    }

    if (!this->handler_registered_) {
      esp_err_t err = esp_event_handler_instance_register(
          WIFI_EVENT, WIFI_EVENT_FTM_REPORT, &WifiFtmRover::ftm_event_handler, this, &this->instance_any_id_);
      if (err == ESP_OK) {
        this->handler_registered_ = true;
        ESP_LOGI(TAG, "FTM event handler registered successfully on retry.");
      } else {
        ESP_LOGW(TAG, "Waiting for Wi-Fi event loop to be ready: %s", esp_err_to_name(err));
        return;
      }
    }

    // Auto-derive channel from connected router if channel_ == 0
    uint8_t use_channel = this->channel_;
    if (use_channel == 0) {
      uint8_t primary = 0;
      wifi_second_chan_t second;
      if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != 0) {
        use_channel = primary;
      } else {
        use_channel = 1;
      }
    }

    // Only abort if session has been stuck for multiple update ticks
    if (this->ranging_in_progress_) {
      this->stuck_cycles_++;
      if (this->stuck_cycles_ < 3) {
        ESP_LOGD(TAG, "Previous FTM session still ongoing, waiting...");
        return;
      }
      ESP_LOGW(TAG, "FTM session timed out after 3s, resetting session.");
      esp_wifi_ftm_end_session();
      esp_wifi_ftm_get_report(nullptr, 0);
      this->ranging_in_progress_ = false;
      this->stuck_cycles_ = 0;
    }

    wifi_ftm_initiator_cfg_t ftmi_cfg = {};
    memcpy(ftmi_cfg.resp_mac, this->target_mac_, 6);
    ftmi_cfg.channel = use_channel;
    ftmi_cfg.frm_count = this->frm_count_;   // 16, 24, 32, or 64 frames
    ftmi_cfg.burst_period = 2;               // 200ms

    esp_err_t err = esp_wifi_ftm_initiate_session(&ftmi_cfg);
    if (err == ESP_OK) {
      this->ranging_in_progress_ = true;
      this->stuck_cycles_ = 0;
      ESP_LOGD(TAG, "Initiated FTM burst to %02X:%02X:%02X:%02X:%02X:%02X on ch %d",
               this->target_mac_[0], this->target_mac_[1], this->target_mac_[2],
               this->target_mac_[3], this->target_mac_[4], this->target_mac_[5], use_channel);
    } else {
      ESP_LOGW(TAG, "esp_wifi_ftm_initiate_session failed: %s", esp_err_to_name(err));
      esp_wifi_ftm_get_report(nullptr, 0);
    }
  }

  static void ftm_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    auto *self = static_cast<WifiFtmRover *>(arg);
    if (!self) return;

    self->ranging_in_progress_ = false;
    self->stuck_cycles_ = 0;
    auto *report = static_cast<wifi_event_ftm_report_t *>(data);

    if (report->status == FTM_STATUS_SUCCESS) {
      float distance_m = static_cast<float>(report->dist_est) / 100.0f; // cm -> m
      float rtt_ns = static_cast<float>(report->rtt_est);

      ESP_LOGI(TAG, "FTM Succeeded: Distance = %.2f m, RTT = %.1f ns (Frames: %u)",
               distance_m, rtt_ns, report->ftm_report_num_entries);

      if (self->distance_sensor_ != nullptr) self->distance_sensor_->publish_state(distance_m);
      if (self->rtt_sensor_ != nullptr) self->rtt_sensor_->publish_state(rtt_ns);
    } else {
      ESP_LOGW(TAG, "FTM Session finished with failure status: %d", report->status);
    }

    // Always free internal driver report memory after each session
    esp_wifi_ftm_get_report(nullptr, 0);
  }

 protected:
  static WifiFtmRover *s_instance_;
  esp_event_handler_instance_t instance_any_id_{nullptr};
  uint8_t target_mac_[6]{0};
  uint8_t channel_{0}; // 0 = auto-derive from router
  uint8_t frm_count_{32};
  uint8_t stuck_cycles_{0};
  bool mac_configured_{false};
  bool handler_registered_{false};
  bool ranging_in_progress_{false};

  sensor::Sensor *distance_sensor_{nullptr};
  sensor::Sensor *rtt_sensor_{nullptr};
};

inline WifiFtmRover *WifiFtmRover::s_instance_ = nullptr;

}  // namespace wifi_ftm_rover
}  // namespace esphome
