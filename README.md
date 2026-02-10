# Waveshare Knob Audio + Mic ESPHome Components

This repository provides two ESPHome external components for ESP32-S3 audio on Waveshare Smart Knob style boards:

- `waveshare_mic` (PDM microphone -> WAV file on SD)
- `waveshare_audio` (WAV/buzz playback -> I2S DAC / AUX output)

## Installation

Use external components like this:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/KrX3D/waveshare-knob-audio-mic.git
      ref: main
    components: [waveshare_audio, waveshare_mic]
    refresh: 0s
```

If you prefer shorthand, this also works on many ESPHome versions:

```yaml
external_components:
  - source: github://KrX3D/waveshare-knob-audio-mic@main
    components: [waveshare_audio, waveshare_mic]
    refresh: 0s
```

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

Default pins matching Arduino project:
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

Default pins matching Arduino project:
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

## Full YAML example (all options + buttons)

This is a complete example with:
- your requested external component declaration,
- UART + I2C,
- start/stop mic recording,
- file playback,
- selectable buzz pattern,
- runtime gain control.

> Note: UART on GPIO39/40 conflicts with the default Arduino I2S pins, so this example uses the alternate audio pins `48/38/47`.

```yaml
esphome:
  name: smartknob_audio_mic

external_components:
  - source:
      type: git
      url: https://github.com/KrX3D/waveshare-knob-audio-mic.git
      ref: main
    components: [waveshare_audio, waveshare_mic]
    refresh: 0s

esp32:
  board: esp32-s3-devkitc-1
  flash_size: 16MB
  framework:
    type: esp-idf

logger:
  level: DEBUG

api:
ota:

i2c:
  sda: 11
  scl: 12
  id: ic_bus

uart:
  id: audio_uart
  tx_pin: GPIO40
  rx_pin: GPIO39
  baud_rate: 115200
  rx_buffer_size: 1024

# mount your sd card with your existing sdmmc component first

waveshare_mic:
  id: ws_mic
  pdm_clock_pin: 45
  pdm_data_pin: 46
  sample_rate: 44100
  path: /sdcard/recording.wav
  buffer_bytes: 4096

waveshare_audio:
  id: ws_audio
  bclk_pin: 48
  ws_pin: 38
  dout_pin: 47
  enable_pin: 0
  sample_rate: 44100
  default_file: /sdcard/recording.wav
  gain: 0.25

globals:
  - id: selected_file
    type: std::string
    restore_value: no
    initial_value: '"/sdcard/recording.wav"'

select:
  - platform: template
    name: "Audio Buzz Pattern"
    id: buzz_pattern
    optimistic: true
    options:
      - "Sine"
      - "Double Beep"
      - "Alarm"
    initial_option: "Sine"

button:
  - platform: template
    name: "Mic Start Recording"
    on_press:
      - lambda: |-
          id(ws_mic).start_recording(id(selected_file));

  - platform: template
    name: "Mic Stop Recording"
    on_press:
      - lambda: |-
          id(ws_mic).stop_recording();

  - platform: template
    name: "Audio Play File"
    on_press:
      - lambda: |-
          id(ws_audio).play_file(id(selected_file));

  - platform: template
    name: "Audio Stop"
    on_press:
      - lambda: |-
          id(ws_audio).stop();

  - platform: template
    name: "Audio Play Buzz"
    on_press:
      - lambda: |-
          using namespace esphome::waveshare_audio;
          auto option = id(buzz_pattern).state;
          if (option == "Double Beep") {
            id(ws_audio).play_buzz(BUZZ_DOUBLE_BEEP);
          } else if (option == "Alarm") {
            id(ws_audio).play_buzz(BUZZ_ALARM);
          } else {
            id(ws_audio).play_buzz(BUZZ_SINE);
          }

number:
  - platform: template
    name: "Audio Gain"
    optimistic: true
    min_value: 0.0
    max_value: 1.0
    step: 0.05
    initial_value: 0.25
    set_action:
      - lambda: |-
          id(ws_audio).set_gain(x);
```

## Home Assistant media_player option

Your custom `waveshare_audio` component exposes buttons/actions, but it does **not** currently register as a Home Assistant `media_player` entity.

If you want a real HA `media_player`, use ESPHome built-in `speaker` + `media_player` (optional, separate path). Example skeleton:

```yaml
# optional alternative path for HA media_player
# use free pins that do not conflict with uart/i2c/other peripherals

i2s_audio:
  - id: i2s_for_speaker
    i2s_bclk_pin: GPIO48
    i2s_lrclk_pin: GPIO38

speaker:
  - platform: i2s_audio
    id: ext_speaker
    i2s_audio_id: i2s_for_speaker
    i2s_dout_pin: GPIO47
    dac_type: external
    sample_rate: 44100
    channel: mono

media_player:
  - platform: speaker
    name: SmartKnob Speaker
    id: smartknob_media_player
    speaker: ext_speaker
```

> Do not drive the same physical DAC from both `waveshare_audio` and built-in `speaker` at the same time.

## Important notes

1. **UART pin conflict with audio**
   - If you use:
     - `uart tx: GPIO40`
     - `uart rx: GPIO39`
   - then those pins cannot simultaneously be used for I2S BCLK/WS.
   - In that case use alternate audio pins (as shown above): `BCLK=48, WS=38, DOUT=47`.

2. **Built-in ESPHome i2s microphone error (`adc_type not specified`)**
   - That error is for ESPHome's built-in `microphone: platform: i2s_audio`.
   - Use `adc_type: pdm` when using a PDM microphone with built-in ESPHome microphone config.
   - This repository's custom `waveshare_mic` component does not use that option.

3. **SD card path**
   - Mount SD first (for example with your `waveshare-knob-sdmmc` component), then use paths like `/sdcard/recording.wav`.


4. **`@None` external component / broken cache folder**
   - If logs show `...waveshare-knob-audio-mic.git@None`, your source ref was not resolved.
   - Fix by setting explicit `ref: main` (or use `github://...@main`).
   - Then clear corrupted external component cache and rerun:

```bash
rm -rf /data/external_components
```

   - If still stuck, restart the ESPHome add-on/container and run `esphome clean <your_yaml>.yaml` before compile.
