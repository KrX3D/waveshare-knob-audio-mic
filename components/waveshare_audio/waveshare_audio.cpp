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
  // GPIO0 (enable_pin_) drives the CH445P I2S routing switch HIGH so the S3
  // I2S lines reach the PCM5100A DAC.  Must stay HIGH at all times.
  gpio_config_t gpio_conf = {};
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
  gpio_conf.intr_type    = GPIO_INTR_DISABLE;
  gpio_conf.pin_bit_mask = (1ULL << static_cast<uint32_t>(this->enable_pin_));
  gpio_conf.mode         = GPIO_MODE_OUTPUT;
  gpio_config(&gpio_conf);
  gpio_set_level(static_cast<gpio_num_t>(this->enable_pin_), 1);

  this->scale_buf_ = static_cast<int16_t *>(
      heap_caps_malloc(SCALE_BUF_SAMPLES * sizeof(int16_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!this->scale_buf_) {
    ESP_LOGE(TAG, "Failed to allocate scale buffer");
    this->mark_failed();
    return;
  }

  this->read_buf_ = static_cast<int16_t *>(
      heap_caps_malloc(READ_BUF_SAMPLES * sizeof(int16_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!this->read_buf_) {
    ESP_LOGE(TAG, "Failed to allocate read buffer");
    heap_caps_free(this->scale_buf_);
    this->scale_buf_ = nullptr;
    this->mark_failed();
    return;
  }

  if (!this->init_i2s_()) {
    heap_caps_free(this->read_buf_);
    this->read_buf_ = nullptr;
    heap_caps_free(this->scale_buf_);
    this->scale_buf_ = nullptr;
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Audio ready (rate=%u Hz)", this->sample_rate_);
}

// ---------------------------------------------------------------------------
// I2S init
// ---------------------------------------------------------------------------

bool WaveshareAudio::init_i2s_() {
  i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  tx_chan_cfg.dma_desc_num  = 8;
  tx_chan_cfg.dma_frame_num = 1024;
  esp_err_t err = i2s_new_channel(&tx_chan_cfg, &this->tx_chan_, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
    return false;
  }

  i2s_std_config_t tx_std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(this->sample_rate_),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_STEREO),
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
    ESP_LOGE(TAG, "i2s_channel_init_std_mode: %s", esp_err_to_name(err));
    return false;
  }

  // Channel starts DISABLED — PCM5100A auto-mutes until first playback.
  this->channel_enabled_     = false;
  this->current_sample_rate_ = this->sample_rate_;
  this->current_slot_mode_   = I2S_SLOT_MODE_STEREO;
  this->ready_               = true;
  return true;
}

// ---------------------------------------------------------------------------
// Speaker interface — called by mixer / media pipeline / voice_assistant
// ---------------------------------------------------------------------------

void WaveshareAudio::start() {
  if (!this->ready_) return;
  if (this->state == speaker::STATE_RUNNING) return;

  // Reject if a file/buzz task is active.  The caller should wait or cancel.
  if (this->playback_task_ != nullptr) {
    ESP_LOGW(TAG, "start() called while task playback active — ignoring");
    return;
  }

  // Configure to the YAML sample_rate (48000 Hz default) in stereo — matching
  // the media_player pipeline format.
  if (!this->reconfigure_clock_if_needed_(this->sample_rate_)) return;
  if (!this->reconfigure_slot_if_needed_(2)) return;
  if (!this->enable_channel_()) return;

  this->state = speaker::STATE_RUNNING;
  ESP_LOGD(TAG, "Speaker started (%u Hz stereo)", this->sample_rate_);
}

void WaveshareAudio::finish() {
  // finish() is called when the upstream buffer is empty (clean end-of-stream).
  // Write silence so the PCM5100A auto-mute engages, then disable the channel.
  if (this->state != speaker::STATE_RUNNING) return;
  this->write_silence_(100);
  this->disable_channel_();
  this->state = speaker::STATE_STOPPED;
  ESP_LOGD(TAG, "Speaker finished");
}

void WaveshareAudio::stop() {
  if (this->state == speaker::STATE_STOPPED) return;
  this->write_silence_(30);
  this->disable_channel_();
  this->state = speaker::STATE_STOPPED;
  ESP_LOGD(TAG, "Speaker stopped");
}

size_t WaveshareAudio::play(const uint8_t *data, size_t length,
                             TickType_t ticks_to_wait) {
  if (!this->ready_) return 0;

  // Auto-start if the channel is not yet enabled (e.g. first call without
  // an explicit start()).
  if (!this->channel_enabled_) {
    if (!this->reconfigure_clock_if_needed_(this->sample_rate_)) return 0;
    if (!this->reconfigure_slot_if_needed_(2)) return 0;
    if (!this->enable_channel_()) return 0;
    this->state = speaker::STATE_RUNNING;
  }

  const int16_t *src = reinterpret_cast<const int16_t *>(data);
  float g = this->gain_.load(std::memory_order_relaxed);
  size_t total_samples = length / sizeof(int16_t);
  size_t written_bytes = 0;

  while (written_bytes < length) {
    size_t offset_samples = written_bytes / sizeof(int16_t);
    size_t chunk_samples  = std::min(total_samples - offset_samples, SCALE_BUF_SAMPLES);

    for (size_t i = 0; i < chunk_samples; i++)
      this->scale_buf_[i] = static_cast<int16_t>(src[offset_samples + i] * g);

    size_t chunk_bytes    = chunk_samples * sizeof(int16_t);
    size_t just_written   = 0;
    esp_err_t err = i2s_channel_write(this->tx_chan_, this->scale_buf_,
                                       chunk_bytes, &just_written, ticks_to_wait);
    if (err != ESP_OK || just_written == 0)
      break;
    written_bytes += just_written;
  }

  return written_bytes;
}

bool WaveshareAudio::has_buffered_data() const {
  // The DMA ring buffer always has capacity; report true only when the channel
  // is actively running to avoid the media pipeline spinning unnecessarily.
  return this->channel_enabled_;
}

// ---------------------------------------------------------------------------
// Channel enable / disable
// ---------------------------------------------------------------------------

bool WaveshareAudio::enable_channel_() {
  if (this->channel_enabled_) return true;
  esp_err_t err = i2s_channel_enable(this->tx_chan_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable: %s", esp_err_to_name(err));
    return false;
  }
  this->channel_enabled_ = true;
  return true;
}

void WaveshareAudio::disable_channel_() {
  if (!this->channel_enabled_) return;
  i2s_channel_disable(this->tx_chan_);
  this->channel_enabled_ = false;
}

// ---------------------------------------------------------------------------
// Reconfigure helpers
// ---------------------------------------------------------------------------

bool WaveshareAudio::reconfigure_clock_if_needed_(uint32_t target_rate) {
  if (target_rate == this->current_sample_rate_) return true;
  ESP_LOGI(TAG, "Clock %u -> %u Hz", this->current_sample_rate_, target_rate);
  bool was_enabled = this->channel_enabled_;
  this->disable_channel_();
  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(target_rate);
  esp_err_t err = i2s_channel_reconfig_std_clock(this->tx_chan_, &clk);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "reconfig clock: %s", esp_err_to_name(err));
    if (was_enabled) this->enable_channel_();
    return false;
  }
  this->current_sample_rate_ = target_rate;
  if (was_enabled) return this->enable_channel_();
  return true;
}

