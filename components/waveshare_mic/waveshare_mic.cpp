#include "waveshare_mic.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace waveshare_mic {

static const char *const TAG = "waveshare_mic";

void WaveshareMic::setup() {
  this->buffer_ = static_cast<int16_t *>(heap_caps_malloc(this->buffer_bytes_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate mic buffer (%u bytes)", static_cast<unsigned>(this->buffer_bytes_));
    this->mark_failed();
    return;
  }

  if (!this->init_i2s_()) {
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Microphone ready (sample_rate=%u, default_path=%s)", this->sample_rate_, this->default_path_.c_str());
}

void WaveshareMic::loop() {
  if (!this->recording_ || !this->i2s_ready_ || this->record_file_ == nullptr)
    return;

  size_t bytes_read = 0;
  esp_err_t err = i2s_channel_read(this->rx_chan_, this->buffer_, this->buffer_bytes_, &bytes_read, 0);
  if (err == ESP_OK && bytes_read > 0) {
    size_t written = fwrite(this->buffer_, 1, bytes_read, this->record_file_);
    this->bytes_written_ += written;
    if (written != bytes_read) {
      ESP_LOGE(TAG, "Short write to %s; stopping recording", this->default_path_.c_str());
      this->stop_recording();
    }
  }
}

bool WaveshareMic::init_i2s_() {
  i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  esp_err_t err = i2s_new_channel(&rx_chan_cfg, nullptr, &this->rx_chan_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
    return false;
  }

  i2s_pdm_rx_config_t pdm_rx_cfg = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(this->sample_rate_),
      .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .clk = static_cast<gpio_num_t>(this->pdm_clock_pin_),
              .din = static_cast<gpio_num_t>(this->pdm_data_pin_),
              .invert_flags =
                  {
                      .clk_inv = false,
                  },
          },
  };

  err = i2s_channel_init_pdm_rx_mode(this->rx_chan_, &pdm_rx_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode failed: %s", esp_err_to_name(err));
    return false;
  }

  err = i2s_channel_enable(this->rx_chan_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
    return false;
  }

  this->i2s_ready_ = true;
  return true;
}

bool WaveshareMic::open_file_(const std::string &path) {
  this->record_file_ = fopen(path.c_str(), "wb");
  if (this->record_file_ == nullptr) {
    ESP_LOGE(TAG, "Could not open recording path: %s", path.c_str());
    return false;
  }
  return true;
}

void WaveshareMic::close_file_() {
  if (this->record_file_ != nullptr) {
    fflush(this->record_file_);
    fclose(this->record_file_);
    this->record_file_ = nullptr;
  }
}

bool WaveshareMic::write_wav_header_placeholder_() {
  const uint8_t wav_header[44] = {
      'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0,
      1,   0,   0,   0,   0, 0, 0, 0, 2,   0,   16,  0,   'd', 'a', 't', 'a', 0, 0, 0, 0,
  };

  size_t wrote = fwrite(wav_header, 1, sizeof(wav_header), this->record_file_);
  return wrote == sizeof(wav_header);
}

void WaveshareMic::finalize_wav_header_() {
  if (this->record_file_ == nullptr)
    return;

  uint32_t data_size = this->bytes_written_;
  uint32_t riff_size = data_size + 36;
  uint32_t byte_rate = this->sample_rate_ * 2;

  fseek(this->record_file_, 4, SEEK_SET);
  fwrite(&riff_size, 1, sizeof(riff_size), this->record_file_);

  fseek(this->record_file_, 24, SEEK_SET);
  fwrite(&this->sample_rate_, 1, sizeof(this->sample_rate_), this->record_file_);
  fwrite(&byte_rate, 1, sizeof(byte_rate), this->record_file_);

  fseek(this->record_file_, 40, SEEK_SET);
  fwrite(&data_size, 1, sizeof(data_size), this->record_file_);
}

bool WaveshareMic::start_recording(const std::string &path) {
  if (!this->i2s_ready_) {
    ESP_LOGW(TAG, "Mic not ready yet");
    return false;
  }

  if (this->recording_) {
    ESP_LOGW(TAG, "Already recording");
    return true;
  }

  std::string final_path = path.empty() ? this->default_path_ : path;
  if (!this->open_file_(final_path))
    return false;

  if (!this->write_wav_header_placeholder_()) {
    ESP_LOGE(TAG, "Failed to write WAV header to %s", final_path.c_str());
    this->close_file_();
    return false;
  }

  this->default_path_ = final_path;
  this->bytes_written_ = 0;
  this->start_ms_ = millis();
  this->recording_ = true;

  ESP_LOGI(TAG, "Started recording to %s", final_path.c_str());
  return true;
}

void WaveshareMic::stop_recording() {
  if (!this->recording_)
    return;

  this->last_recording_ms_ = millis() - this->start_ms_;
  this->recording_ = false;
  this->finalize_wav_header_();
  this->close_file_();
  if (this->bytes_written_ == 0) {
    ESP_LOGW(TAG, "Stopped recording (%u ms) but captured 0 bytes", this->last_recording_ms_);
  } else {
    ESP_LOGI(TAG, "Stopped recording (%u ms, %u bytes)", this->last_recording_ms_, this->bytes_written_);
  }
}

uint32_t WaveshareMic::recording_ms() const {
  if (!this->recording_)
    return 0;
  return millis() - this->start_ms_;
}

}  // namespace waveshare_mic
}  // namespace esphome
