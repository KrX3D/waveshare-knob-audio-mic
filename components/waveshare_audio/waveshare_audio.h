#pragma once

#include "esphome/core/component.h"
#include "esphome/components/speaker/speaker.h"
#include "driver/i2s_std.h"

#include <atomic>
#include <string>

namespace esphome {
namespace waveshare_audio {

enum BuzzPattern : uint8_t {
  BUZZ_SINE        = 0,
  BUZZ_DOUBLE_BEEP = 1,
  BUZZ_ALARM       = 2,
};

// WaveshareAudio implements both esphome::Component (for setup/loop lifecycle)
// and esphome::speaker::Speaker (so it can be used as output_speaker for the
// mixer platform, or directly in media_player / voice_assistant).
//
// Two audio paths co-exist:
//   1. Streaming (Speaker interface) — used by media_player, TTS, voice_assistant.
//      The mixer or media pipeline calls start() once, then play() repeatedly,
//      then stop() / finish() when done.
//   2. File playback — play_file() plays a WAV from SD in a FreeRTOS task.
//   3. Buzz patterns — play_buzz() generates tones in a FreeRTOS task.
//
// Paths 2 and 3 are rejected while the Speaker interface is active (state ==
// STATE_RUNNING) to prevent I2S contention.  Call stop() first to release
// the streaming path before triggering file or buzz playback.

class WaveshareAudio : public Component, public speaker::Speaker {
 public:
  // ------------------------------------------------------------------ setup
  void setup() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // ---------------------------------------------------------- setters (YAML)
  void set_bclk_pin(int pin)             { this->bclk_pin_    = pin; }
  void set_ws_pin(int pin)               { this->ws_pin_      = pin; }
  void set_dout_pin(int pin)             { this->dout_pin_    = pin; }
  void set_enable_pin(int pin)           { this->enable_pin_  = pin; }
  void set_sample_rate(uint32_t sr)      { this->sample_rate_ = sr;  }
  void set_default_file(const std::string &p) { this->default_file_ = p; }
  void set_gain(float g)                 { this->gain_.store(g, std::memory_order_relaxed); }

  // ------------------------------------------------- Speaker interface (ESPHome)
  // Called once by the media pipeline / mixer to prepare hardware.
  void start() override;

  // Called when the pipeline has no more data to send (clean end-of-stream).
  void finish() override;

  // Called to stop immediately (e.g. on error or skip).
  void stop() override;

  // Called repeatedly by the mixer / media pipeline with raw PCM-16 bytes.
  // Returns the number of bytes actually consumed.  May be less than length
  // if the I2S DMA is momentarily full.
  size_t play(const uint8_t *data, size_t length) override;

  // Returns true while there is audio queued in the DMA ring buffer.
  bool has_buffered_data() const override;

  // ---------------------------------------------- SD file / buzz playback API
  // play_file() and play_buzz() are rejected with a warning if the Speaker
  // streaming interface is currently active.
  bool play_file(const std::string &path = "");
  bool play_buzz(BuzzPattern pattern);
  void stop_playback();
  bool is_playing() const { return this->playback_task_ != nullptr; }

 protected:
  // ---------------------------------------------------------------- WAV header
  struct WavHeader {
    uint16_t num_channels{1};
    uint32_t sample_rate{44100};
    uint16_t bits_per_sample{16};
    bool     valid{false};
  };

  // -------------------------------------------------------- internal helpers
  bool init_i2s_();
  bool enable_channel_();
  void disable_channel_();
  bool reconfigure_clock_if_needed_(uint32_t target_rate);
  bool reconfigure_slot_if_needed_(uint16_t num_channels);
  WavHeader read_wav_header_(FILE *fp);
  bool write_pcm_(const int16_t *pcm, size_t bytes);
  bool play_sine_(float freq_hz, uint32_t duration_ms);
  bool play_file_blocking_(const std::string &path);
  bool play_buzz_blocking_(BuzzPattern pattern);
  void write_silence_(uint32_t duration_ms);

  static void playback_task_trampoline_(void *arg);

  // ------------------------------------------------------------------ pins
  int bclk_pin_{39};
  int ws_pin_{40};
  int dout_pin_{41};
  int enable_pin_{0};

  // ------------------------------------------------------------------ config
  // sample_rate_ is the YAML-configured rate used for buzz patterns and as
  // the fallback for file playback when no valid WAV header is found.
  // It is also the rate used by the Speaker streaming interface (should match
  // the media_player pipeline rate — typically 48000 Hz).
  uint32_t   sample_rate_{48000};
  std::string default_file_{"/sdcard/recording.wav"};
  std::atomic<float> gain_{0.25f};

  // ------------------------------------------------------------------ I2S
  i2s_chan_handle_t tx_chan_{nullptr};
  bool     ready_{false};
  bool     channel_enabled_{false};
  uint32_t current_sample_rate_{0};
  i2s_slot_mode_t current_slot_mode_{I2S_SLOT_MODE_MONO};

  // -------------------------------------------------- task playback (file/buzz)
  std::atomic<bool> stop_requested_{false};

  TaskHandle_t playback_task_{nullptr};

  enum PlaybackMode : uint8_t {
    PLAYBACK_NONE = 0,
    PLAYBACK_FILE = 1,
    PLAYBACK_BUZZ = 2,
  } playback_mode_{PLAYBACK_NONE};

  std::string  pending_file_{};
  BuzzPattern  pending_buzz_{BUZZ_SINE};

  // ------------------------------------------------------------ SPIRAM buffers
  int16_t *scale_buf_{nullptr};
  static constexpr size_t SCALE_BUF_SAMPLES = 2048;

  int16_t *read_buf_{nullptr};
  static constexpr size_t READ_BUF_SAMPLES  = 8192;  // 16 KB
};

}  // namespace waveshare_audio
}  // namespace esphome