bool WaveshareAudio::reconfigure_slot_if_needed_(uint16_t num_channels) {
  i2s_slot_mode_t target = (num_channels >= 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
  if (target == this->current_slot_mode_) return true;
  ESP_LOGI(TAG, "Slot -> %s", (target == I2S_SLOT_MODE_STEREO) ? "stereo" : "mono");
  bool was_enabled = this->channel_enabled_;
  this->disable_channel_();
  i2s_std_slot_config_t slot =
      I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, target);
  esp_err_t err = i2s_channel_reconfig_std_slot(this->tx_chan_, &slot);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "reconfig slot: %s", esp_err_to_name(err));
    if (was_enabled) this->enable_channel_();
    return false;
  }
  this->current_slot_mode_ = target;
  if (was_enabled) return this->enable_channel_();
  return true;
}

// ---------------------------------------------------------------------------
// WAV header parser
// ---------------------------------------------------------------------------

WaveshareAudio::WavHeader WaveshareAudio::read_wav_header_(FILE *fp) {
  WavHeader hdr;
  uint8_t buf[44];
  fseek(fp, 0, SEEK_SET);
  if (fread(buf, 1, sizeof(buf), fp) != sizeof(buf)) return hdr;
  if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F' ||
      buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E')
    return hdr;
  hdr.num_channels    = static_cast<uint16_t>(buf[22] | (buf[23] << 8));
  hdr.sample_rate     = static_cast<uint32_t>(
      buf[24] | (buf[25] << 8) | (buf[26] << 16) | (buf[27] << 24));
  hdr.bits_per_sample = static_cast<uint16_t>(buf[34] | (buf[35] << 8));
  hdr.valid = (hdr.num_channels >= 1 && hdr.num_channels <= 2 &&
               hdr.bits_per_sample == 16 &&
               hdr.sample_rate >= 8000 && hdr.sample_rate <= 48000);
  if (!hdr.valid)
    ESP_LOGW(TAG, "Unsupported WAV: %u ch, %u-bit, %u Hz — using defaults",
             hdr.num_channels, hdr.bits_per_sample, hdr.sample_rate);
  return hdr;
}

