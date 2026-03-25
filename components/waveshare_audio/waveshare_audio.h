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
  // Parsed fields from a standard 44-byte PCM WAV header.
  struct WavHeader {
    uint16_t num_channels{1};
    uint32_t sample_rate{44100};
    uint16_t bits_per_sample{16};
    bool valid{false};
  };

  bool init_i2s_();

  // Enables the TX channel if it is currently disabled (no-op when already enabled).
  bool enable_channel_();

  // Disables the TX channel so the PCM5100A loses the I2S clock and enters
  // hardware auto-mute mode.  This is the primary fix for idle buzzing.
  void disable_channel_();

  // Reconfigures the I2S TX clock rate when target_rate differs from the
  // currently active rate.  Handles the required disable/enable cycle.
  // Root cause fix for "plays too fast": the WAV file sample rate is now
  // applied to the hardware clock before each playback starts.
  bool reconfigure_clock_if_needed_(uint32_t target_rate);

  // Reconfigures the I2S TX slot mode when num_channels differs from the
  // currently active mode.  Handles the required disable/enable cycle.
  bool reconfigure_slot_if_needed_(uint16_t num_channels);

  // Reads and validates a standard 44-byte PCM WAV header.
  // On success the file pointer is positioned at offset 44 (audio data start).
  // Returns WavHeader with valid=false on any parse failure.
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
  uint32_t sample_rate_{16000};
  std::string default_file_{"/sdcard/recording.wav"};
  float gain_{0.25f};

  i2s_chan_handle_t tx_chan_{nullptr};
  bool ready_{false};

  // Whether the I2S TX channel is currently enabled.
  // The channel is kept disabled when idle so the PCM5100A eventually
  // auto-mutes once 1034 silence frames have been written before shutdown.
  bool channel_enabled_{false};

  // Active hardware configuration — tracked so reconfigure helpers can skip
  // redundant disable/enable cycles on consecutive files that match.
  uint32_t current_sample_rate_{0};
  i2s_slot_mode_t current_slot_mode_{I2S_SLOT_MODE_MONO};

  // std::atomic<bool> provides cross-core memory ordering on dual-core S3.
  std::atomic<bool> stop_requested_{false};

  TaskHandle_t playback_task_{nullptr};

  // Pre-allocated SPIRAM buffer reused by write_pcm_() for gain scaling.
  // Avoids per-block heap allocation at audio rate.
  int16_t *scale_buf_{nullptr};
  static constexpr size_t SCALE_BUF_SAMPLES = 2048;

  // Pre-allocated SPIRAM read buffer for play_file_blocking_().
  // 16 KB = 8192 samples = ~512 ms at 16 kHz mono.  A large block reduces
  // how often we call fread(), which is the primary cause of I2S DMA underrun:
  // FAT32 SD reads have variable latency (cluster boundary, FAT lookup, SD bus
  // arbitration with the display) and a small read block causes the DMA to
  // starve whenever a read takes longer than the DMA buffer depth.
  int16_t *read_buf_{nullptr};
  static constexpr size_t READ_BUF_SAMPLES = 8192;  // 16 KB

  enum PlaybackMode : uint8_t {
    PLAYBACK_NONE = 0,
    PLAYBACK_FILE = 1,
    PLAYBACK_BUZZ = 2,
  } playback_mode_{PLAYBACK_NONE};

  std::string pending_file_{};
  BuzzPattern pending_buzz_{BUZZ_SINE};
};

}  // namespace waveshare_audio
}  // namespace esphome