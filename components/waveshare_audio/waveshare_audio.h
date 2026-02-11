#pragma once

#include "esphome/core/component.h"
#include "driver/i2s_std.h"

#include <string>
#include <vector>

namespace esphome {
namespace waveshare_audio {

enum BuzzPattern : uint8_t {
  BUZZ_SINE = 0,
  BUZZ_DOUBLE_BEEP = 1,
  BUZZ_ALARM = 2,
};

class WaveshareAudio : public Component {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_bclk_pin(int pin) { this->bclk_pin_ = pin; }
  void set_ws_pin(int pin) { this->ws_pin_ = pin; }
  void set_dout_pin(int pin) { this->dout_pin_ = pin; }
  void set_enable_pin(int pin) { this->enable_pin_ = pin; }
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }
  void set_default_file(const std::string &path) { this->default_file_ = path; }
  void set_gain(float gain) { this->gain_ = gain; }

  bool play_file(const std::string &path = "");
  bool play_buzz(BuzzPattern pattern);
  void stop();
  bool is_playing() const { return this->playback_task_ != nullptr; }

 protected:
  bool init_i2s_();
  bool write_pcm_(const int16_t *pcm, size_t bytes);
  bool play_sine_(float freq_hz, uint32_t duration_ms);
  bool play_file_blocking_(const std::string &path);
  bool play_buzz_blocking_(BuzzPattern pattern);
  void write_silence_(uint32_t duration_ms);

  static void playback_task_trampoline_(void *arg);

  int bclk_pin_{39};
  int ws_pin_{40};
  int dout_pin_{41};
  int enable_pin_{0};
  uint32_t sample_rate_{44100};
  std::string default_file_{"/sdcard/recording.wav"};
  float gain_{0.25f};

  i2s_chan_handle_t tx_chan_{nullptr};
  bool ready_{false};
  volatile bool stop_requested_{false};
  TaskHandle_t playback_task_{nullptr};

  enum PlaybackMode : uint8_t { PLAYBACK_NONE = 0, PLAYBACK_FILE = 1, PLAYBACK_BUZZ = 2 } playback_mode_{PLAYBACK_NONE};
  std::string pending_file_{};
  BuzzPattern pending_buzz_{BUZZ_SINE};
};

}  // namespace waveshare_audio
}  // namespace esphome
