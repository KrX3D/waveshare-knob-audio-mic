#include "waveshare_audio.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace waveshare_audio {

static const char *const TAG = "waveshare_audio";

void WaveshareAudio::setup() {
  gpio_config_t gpio_conf = {};
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.pin_bit_mask = (1ULL << static_cast<uint32_t>(this->enable_pin_));
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_config(&gpio_conf);
  gpio_set_level(static_cast<gpio_num_t>(this->enable_pin_), 1);

  if (!this->init_i2s_()) {
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Audio output ready (sample_rate=%u)", this->sample_rate_);
}

bool WaveshareAudio::init_i2s_() {
  i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&tx_chan_cfg, &this->tx_chan_, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
    return false;
  }

  i2s_std_config_t tx_std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(this->sample_rate_),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = static_cast<gpio_num_t>(this->bclk_pin_),
              .ws = static_cast<gpio_num_t>(this->ws_pin_),
              .dout = static_cast<gpio_num_t>(this->dout_pin_),
              .din = I2S_GPIO_UNUSED,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  err = i2s_channel_init_std_mode(this->tx_chan_, &tx_std_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
    return false;
  }

  err = i2s_channel_enable(this->tx_chan_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
    return false;
  }

  this->ready_ = true;
  return true;
}

bool WaveshareAudio::write_pcm_(const int16_t *pcm, size_t bytes) {
  if (!this->ready_ || this->stop_requested_)
    return false;

  std::vector<int16_t> scaled(bytes / sizeof(int16_t));
  for (size_t i = 0; i < scaled.size(); i++) {
    scaled[i] = static_cast<int16_t>(pcm[i] * this->gain_);
  }

  size_t bytes_written = 0;
  esp_err_t err = i2s_channel_write(this->tx_chan_, scaled.data(), bytes, &bytes_written, pdMS_TO_TICKS(50));
  return err == ESP_OK && bytes_written == bytes;
}

void WaveshareAudio::write_silence_(uint32_t duration_ms) {
  std::vector<int16_t> zeros(256, 0);
  uint32_t sample_count = (this->sample_rate_ * duration_ms) / 1000;
  uint32_t sent = 0;
  while (sent < sample_count) {
    size_t chunk = std::min<size_t>(zeros.size(), sample_count - sent);
    size_t bytes_written = 0;
    i2s_channel_write(this->tx_chan_, zeros.data(), chunk * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(20));
    sent += chunk;
  }
}

bool WaveshareAudio::play_file_blocking_(const std::string &path) {
  FILE *fp = fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    ESP_LOGE(TAG, "Unable to open file: %s", path.c_str());
    return false;
  }

  fseek(fp, 44, SEEK_SET);
  std::vector<int16_t> block(1024);

  bool ok = true;
  while (!this->stop_requested_) {
    size_t read = fread(block.data(), 1, block.size() * sizeof(int16_t), fp);
    if (read == 0)
      break;
    if (!this->write_pcm_(block.data(), read)) {
      ok = false;
      break;
    }
  }
  fclose(fp);

  return ok && !this->stop_requested_;
}

bool WaveshareAudio::play_sine_(float freq_hz, uint32_t duration_ms) {
  if (!this->ready_)
    return false;

  const float amplitude = 28000.0f;
  const uint32_t sample_count = (this->sample_rate_ * duration_ms) / 1000;
  const uint32_t fade_samples = std::max<uint32_t>(1, this->sample_rate_ / 200);  // ~5ms
  std::vector<int16_t> samples(256);

  uint32_t generated = 0;
  while (generated < sample_count && !this->stop_requested_) {
    size_t to_generate = std::min<size_t>(samples.size(), sample_count - generated);
    for (size_t i = 0; i < to_generate; i++) {
      uint32_t idx = generated + i;
      float t = static_cast<float>(idx) / static_cast<float>(this->sample_rate_);
      float env = 1.0f;
      if (idx < fade_samples)
        env = static_cast<float>(idx) / static_cast<float>(fade_samples);
      else if (idx + fade_samples > sample_count)
        env = static_cast<float>(sample_count - idx) / static_cast<float>(fade_samples);
      if (freq_hz <= 0.0f)
        samples[i] = 0;
      else
        samples[i] = static_cast<int16_t>(amplitude * env * sinf(2.0f * static_cast<float>(M_PI) * freq_hz * t));
    }

    if (!this->write_pcm_(samples.data(), to_generate * sizeof(int16_t)))
      return false;

    generated += to_generate;
  }
  return !this->stop_requested_;
}

bool WaveshareAudio::play_buzz_blocking_(BuzzPattern pattern) {
  switch (pattern) {
    case BUZZ_SINE:
      return this->play_sine_(880.0f, 250);
    case BUZZ_DOUBLE_BEEP:
      return this->play_sine_(660.0f, 120) && this->play_sine_(0.0f, 70) && this->play_sine_(660.0f, 120);
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

void WaveshareAudio::playback_task_trampoline_(void *arg) {
  auto *self = static_cast<WaveshareAudio *>(arg);
  bool ok = true;
  if (self->playback_mode_ == PLAYBACK_BUZZ)
    ok = self->play_buzz_blocking_(self->pending_buzz_);
  else if (self->playback_mode_ == PLAYBACK_FILE)
    ok = self->play_file_blocking_(self->pending_file_);

  if (!ok && !self->stop_requested_)
    ESP_LOGW(TAG, "Playback ended with error");

  self->write_silence_(40);
  self->stop_requested_ = false;
  self->playback_mode_ = PLAYBACK_NONE;
  self->playback_task_ = nullptr;
  vTaskDelete(nullptr);
}

bool WaveshareAudio::play_file(const std::string &path) {
  if (!this->ready_)
    return false;
  if (this->playback_task_ != nullptr) {
    ESP_LOGW(TAG, "Playback already running; stop first");
    return false;
  }

  this->pending_file_ = path.empty() ? this->default_file_ : path;
  this->stop_requested_ = false;
  this->playback_mode_ = PLAYBACK_FILE;
  BaseType_t r = xTaskCreate(WaveshareAudio::playback_task_trampoline_, "ws_audio_play", 4096, this, 4, &this->playback_task_);
  if (r != pdPASS) {
    this->playback_task_ = nullptr;
    ESP_LOGE(TAG, "Failed to create playback task");
    return false;
  }
  return true;
}

bool WaveshareAudio::play_buzz(BuzzPattern pattern) {
  if (!this->ready_)
    return false;
  if (this->playback_task_ != nullptr) {
    ESP_LOGW(TAG, "Playback already running; stop first");
    return false;
  }

  this->pending_buzz_ = pattern;
  this->stop_requested_ = false;
  this->playback_mode_ = PLAYBACK_BUZZ;
  BaseType_t r = xTaskCreate(WaveshareAudio::playback_task_trampoline_, "ws_audio_buzz", 4096, this, 4, &this->playback_task_);
  if (r != pdPASS) {
    this->playback_task_ = nullptr;
    ESP_LOGE(TAG, "Failed to create buzz task");
    return false;
  }
  return true;
}

void WaveshareAudio::stop() {
  if (!this->ready_)
    return;
  this->stop_requested_ = true;
  this->write_silence_(30);
  i2s_channel_disable(this->tx_chan_);
  i2s_channel_enable(this->tx_chan_);
}

}  // namespace waveshare_audio
}  // namespace esphome
