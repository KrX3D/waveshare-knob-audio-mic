#pragma once

#include "esphome/core/component.h"
#include "driver/i2s_pdm.h"

#include <cstdio>
#include <string>

namespace esphome {
namespace waveshare_mic {

class WaveshareMic : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_pdm_clock_pin(int pin)          { this->pdm_clock_pin_ = pin;    }
  void set_pdm_data_pin(int pin)           { this->pdm_data_pin_  = pin;    }
  void set_sample_rate(uint32_t sr)        { this->sample_rate_   = sr;     }
  void set_default_path(const std::string &p) { this->default_path_ = p;   }
  void set_buffer_bytes(size_t b)          { this->buffer_bytes_  = b;      }

  bool     start_recording(const std::string &path = "");
  void     stop_recording();
  bool     is_recording() const { return this->recording_; }
  uint32_t recording_ms() const;

 protected:
  bool init_i2s_();
  bool open_file_(const std::string &path);
  void close_file_();
  bool write_wav_header_placeholder_();
  void finalize_wav_header_();

  int      pdm_clock_pin_{45};
  int      pdm_data_pin_{46};
  uint32_t sample_rate_{16000};

  // Path configured in YAML.  Never mutated at runtime so repeated calls to
  // start_recording() without an explicit path always resolve to this value.
  std::string default_path_{"/sdcard/recording.wav"};

  // Resolved path for the active / most-recent recording.  Separate from
  // default_path_ so start_recording("/sdcard/other.wav") does not silently
  // overwrite the YAML-configured default.
  std::string current_path_{};

  size_t buffer_bytes_{4096};

  bool     i2s_ready_{false};
  bool     recording_{false};
  uint32_t start_ms_{0};
  uint32_t bytes_written_{0};
  uint32_t last_recording_ms_{0};
  uint32_t last_stats_log_ms_{0};

  i2s_chan_handle_t rx_chan_{nullptr};
  FILE     *record_file_{nullptr};
  int16_t  *buffer_{nullptr};
};

}  // namespace waveshare_mic
}  // namespace esphome