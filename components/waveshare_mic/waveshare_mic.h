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

  void set_pdm_clock_pin(int pin) { this->pdm_clock_pin_ = pin; }
  void set_pdm_data_pin(int pin) { this->pdm_data_pin_ = pin; }
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }
  void set_default_path(const std::string &path) { this->default_path_ = path; }
  void set_buffer_bytes(size_t bytes) { this->buffer_bytes_ = bytes; }

  bool start_recording(const std::string &path = "");
  void stop_recording();
  bool is_recording() const { return this->recording_; }
  uint32_t recording_ms() const;

 protected:
  bool init_i2s_();
  bool open_file_(const std::string &path);
  void close_file_();
  bool write_wav_header_placeholder_();
  void finalize_wav_header_();

  int pdm_clock_pin_{45};
  int pdm_data_pin_{46};
  uint32_t sample_rate_{44100};
  std::string default_path_{"/sdcard/recording.wav"};
  size_t buffer_bytes_{4096};

  bool i2s_ready_{false};
  bool recording_{false};
  uint32_t start_ms_{0};
  uint32_t bytes_written_{0};
  uint32_t last_recording_ms_{0};

  i2s_chan_handle_t rx_chan_{nullptr};
  FILE *record_file_{nullptr};
  int16_t *buffer_{nullptr};
};

}  // namespace waveshare_mic
}  // namespace esphome
