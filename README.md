# Waveshare Knob Audio + Mic ESPHome Components

This repository provides two ESPHome external components for ESP32-S3 audio on Waveshare Smart Knob style boards:

- `waveshare_mic` (PDM microphone -> WAV file on SD)
- `waveshare_audio` (WAV/buzz playback -> I2S DAC / AUX output)

## Components

### `waveshare_mic`

Features:
- Configurable PDM clock/data pins
- Configurable sample rate
- Writes 16-bit mono WAV files to SD path
- Runtime controls:
  - `start_recording(path)`
  - `stop_recording()`
  - `is_recording()`
  - `recording_ms()`

Default pins now match your Arduino project:
- `pdm_clock_pin: 45`
- `pdm_data_pin: 46`

```yaml
waveshare_mic:
  id: ws_mic
  pdm_clock_pin: 45
  pdm_data_pin: 46
  sample_rate: 44100
  path: /sdcard/recording.wav
  buffer_bytes: 4096
```

### `waveshare_audio`

Features:
- Configurable I2S output pins (BCLK/WS/DOUT)
- Configurable sample rate and gain
- WAV file playback from SD
- Built-in buzz patterns:
  - `BUZZ_SINE`
  - `BUZZ_DOUBLE_BEEP`
  - `BUZZ_ALARM`
- Runtime controls:
  - `play_file(path)`
  - `play_buzz(pattern)`
  - `stop()`
  - `set_gain(value)`
- PCM5100 control pin support (`enable_pin`, defaults to `GPIO0`, set HIGH in setup)

Default pins now match your Arduino project:
- `bclk_pin: 39`
- `ws_pin: 40`
- `dout_pin: 41`

```yaml
waveshare_audio:
  id: ws_audio
  bclk_pin: 39
  ws_pin: 40
  dout_pin: 41
  enable_pin: 0
  sample_rate: 44100
  default_file: /sdcard/recording.wav
  gain: 0.25
```

## Full example

See `example.yaml` for:
- record start/stop buttons,
- playback button,
- buzz selection,
- gain control,
- UART + I2C declarations.

## Important notes

1. **UART pin conflict with audio**
   - If you use:
     - `uart tx: GPIO40`
     - `uart rx: GPIO39`
   - then those pins cannot simultaneously be used for I2S BCLK/WS.
   - In that case use the alternate audio pins shown in your Arduino comments: `BCLK=48, WS=38, DOUT=47`.

2. **Built-in ESPHome i2s microphone error (`adc_type not specified`)**
   - That error is for ESPHome's built-in `microphone: platform: i2s_audio`.
   - Use `adc_type: pdm` when using a PDM microphone.
   - This repository's custom `waveshare_mic` component does not use that option.

3. **SD card path**
   - Mount SD first (for example with your `waveshare-knob-sdmmc` component), then use paths like `/sdcard/recording.wav`.
