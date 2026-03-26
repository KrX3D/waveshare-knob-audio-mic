#include "waveshare_mic.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace waveshare_mic {

static const char *const TAG = "waveshare_mic";

void WaveshareMic::setup() {
  this->buffer_ = static_cast<int16_t *>(
      heap_caps_malloc(this->buffer_bytes_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!this->buffer_) {
    ESP_LOGE(TAG, "Failed to allocate mic buffer (%u bytes)",
             static_cast<unsigned>(this->buffer_bytes_));
    this->mark_failed();
    return;
  }

  if (!this->init_i2s_()) {
    heap_caps_free(this->buffer_);
    this->buffer_ = nullptr;
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Microphone ready (rate=%u Hz, path=%s)",
           this->sample_rate_, this->default_path_.c_str());
}

void WaveshareMic::loop() {
  if (!this->i2s_ready_) return;

  bool mic_active = (this->state_ == microphone::STATE_RUNNING);
  if (!this->recording_ && !mic_active) return;

  size_t    bytes_read = 0;
  esp_err_t err = i2s_channel_read(this->rx_chan_, this->buffer_,
                                    this->buffer_bytes_, &bytes_read,
                                    pdMS_TO_TICKS(200));

  if (err == ESP_ERR_TIMEOUT) return;
  if (bytes_read == 0) return;

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2s_channel_read: %s", esp_err_to_name(err));
    return;
  }

  // SD recording path
  if (this->recording_ && this->record_file_) {
    size_t written = fwrite(this->buffer_, 1, bytes_read, this->record_file_);
    this->bytes_written_ += written;
    if (written != bytes_read) {
      ESP_LOGE(TAG, "Short write to %s; stopping", this->current_path_.c_str());
      this->stop_recording();
      return;
    }

    uint32_t now = millis();
    if (now - this->last_stats_log_ms_ > 5000) {
      this->last_stats_log_ms_ = now;
      ESP_LOGI(TAG, "Recording %s: %u ms, %u bytes",
               this->current_path_.c_str(), now - this->start_ms_,
               this->bytes_written_);
    }
  }

  // Microphone callback path (voice_assistant / on_data)
  // data_callbacks_ expects const std::vector<uint8_t>& — pass raw bytes.
  if (mic_active) {
    std::vector<uint8_t> data(
        reinterpret_cast<uint8_t *>(this->buffer_),
        reinterpret_cast<uint8_t *>(this->buffer_) + bytes_read);
    this->data_callbacks_.call(data);
  }
}

void WaveshareMic::start() {
  if (!this->i2s_ready_) {
    ESP_LOGW(TAG, "start() called but I2S not ready");
    return;
  }
  if (this->state_ == microphone::STATE_RUNNING) return;

  // Flush stale PDM DMA data accumulated while idle.
  i2s_channel_disable(this->rx_chan_);
  i2s_channel_enable(this->rx_chan_);

  this->state_ = microphone::STATE_RUNNING;
  ESP_LOGD(TAG, "Microphone started");
}

void WaveshareMic::stop() {
  if (this->state_ == microphone::STATE_STOPPED) return;
  // Don't disable I2S here — SD recording may still be active.
  this->state_ = microphone::STATE_STOPPED;
  ESP_LOGD(TAG, "Microphone stopped");
}

bool WaveshareMic::init_i2s_() {
  i2s_chan_config_t rx_chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&rx_chan_cfg, nullptr, &this->rx_chan_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
    return false;
  }

  i2s_pdm_rx_config_t pdm_rx_cfg = {
      .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(this->sample_rate_),
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .clk = static_cast<gpio_num_t>(this->pdm_clock_pin_),
          .din = static_cast<gpio_num_t>(this->pdm_data_pin_),
          .invert_flags = { .clk_inv = false },
      },
  };

  err = i2s_channel_init_pdm_rx_mode(this->rx_chan_, &pdm_rx_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode: %s", esp_err_to_name(err));
    return false;
  }

  err = i2s_channel_enable(this->rx_chan_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable: %s", esp_err_to_name(err));
    return false;
  }

  this->i2s_ready_ = true;
  return true;
}

bool WaveshareMic::open_file_(const std::string &path) {
  this->record_file_ = fopen(path.c_str(), "wb");
  if (!this->record_file_) { ESP_LOGE(TAG, "Cannot open: %s", path.c_str()); return false; }
  return true;
}

void WaveshareMic::close_file_() {
  if (this->record_file_) {
    fflush(this->record_file_);
    fclose(this->record_file_);
    this->record_file_ = nullptr;
  }
}

bool WaveshareMic::write_wav_header_placeholder_() {
  const uint8_t h[44] = {
    'R','I','F','F', 0,0,0,0,
    'W','A','V','E',
    'f','m','t',' ', 16,0,0,0,
    1,0, 1,0,
    0,0,0,0, 0,0,0,0,
    2,0, 16,0,
    'd','a','t','a', 0,0,0,0,
  };
  return fwrite(h, 1, sizeof(h), this->record_file_) == sizeof(h);
}

void WaveshareMic::finalize_wav_header_() {
  if (!this->record_file_) return;
  uint32_t data_size = this->bytes_written_;
  uint32_t riff_size = data_size + 36;
  uint32_t byte_rate = this->sample_rate_ * 2;
  fseek(this->record_file_, 4,  SEEK_SET); fwrite(&riff_size,          1, 4, this->record_file_);
  fseek(this->record_file_, 24, SEEK_SET); fwrite(&this->sample_rate_, 1, 4, this->record_file_);
                                            fwrite(&byte_rate,          1, 4, this->record_file_);
  fseek(this->record_file_, 40, SEEK_SET); fwrite(&data_size,          1, 4, this->record_file_);
}

bool WaveshareMic::start_recording(const std::string &path) {
  if (!this->i2s_ready_) { ESP_LOGW(TAG, "Mic not ready"); return false; }
  if (this->recording_)  { ESP_LOGW(TAG, "Already recording"); return true; }

  this->current_path_ = path.empty() ? this->default_path_ : path;
  if (!this->open_file_(this->current_path_)) return false;
  if (!this->write_wav_header_placeholder_()) {
    ESP_LOGE(TAG, "WAV header write failed: %s", this->current_path_.c_str());
    this->close_file_();
    return false;
  }

  // Flush stale PDM DMA data.
  i2s_channel_disable(this->rx_chan_);
  i2s_channel_enable(this->rx_chan_);

  this->bytes_written_     = 0;
  this->start_ms_          = millis();
  this->last_stats_log_ms_ = this->start_ms_;
  this->recording_         = true;

  ESP_LOGI(TAG, "Recording -> %s (rate=%u Hz)", this->current_path_.c_str(), this->sample_rate_);
  return true;
}

void WaveshareMic::stop_recording() {
  if (!this->recording_) return;
  this->last_recording_ms_ = millis() - this->start_ms_;
  this->recording_         = false;
  this->finalize_wav_header_();
  this->close_file_();

  if (this->bytes_written_ == 0)
    ESP_LOGW(TAG, "Stopped (%u ms) — 0 bytes captured", this->last_recording_ms_);
  else
    ESP_LOGI(TAG, "Stopped (%u ms, %u bytes)", this->last_recording_ms_, this->bytes_written_);
}

uint32_t WaveshareMic::recording_ms() const {
  if (!this->recording_) return 0;
  return millis() - this->start_ms_;
}

}  // namespace waveshare_mic
}  // namespace esphome