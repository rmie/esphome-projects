#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace esphome {
namespace wifi_ftm_anchor {

static const char *const TAG = "wifi_ftm_anchor";

class WifiFtmAnchor : public Component {
 public:
  void set_offset_cm(int16_t offset_cm) { this->offset_cm_ = offset_cm; }
  void set_mac_sensor(text_sensor::TextSensor *s) { this->mac_sensor_ = s; }

  float get_setup_priority() const override {
    // Delay setup until the network data connection is active (API/MQTT phase)
    return esphome::setup_priority::AFTER_CONNECTION; // Value is 100.0f
  }

  void setup() override {
    this->enable_ftm_responder();
  }

  void loop() override {
    uint32_t now = millis();
    if (!this->ftm_configured_ || (now - this->last_log_ > 5000)) {
      this->last_log_ = now;
      this->enable_ftm_responder();
    }
  }

  void enable_ftm_responder() {
    uint8_t primary = 0;
    wifi_second_chan_t second;
    esp_err_t err = esp_wifi_get_channel(&primary, &second);
    if (err != ESP_OK || primary == 0) {
      ESP_LOGD(TAG, "Wi-Fi channel not assigned yet (err=%d)", err);
      return;
    }

    // 1. Force WIFI_MODE_APSTA
    wifi_mode_t current_mode;
    if (esp_wifi_get_mode(&current_mode) == ESP_OK && current_mode != WIFI_MODE_APSTA) {
      err = esp_wifi_set_mode(WIFI_MODE_APSTA);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_mode(WIFI_MODE_APSTA) failed: %s", esp_err_to_name(err));
        return;
      }
    }

    // 2. Configure AP
    wifi_config_t ap_config = {};
    esp_wifi_get_config(WIFI_IF_AP, &ap_config);

    if (strlen((char *)ap_config.ap.ssid) == 0) {
      strncpy((char *)ap_config.ap.ssid, "FTM_ANCHOR_C3", sizeof(ap_config.ap.ssid) - 1);
      ap_config.ap.ssid_len = strlen("FTM_ANCHOR_C3");
      ap_config.ap.authmode = WIFI_AUTH_OPEN;
      ap_config.ap.max_connection = 4;
    }

    ap_config.ap.channel = primary;
    ap_config.ap.ftm_responder = true;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "esp_wifi_set_config(WIFI_IF_AP) failed: %s", esp_err_to_name(err));
      return;
    }

    if (this->offset_cm_ != 0) {
      esp_wifi_ftm_resp_set_offset(this->offset_cm_);
    }

    uint8_t ap_mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, ap_mac);

    char mac_buf[18];
    snprintf(mac_buf, sizeof(mac_buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             ap_mac[0], ap_mac[1], ap_mac[2], ap_mac[3], ap_mac[4], ap_mac[5]);

    if (!this->ftm_configured_) {
      this->ftm_configured_ = true;
      if (this->mac_sensor_ != nullptr) {
        this->mac_sensor_->publish_state(mac_buf);
      }
    }

    ESP_LOGI(TAG, ">>> FTM ANCHOR RESPONDER ACTIVE! Channel: %d, BSSID: %s <<<", primary, mac_buf);
  }

 protected:
  int16_t offset_cm_{0};
  bool ftm_configured_{false};
  uint32_t last_log_{0};
  text_sensor::TextSensor *mac_sensor_{nullptr};
};

}  // namespace wifi_ftm_anchor
}  // namespace esphome
