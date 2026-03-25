#include "waveshare_audio.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace waveshare_audio {

static const char *const TAG = "waveshare_audio";

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void WaveshareAudio::setup() {
  // Drive PCM5100A XSMT pin HIGH initially so gpio_config records it as an
  // output, then immediately pull it LOW to assert hardware mute.
  // The DAC will only be unmuted just before the first playback starts.
  gpio_config_t gpio_conf = {};
  gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
  gpio_conf.intr_type     = GPIO_INTR_DISABLE;
  gpio_conf.pin_bit_mask  = (1ULL << static_cast<uint32_t>(this->enable_pin_));
  gpio_conf.mode          = GPIO_MODE_OUTPUT;
  gpio_config(&gpio_conf);
  gpio_set_level(static_cast<gpio_num_t>(this->enable_pin_), 0);  // muted
  this->dac_muted_ = true;

  // Pre-allocate gain-scaling buffer in SPIRAM so write_pcm_() never
  // touches the internal heap allocator at audio rate.
  this->scale_buf_ = static_cast<int16_t *>(
      heap_caps_malloc(SCALE_BUF_SAMPLES * sizeof(int16_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->scale_buf_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate scale buffer (%u bytes)",
             static_cast<unsigned>(SCALE_BUF_SAMPLES * sizeof(int16_t)));
    this->mark_failed();
    return;
  }

  if (!this->init_i2s_()) {
    heap_caps_free(this->scale_buf_);
    this->scale_buf_ = nullptr;
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Audio output ready (rate=%u Hz) — DAC muted, channel idle",
           this->sample_rate_);
}

// ---------------------------------------------------------------------------
// I2S init — channel starts DISABLED so PCM5100A auto-mutes on boot
// ---------------------------------------------------------------------------

bool WaveshareAudio::init_i2s_() {
  i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&tx_chan_cfg, &this->tx_chan_, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
    return false;
  }

  i2s_std_config_t tx_std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(this->sample_rate_),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = static_cast<gpio_num_t>(this->bclk_pin_),
          .ws   = static_cast<gpio_num_t>(this->ws_pin_),
          .dout = static_cast<gpio_num_t>(this->dout_pin_),
          .din  = I2S_GPIO_UNUSED,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv   = false,
          },
      },
  };

  err = i2s_channel_init_std_mode(this->tx_chan_, &tx_std_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
    return false;
  }

  // NOTE: intentionally NOT calling i2s_channel_enable() here.
  // The channel starts disabled; PCM5100A receives no clock and auto-mutes.
  // enable_channel_() is called only when playback actually begins.
  this->channel_enabled_     = false;
  this->current_sample_rate_ = this->sample_rate_;
  this->current_slot_mode_   = I2S_SLOT_MODE_MONO;
  this->ready_               = true;
  return true;
}

// ---------------------------------------------------------------------------
// Channel enable / disable helpers
// ---------------------------------------------------------------------------

bool WaveshareAudio::enable_channel_() {
  if (this->channel_enabled_)
    return true;
  // Unmute the PCM5100A (XSMT HIGH) before starting the I2S clock.
  // The chip ramps up from mute over ~1034 LRCK cycles (~65 ms at 16 kHz)
  // so the first moments of audio fade in naturally — no pop.
  if (this->dac_muted_) {
    gpio_set_level(static_cast<gpio_num_t>(this->enable_pin_), 1);
    this->dac_muted_ = false;
  }
  esp_err_t err = i2s_channel_enable(this->tx_chan_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
    // Re-mute if enable failed so we don't leave DAC unmuted with no clock.
    gpio_set_level(static_cast<gpio_num_t>(this->enable_pin_), 0);
    this->dac_muted_ = true;
    return false;
  }
  this->channel_enabled_ = true;
  return true;
}

void WaveshareAudio::disable_channel_() {
  if (!this->channel_enabled_)
    return;
  i2s_channel_disable(this->tx_chan_);
  this->channel_enabled_ = false;
  // Assert hardware mute AFTER disabling the I2S peripheral.
  // i2s_channel_disable() can leave BCLK stuck HIGH electrically.
  // Without this the PCM5100A sees a frozen clock as a "paused bus" and
  // keeps buzzing.  Driving enable_pin_ LOW silences it unconditionally.
  gpio_set_level(static_cast<gpio_num_t>(this->enable_pin_), 0);
  this->dac_muted_ = true;
}

