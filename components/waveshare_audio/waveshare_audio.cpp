#include "waveshare_audio.h"

#include "esphome/core/log.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace esphome {
namespace waveshare_audio {

static const char *const TAG = "waveshare_audio";

void WaveshareAudio::setup() {
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
  if (!this->ready_)
    return false;

  std::vector<int16_t> scaled(bytes / sizeof(int16_t));
  for (size_t i = 0; i < scaled.size(); i++) {
    scaled[i] = static_cast<int16_t>(pcm[i] * this->gain_);
  }

  size_t bytes_written = 0;
  esp_err_t err = i2s_channel_write(this->tx_chan_, scaled.data(), bytes, &bytes_written, portMAX_DELAY);
  return err == ESP_OK && bytes_written == bytes;
}

bool WaveshareAudio::play_file(const std::string &path) {
  std::string final_path = path.empty() ? this->default_file_ : path;
  FILE *fp = fopen(final_path.c_str(), "rb");
  if (fp == nullptr) {
    ESP_LOGE(TAG, "Unable to open file: %s", final_path.c_str());
    return false;
  }

  fseek(fp, 44, SEEK_SET);  // skip wav header
  std::vector<int16_t> block(1024);

  while (true) {
    size_t read = fread(block.data(), 1, block.size() * sizeof(int16_t), fp);
    if (read == 0)
      break;
    if (!this->write_pcm_(block.data(), read)) {
      ESP_LOGE(TAG, "I2S write failed during playback");
      fclose(fp);
      return false;
    }
  }

  fclose(fp);
  ESP_LOGI(TAG, "Played file %s", final_path.c_str());
  return true;
}

bool WaveshareAudio::play_sine_(float freq_hz, uint32_t duration_ms) {
  if (!this->ready_)
    return false;

  const float amplitude = 28000.0f;
  const uint32_t sample_count = (this->sample_rate_ * duration_ms) / 1000;
  std::vector<int16_t> samples(256);

  uint32_t generated = 0;
  while (generated < sample_count) {
    size_t to_generate = std::min<size_t>(samples.size(), sample_count - generated);
    for (size_t i = 0; i < to_generate; i++) {
      float t = static_cast<float>(generated + i) / static_cast<float>(this->sample_rate_);
      samples[i] = static_cast<int16_t>(amplitude * sinf(2.0f * static_cast<float>(M_PI) * freq_hz * t));
    }

    if (!this->write_pcm_(samples.data(), to_generate * sizeof(int16_t))) {
      return false;
    }
    generated += to_generate;
  }
  return true;
}

bool WaveshareAudio::play_buzz(BuzzPattern pattern) {
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

void WaveshareAudio::stop() {
  if (!this->ready_)
    return;
  i2s_channel_disable(this->tx_chan_);
  i2s_channel_enable(this->tx_chan_);
}

}  // namespace waveshare_audio
}  // namespace esphome
