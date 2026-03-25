#pragma once

#include "esphome/core/component.h"
#include "esphome/components/microphone/microphone.h"
#include "driver/i2s_pdm.h"

#include <cstdio>
#include <string>
#include <vector>

namespace esphome {
namespace waveshare_mic {

// WaveshareMic implements both esphome::Component (setup/loop lifecycle)
// and esphome::microphone::Microphone so it can be used directly in
// voice_assistant, on_data callbacks, etc.
//
// Two capture modes co-exist:
//   1. SD recording  — start_recording() / stop_recording().  Audio is
//      written to a WAV file on the SD card.
//   2. Microphone interface — start() / stop() from the voice_assistant or
//      any component that references this as a Microphone.  Audio is
//      delivered via the data_callbacks_ (push) and read() (poll).
//
// Both modes can run simultaneously: when both are active, the audio
// captured in each loop() tick is written to the SD file AND dispatched
// to the callbacks.

class WaveshareMic : public Component, public microphone::Microphone {
 public:
  void setup() override;
  void loop()  override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // ---------------------------------------------------------- setters (YAML)
  void set_pdm_clock_pin(int pin)             { this->pdm_clock_pin_ = pin;   }
  void set_pdm_data_pin(int pin)              { this->pdm_data_pin_  = pin;   }
  void set_sample_rate(uint32_t sr)           { this->sample_rate_   = sr;    }
  void set_default_path(const std::string &p) { this->default_path_  = p;     }
  void set_buffer_bytes(size_t b)             { this->buffer_bytes_  = b;     }

  // ----------------------------------------------- Microphone interface (ESPHome)
  // start() enables the microphone for the voice_assistant / on_data path.
  // Flushes stale PDM DMA data so the first buffer is clean.
  void start() override;

  // stop() disables the microphone interface path.  Does NOT stop SD recording
  // if start_recording() is also active.
  void stop() override;

  // Synchronous read — returns number of int16_t samples written to buf.
  // Called by voice_assistant in its processing task.
  size_t read(int16_t *buf, size_t len) override;

  // --------------------------------------------------- SD recording API
  bool     start_recording(const std::string &path = "");
  void     stop_recording();
  bool     is_recording() const { return this->recording_; }
  uint32_t recording_ms()  const;

 protected:
  bool init_i2s_();
  bool open_file_(const std::string &path);
  void close_file_();
  bool write_wav_header_placeholder_();
  void finalize_wav_header_();

  int      pdm_clock_pin_{45};
  int      pdm_data_pin_{46};
  uint32_t sample_rate_{16000};

  std::string default_path_{"/sdcard/recording.wav"};
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