// ---------------------------------------------------------------------------
// Reconfigure helpers — fix for "plays too fast" and stereo/mono mismatch
//
// Both helpers manage the disable/enable cycle internally so callers do not
// need to track channel state.
// ---------------------------------------------------------------------------

bool WaveshareAudio::reconfigure_clock_if_needed_(uint32_t target_rate) {
  if (target_rate == this->current_sample_rate_)
    return true;

  ESP_LOGI(TAG, "Reconfiguring I2S clock: %u Hz -> %u Hz",
           this->current_sample_rate_, target_rate);

  // Channel must be disabled before reconfiguring.
  bool was_enabled = this->channel_enabled_;
  this->disable_channel_();

  i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(target_rate);
  esp_err_t err = i2s_channel_reconfig_std_clock(this->tx_chan_, &clk_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_reconfig_std_clock failed: %s", esp_err_to_name(err));
    // Attempt to restore previous state.
    if (was_enabled) this->enable_channel_();
    return false;
  }

  this->current_sample_rate_ = target_rate;

  if (was_enabled)
    return this->enable_channel_();
  return true;
}

bool WaveshareAudio::reconfigure_slot_if_needed_(uint16_t num_channels) {
  i2s_slot_mode_t target = (num_channels >= 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
  if (target == this->current_slot_mode_)
    return true;

  ESP_LOGI(TAG, "Reconfiguring I2S slot mode -> %s",
           (target == I2S_SLOT_MODE_STEREO) ? "stereo" : "mono");

  bool was_enabled = this->channel_enabled_;
  this->disable_channel_();

  i2s_std_slot_config_t slot_cfg =
      I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, target);
  esp_err_t err = i2s_channel_reconfig_std_slot(this->tx_chan_, &slot_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_reconfig_std_slot failed: %s", esp_err_to_name(err));
    if (was_enabled) this->enable_channel_();
    return false;
  }

  this->current_slot_mode_ = target;

  if (was_enabled)
    return this->enable_channel_();
  return true;
}

// ---------------------------------------------------------------------------
// WAV header parser
// ---------------------------------------------------------------------------

WaveshareAudio::WavHeader WaveshareAudio::read_wav_header_(FILE *fp) {
  WavHeader hdr;
  uint8_t buf[44];

  fseek(fp, 0, SEEK_SET);
  if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
    ESP_LOGW(TAG, "Could not read full 44-byte WAV header");
    return hdr;  // valid = false
  }

  // Validate RIFF/WAVE magic bytes.
  if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F' ||
      buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E') {
    ESP_LOGW(TAG, "Not a valid RIFF/WAVE file");
    return hdr;
  }

  hdr.num_channels    = static_cast<uint16_t>(buf[22] | (buf[23] << 8));
  hdr.sample_rate     = static_cast<uint32_t>(
      buf[24] | (buf[25] << 8) | (buf[26] << 16) | (buf[27] << 24));
  hdr.bits_per_sample = static_cast<uint16_t>(buf[34] | (buf[35] << 8));

  hdr.valid = (hdr.num_channels >= 1 && hdr.num_channels <= 2 &&
               hdr.bits_per_sample == 16 &&
               hdr.sample_rate >= 8000 && hdr.sample_rate <= 48000);

  if (!hdr.valid) {
    ESP_LOGW(TAG, "Unsupported WAV format: %u ch, %u-bit, %u Hz — "
             "falling back to configured defaults",
             hdr.num_channels, hdr.bits_per_sample, hdr.sample_rate);
  }
  // After fread of 44 bytes the file pointer is at offset 44 — audio data start.
  return hdr;
}

// ---------------------------------------------------------------------------
// Core audio I/O
// ---------------------------------------------------------------------------

bool WaveshareAudio::write_pcm_(const int16_t *pcm, size_t bytes) {
  if (!this->ready_ || this->stop_requested_)
    return false;

  size_t n = std::min(bytes / sizeof(int16_t), SCALE_BUF_SAMPLES);
  float g  = this->gain_;
  for (size_t i = 0; i < n; i++)
    this->scale_buf_[i] = static_cast<int16_t>(pcm[i] * g);

  size_t write_bytes   = n * sizeof(int16_t);
  size_t total_written = 0;

  while (total_written < write_bytes && !this->stop_requested_) {
    size_t just_written = 0;
    const auto *ptr     = reinterpret_cast<const uint8_t *>(this->scale_buf_) + total_written;
    esp_err_t err = i2s_channel_write(this->tx_chan_, ptr, write_bytes - total_written,
                                      &just_written, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "i2s write failed (%s)", esp_err_to_name(err));
      return false;
    }
    if (just_written == 0)
      continue;
    total_written += just_written;
  }

  return total_written == write_bytes && !this->stop_requested_;
}