// ---------------------------------------------------------------------------
// Core audio I/O
// ---------------------------------------------------------------------------

bool WaveshareAudio::write_pcm_(const int16_t *pcm, size_t bytes) {
  if (!this->ready_ || this->stop_requested_) return false;
  size_t n = std::min(bytes / sizeof(int16_t), SCALE_BUF_SAMPLES);
  float g = this->gain_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < n; i++)
    this->scale_buf_[i] = static_cast<int16_t>(pcm[i] * g);
  size_t write_bytes   = n * sizeof(int16_t);
  size_t total_written = 0;
  while (total_written < write_bytes && !this->stop_requested_) {
    size_t just_written = 0;
    const auto *ptr = reinterpret_cast<const uint8_t *>(this->scale_buf_) + total_written;
    esp_err_t err = i2s_channel_write(this->tx_chan_, ptr,
                                       write_bytes - total_written,
                                       &just_written, pdMS_TO_TICKS(100));
    if (err != ESP_OK) { ESP_LOGW(TAG, "i2s write: %s", esp_err_to_name(err)); return false; }
    if (just_written == 0) continue;
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
  if (!this->stop_requested_) vTaskDelay(pdMS_TO_TICKS(20));
}

// ---------------------------------------------------------------------------
// File playback
// ---------------------------------------------------------------------------

bool WaveshareAudio::play_file_blocking_(const std::string &path) {
  FILE *fp = fopen(path.c_str(), "rb");
  if (!fp) { ESP_LOGE(TAG, "Cannot open: %s", path.c_str()); return false; }

  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  WavHeader hdr = this->read_wav_header_(fp);
  if (!hdr.valid) {
    hdr.num_channels = 1;
    hdr.sample_rate  = this->sample_rate_;
    fseek(fp, 44, SEEK_SET);
  }

  ESP_LOGI(TAG, "Playing %s (%ld B, %u ch, %u Hz)", path.c_str(),
           file_size, hdr.num_channels, hdr.sample_rate);

  if (!this->reconfigure_clock_if_needed_(hdr.sample_rate) ||
      !this->reconfigure_slot_if_needed_(hdr.num_channels) ||
      !this->enable_channel_()) {
    fclose(fp);
    return false;
  }

  bool ok = true;
  while (!this->stop_requested_) {
    size_t read = fread(this->read_buf_, 1,
                        READ_BUF_SAMPLES * sizeof(int16_t), fp);
    if (read == 0) break;
    if (!this->write_pcm_(this->read_buf_, read)) { ok = false; break; }
  }
  fclose(fp);
  return ok && !this->stop_requested_;
}

// ---------------------------------------------------------------------------
// Buzz / tone
// ---------------------------------------------------------------------------

