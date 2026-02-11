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
  bclk_pin: 39
  ws_pin: 40
  dout_pin: 41
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
          auto option = id(buzz_pattern).current_option();
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


5. **No beeps / no AUX output**
   - Compare against Arduino mapping (from `user_config.h`):
     - I2S DAC: `BCLK=39`, `WS=40`, `DOUT=41`
     - PDM mic: `CLK=45`, `DATA=46`
   - If UART on GPIO39/40 is enabled, audio clock pins conflict; either disable UART or move I2S to alternate pins (`48/38/47`) if your board revision actually routes them.
   - Your current custom component sets PCM5100 enable pin HIGH on `enable_pin` (default `GPIO0`), matching Arduino behavior.
   - For first audible test, set gain higher (for example `gain: 0.8`).

## Home Assistant media_player (full add-on YAML block)

If you want a true HA `media_player` entity, use ESPHome built-in `speaker` + `media_player` on the same DAC pins (do not use `waveshare_audio` at the same time):

```yaml
# Use either this built-in media_player path OR waveshare_audio, not both simultaneously

i2s_audio:
  - id: i2s_for_speaker
    i2s_bclk_pin: GPIO39
    i2s_lrclk_pin: GPIO40

speaker:
  - platform: i2s_audio
    id: ext_speaker
    i2s_audio_id: i2s_for_speaker
    i2s_dout_pin: GPIO41
    dac_type: external
    sample_rate: 44100
    channel: mono

media_player:
  - platform: speaker
    name: SmartKnob Speaker
    id: smartknob_media_player
    speaker: ext_speaker
```


## Built-in ESPHome path (recommended for Voice Assistant testing)

If you want to validate hardware quickly without these custom components, use ESPHome built-ins (`i2s_audio` + `microphone` + `speaker` + `media_player` + `voice_assistant`).

A full reference config is included in this repo:
- `example_builtin_voice_assistant.yaml`

Why this often works better first:
- easier integration with Home Assistant Assist,
- known-good `media_player` entity behavior,
- simpler troubleshooting surface.

### About your current config

Your built-in pin layout is consistent with the Arduino mapping for active audio path:
- mic PDM: `CLK=45`, `DATA=46`
- speaker I2S: `BCLK=39`, `WS=40`, `DOUT=41`

And yes, for current ESPHome schemas you still need an `i2s_lrclk_pin` even on the PDM mic bus; using a free dummy pin is expected.

### Why you may hear buzzing on TTS

Common causes:
1. **Sample-rate mismatch / resampling artifacts** (TTS stream vs I2S pipeline).
2. **Pin conflict/noise coupling** (UART/I2C/other peripherals sharing nearby lines).
3. **Power/ground noise** on the DAC/amp path.
4. **Very low gain in custom path** (if using custom component, raise gain for audibility tests).

Quick checks:
- keep speaker/media pipeline at `16 kHz`, mono, WAV while testing,
- temporarily disable UART and any non-essential peripherals,
- verify `enable_pin` behavior if using `waveshare_audio` custom component,
- test with a short known-good WAV first, then TTS.