void WaveshareAudio::write_silence_(uint32_t duration_ms) {
  int16_t zeros[256] = {};
  uint32_t sample_count = (this->current_sample_rate_ * duration_ms) / 1000;
  uint32_t sent = 0;
  while (sent < sample_count && !this->stop_requested_) {
    size_t chunk = std::min<size_t>(256, sample_count - sent);
    size_t bytes_written = 0;
    i2s_channel_write(this->tx_chan_, zeros, chunk * sizeof(int16_t),
                      &bytes_written, pdMS_TO_TICKS(20));
    sent += chunk;
  }
}

// ---------------------------------------------------------------------------
// File playback
// ---------------------------------------------------------------------------

bool WaveshareAudio::play_file_blocking_(const std::string &path) {
  FILE *fp = fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    ESP_LOGE(TAG, "Unable to open file: %s", path.c_str());
    return false;
  }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);

  WavHeader hdr = this->read_wav_header_(fp);
  if (!hdr.valid) {
    // Fall back to YAML-configured defaults and skip the 44-byte header.
    hdr.num_channels = 1;
    hdr.sample_rate  = this->sample_rate_;
    fseek(fp, 44, SEEK_SET);
  }

  ESP_LOGI(TAG, "Playing %s (%ld bytes, %u ch, %u Hz, %u-bit)",
           path.c_str(), file_size, hdr.num_channels,
           hdr.sample_rate, hdr.bits_per_sample);

  // Apply the WAV file's sample rate to the I2S hardware clock BEFORE
  // enabling the channel.  This is the fix for "plays too fast": a mismatch
  // between the file's rate and the active hardware clock stretches or
  // compresses the audio in time.
  if (!this->reconfigure_clock_if_needed_(hdr.sample_rate)) {
    ESP_LOGE(TAG, "Failed to reconfigure I2S clock for %u Hz", hdr.sample_rate);
    fclose(fp);
    return false;
  }

  if (!this->reconfigure_slot_if_needed_(hdr.num_channels)) {
    ESP_LOGE(TAG, "Failed to configure I2S slot mode");
    fclose(fp);
    return false;
  }

  // Enable the channel just before we start writing — PCM5100A wakes from
  // auto-mute when the I2S clock resumes.
  if (!this->enable_channel_()) {
    fclose(fp);
    return false;
  }

  int16_t block[1024];
  bool ok = true;

  while (!this->stop_requested_) {
    size_t read = fread(block, 1, sizeof(block), fp);
    if (read == 0)
      break;
    if (!this->write_pcm_(block, read)) {
      ok = false;
      break;
    }
  }
  fclose(fp);

  if (!ok)
    ESP_LOGW(TAG, "Playback failed before EOF for %s", path.c_str());

  return ok && !this->stop_requested_;
}

// ---------------------------------------------------------------------------
// Buzz / tone generation
// ---------------------------------------------------------------------------

bool WaveshareAudio::play_sine_(float freq_hz, uint32_t duration_ms) {
  if (!this->ready_) return false;

  const float    amplitude    = 28000.0f;
  const uint32_t sample_count = (this->current_sample_rate_ * duration_ms) / 1000;
  const uint32_t fade_samples = std::max<uint32_t>(1, this->current_sample_rate_ / 200);
  int16_t        samples[256];

  uint32_t generated = 0;
  while (generated < sample_count && !this->stop_requested_) {
    size_t to_gen = std::min<size_t>(256, sample_count - generated);
    for (size_t i = 0; i < to_gen; i++) {
      uint32_t idx = generated + i;
      float t   = static_cast<float>(idx) / static_cast<float>(this->current_sample_rate_);
      float env = 1.0f;
      if (idx < fade_samples)
        env = static_cast<float>(idx) / static_cast<float>(fade_samples);
      else if (idx + fade_samples > sample_count)
        env = static_cast<float>(sample_count - idx) / static_cast<float>(fade_samples);
      samples[i] = (freq_hz > 0.0f)
          ? static_cast<int16_t>(amplitude * env *
                sinf(2.0f * static_cast<float>(M_PI) * freq_hz * t))
          : 0;
    }
    if (!this->write_pcm_(samples, to_gen * sizeof(int16_t)))
      return false;
    generated += to_gen;
  }
  return !this->stop_requested_;
}