bool WaveshareAudio::play_sine_(float freq_hz, uint32_t duration_ms) {
  if (!this->ready_) return false;
  const float    amp          = 28000.0f;
  const uint32_t sample_count = (this->current_sample_rate_ * duration_ms) / 1000;
  const uint32_t fade         = std::max<uint32_t>(1, this->current_sample_rate_ / 200);
  int16_t samples[256];
  uint32_t generated = 0;
  while (generated < sample_count && !this->stop_requested_) {
    size_t to_gen = std::min<size_t>(256, sample_count - generated);
    for (size_t i = 0; i < to_gen; i++) {
      uint32_t idx = generated + i;
      float t   = static_cast<float>(idx) / static_cast<float>(this->current_sample_rate_);
      float env = 1.0f;
      if (idx < fade)                      env = static_cast<float>(idx) / static_cast<float>(fade);
      else if (idx + fade > sample_count)  env = static_cast<float>(sample_count - idx) / static_cast<float>(fade);
      samples[i] = (freq_hz > 0.0f)
          ? static_cast<int16_t>(amp * env * sinf(2.0f * static_cast<float>(M_PI) * freq_hz * t))
          : 0;
    }
    if (!this->write_pcm_(samples, to_gen * sizeof(int16_t))) return false;
    generated += to_gen;
  }
  return !this->stop_requested_;
}

bool WaveshareAudio::play_buzz_blocking_(BuzzPattern pattern) {
  if (!this->ready_) return false;
  // Buzz patterns are mono at the YAML sample rate.
  if (!this->reconfigure_clock_if_needed_(this->sample_rate_)) return false;
  if (!this->reconfigure_slot_if_needed_(1)) return false;
  if (!this->enable_channel_()) return false;
  switch (pattern) {
    case BUZZ_SINE:        return this->play_sine_(880.0f, 250);
    case BUZZ_DOUBLE_BEEP: return this->play_sine_(660.0f, 120) &&
                                  this->play_sine_(0.0f,   70)  &&
                                  this->play_sine_(660.0f, 120);
    case BUZZ_ALARM:
      for (int i = 0; i < 3; i++)
        if (!this->play_sine_(1200.0f, 140) || !this->play_sine_(700.0f, 140)) return false;
      return true;
    default: return false;
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

  self->write_silence_(100);
  self->disable_channel_();

  // After file/buzz playback ends, restore Speaker to STOPPED so the media
  // pipeline can call start() again cleanly.
  self->state = speaker::STATE_STOPPED;

  self->stop_requested_ = false;
  self->playback_mode_  = PLAYBACK_NONE;
  self->playback_task_  = nullptr;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Public API — file / buzz
// ---------------------------------------------------------------------------

bool WaveshareAudio::play_file(const std::string &path) {
  if (!this->ready_) return false;
  // Reject if the Speaker streaming interface is active.
  if (this->state == speaker::STATE_RUNNING) {
    ESP_LOGW(TAG, "play_file() blocked — Speaker streaming active; call stop() first");
    return false;
  }
  if (this->playback_task_ != nullptr) {
    ESP_LOGW(TAG, "Playback already running");
    return false;
  }
  this->pending_file_   = path.empty() ? this->default_file_ : path;
  this->stop_requested_ = false;
  this->playback_mode_  = PLAYBACK_FILE;
  BaseType_t r = xTaskCreate(WaveshareAudio::playback_task_trampoline_,
                              "ws_audio_play", 8192, this, 4,
                              &this->playback_task_);
  if (r != pdPASS) { this->playback_task_ = nullptr; ESP_LOGE(TAG, "Task create failed"); return false; }
  return true;
}

bool WaveshareAudio::play_buzz(BuzzPattern pattern) {
  if (!this->ready_) return false;
  if (this->state == speaker::STATE_RUNNING) {
    ESP_LOGW(TAG, "play_buzz() blocked — Speaker streaming active; call stop() first");
    return false;
  }
  if (this->playback_task_ != nullptr) {
    ESP_LOGW(TAG, "Playback already running");
    return false;
  }
  this->pending_buzz_   = pattern;
  this->stop_requested_ = false;
  this->playback_mode_  = PLAYBACK_BUZZ;
  BaseType_t r = xTaskCreate(WaveshareAudio::playback_task_trampoline_,
                              "ws_audio_buzz", 8192, this, 4,
                              &this->playback_task_);
  if (r != pdPASS) { this->playback_task_ = nullptr; ESP_LOGE(TAG, "Task create failed"); return false; }
  return true;
}

void WaveshareAudio::stop_playback() {
  if (!this->ready_) return;
  this->stop_requested_ = true;
  if (this->channel_enabled_) {
    this->write_silence_(30);
    this->disable_channel_();
  }
  this->state = speaker::STATE_STOPPED;
}

}  // namespace waveshare_audio
}  // namespace esphome