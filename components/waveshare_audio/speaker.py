import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import speaker
from esphome.const import CONF_ID, CONF_SAMPLE_RATE
from . import waveshare_audio_ns

CODEOWNERS = ["@KrX3D"]

CONF_BCLK_PIN     = "bclk_pin"
CONF_WS_PIN       = "ws_pin"
CONF_DOUT_PIN     = "dout_pin"
CONF_DEFAULT_FILE = "default_file"
CONF_GAIN         = "gain"
CONF_ENABLE_PIN   = "enable_pin"

# Declare WaveshareAudio as a speaker::Speaker platform component.
# Declaring the class HERE (in speaker.py, not __init__.py) is what makes
# cv.use_id(speaker.Speaker) pass in the mixer platform — ESPHome's type
# checker requires the class to be registered as a speaker platform, not just
# a top-level component that happens to inherit Speaker.
WaveshareAudio = waveshare_audio_ns.class_(
    "WaveshareAudio", cg.Component, speaker.Speaker
)


def validate_gpio_num(value):
    if isinstance(value, str):
        upper = value.strip().upper()
        if upper.startswith("GPIO"):
            value = upper[4:]
    value = cv.int_(value)
    return cv.int_range(min=0, max=48)(value)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WaveshareAudio),
        cv.Required(CONF_BCLK_PIN):  validate_gpio_num,
        cv.Required(CONF_WS_PIN):    validate_gpio_num,
        cv.Required(CONF_DOUT_PIN):  validate_gpio_num,
        # 48000 Hz matches the media_player pipeline format.
        # Buzz patterns and SD file playback reconfigure the clock automatically.
        cv.Optional(CONF_SAMPLE_RATE,  default=48000):                   cv.int_range(min=8000, max=48000),
        cv.Optional(CONF_DEFAULT_FILE, default="/sdcard/recording.wav"): cv.string,
        cv.Optional(CONF_GAIN,         default=0.25):                    cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_ENABLE_PIN,   default=0):                       validate_gpio_num,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_bclk_pin(config[CONF_BCLK_PIN]))
    cg.add(var.set_ws_pin(config[CONF_WS_PIN]))
    cg.add(var.set_dout_pin(config[CONF_DOUT_PIN]))
    cg.add(var.set_sample_rate(config[CONF_SAMPLE_RATE]))
    cg.add(var.set_default_file(config[CONF_DEFAULT_FILE]))
    cg.add(var.set_gain(config[CONF_GAIN]))
    cg.add(var.set_enable_pin(config[CONF_ENABLE_PIN]))