bool WaveshareAudio::play_buzz_blocking_(BuzzPattern pattern) {
  if (!this->ready_) return false;

  // Buzz patterns are mono sine waves — restore mono and YAML sample rate,
  // then enable the channel.
  if (!this->reconfigure_clock_if_needed_(this->sample_rate_)) return false;
  if (!this->reconfigure_slot_if_needed_(1)) return false;
  if (!this->enable_channel_()) return false;

  switch (pattern) {
    case BUZZ_SINE:
      return this->play_sine_(880.0f, 250);
    case BUZZ_DOUBLE_BEEP:
      return this->play_sine_(660.0f, 120) &&
             this->play_sine_(0.0f, 70)    &&
             this->play_sine_(660.0f, 120);
    case BUZZ_ALARM:
      for (int i = 0; i < 3; i++) {
        if (!this->play_sine_(1200.0f, 140) || !this->play_sine_(700.0f, 140))
          return false;
      }
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Playback task
// ---------------------------------------------------------------------------

void WaveshareAudio::playback_task_trampoline_(void *arg) {
  auto *self = static_cast<WaveshareAudio *>(arg);
  bool ok    = true;

  if (self->playback_mode_ == PLAYBACK_BUZZ)
    ok = self->play_buzz_blocking_(self->pending_buzz_);
  else if (self->playback_mode_ == PLAYBACK_FILE)
    ok = self->play_file_blocking_(self->pending_file_);

  if (!ok && !self->stop_requested_)
    ESP_LOGW(TAG, "Playback ended with error");

  // Flush DMA before disabling so the DAC does not click.
  self->write_silence_(40);

  // Disable the I2S channel — PCM5100A auto-mutes, eliminating idle buzz.
  self->disable_channel_();

  // IMPORTANT: null playback_task_ BEFORE vTaskDelete(nullptr) to avoid the
  // main core reading a dangling handle between task deletion and pointer clear.
  self->stop_requested_ = false;
  self->playback_mode_  = PLAYBACK_NONE;
  self->playback_task_  = nullptr;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool WaveshareAudio::play_file(const std::string &path) {
  if (!this->ready_) return false;
  if (this->playback_task_ != nullptr) {
    ESP_LOGW(TAG, "Playback already running; stop first");
    return false;
  }

  this->pending_file_   = path.empty() ? this->default_file_ : path;
  this->stop_requested_ = false;
  this->playback_mode_  = PLAYBACK_FILE;

  BaseType_t r = xTaskCreate(WaveshareAudio::playback_task_trampoline_,
                              "ws_audio_play", 8192, this, 4,
                              &this->playback_task_);
  if (r != pdPASS) {
    this->playback_task_ = nullptr;
    ESP_LOGE(TAG, "Failed to create playback task");
    return false;
  }
  return true;
}

bool WaveshareAudio::play_buzz(BuzzPattern pattern) {
  if (!this->ready_) return false;
  if (this->playback_task_ != nullptr) {
    ESP_LOGW(TAG, "Playback already running; stop first");
    return false;
  }

  this->pending_buzz_   = pattern;
  this->stop_requested_ = false;
  this->playback_mode_  = PLAYBACK_BUZZ;

  BaseType_t r = xTaskCreate(WaveshareAudio::playback_task_trampoline_,
                              "ws_audio_buzz", 8192, this, 4,
                              &this->playback_task_);
  if (r != pdPASS) {
    this->playback_task_ = nullptr;
    ESP_LOGE(TAG, "Failed to create buzz task");
    return false;
  }
  return true;
}

void WaveshareAudio::stop() {
  if (!this->ready_) return;
  this->stop_requested_ = true;
  // Flush then disable — PCM5100A auto-mutes when clock stops.
  if (this->channel_enabled_) {
    this->write_silence_(30);
    this->disable_channel_();
  }
}

}  // namespace waveshare_audio
}  // namespace esphome