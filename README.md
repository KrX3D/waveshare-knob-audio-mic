# Waveshare Knob Audio + Mic ESPHome Components

This repository now provides **two ESPHome external components** for ESP32-S3 audio on the Waveshare Smart Knob style board:

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

Example config:

```yaml
waveshare_mic:
  id: ws_mic
  pdm_clock_pin: 42
  pdm_data_pin: 2
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

Example config:

```yaml
waveshare_audio:
  id: ws_audio
  bclk_pin: 41
  ws_pin: 40
  dout_pin: 39
  sample_rate: 44100
  default_file: /sdcard/recording.wav
  gain: 0.25
```

## Full example

See `example.yaml` for start/stop buttons, buzz selection, and gain control.

## Notes

- Use your existing SD component to mount `/sdcard` first (for example your `waveshare-knob-sdmmc` component).
- Current implementation is raw WAV read/write and simple buzz generation.
- A true Home Assistant `media_player` entity can be added later as a next step.
