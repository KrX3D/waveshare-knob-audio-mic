#pragma once

#include "esphome/core/component.h"
#include "driver/i2s_std.h"

#include <atomic>
#include <string>

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
  // Parsed fields from a WAV file header (standard 44-byte PCM layout).
  struct WavHeader {
    uint16_t num_channels{1};
    uint32_t sample_rate{44100};
    uint16_t bits_per_sample{16};
    bool valid{false};
  };

  bool init_i2s_();
  // Reconfigures the I2S TX channel slot mode only when it differs from the
  // current mode, avoiding redundant disable/enable cycles.
  bool reconfigure_slot_if_needed_(uint16_t num_channels);
  // Reads and validates a standard 44-byte PCM WAV header.
  // Returns a WavHeader with valid=false on any parse failure.
  // On success the file pointer is positioned at the audio data start (offset 44).
  WavHeader read_wav_header_(FILE *fp);
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

  // std::atomic<bool> ensures cross-core memory visibility on dual-core S3.
  // volatile bool alone only prevents compiler optimisation, not CPU reordering.
  std::atomic<bool> stop_requested_{false};

  TaskHandle_t playback_task_{nullptr};

  // Pre-allocated SPIRAM buffer reused by write_pcm_() for gain scaling.
  // Avoids per-block heap allocation at audio rate.
  int16_t *scale_buf_{nullptr};
  static constexpr size_t SCALE_BUF_SAMPLES = 2048;

  // Tracks the active I2S slot mode so reconfigure_slot_if_needed_() can skip
  // redundant reconfigurations when consecutive files have the same channel count.
  i2s_slot_mode_t current_slot_mode_{I2S_SLOT_MODE_MONO};

  enum PlaybackMode : uint8_t { PLAYBACK_NONE = 0, PLAYBACK_FILE = 1, PLAYBACK_BUZZ = 2 } playback_mode_{PLAYBACK_NONE};
  std::string pending_file_{};
  BuzzPattern pending_buzz_{BUZZ_SINE};
};

}  // namespace waveshare_audio
}  // namespace esphome