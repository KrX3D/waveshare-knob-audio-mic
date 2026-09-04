import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import speaker
from esphome.const import CONF_BITS_PER_SAMPLE, CONF_ID, CONF_SAMPLE_RATE
from . import waveshare_audio_ns

CODEOWNERS = ["@KrX3D"]

CONF_BCLK_PIN     = "bclk_pin"
CONF_WS_PIN       = "ws_pin"
CONF_DOUT_PIN     = "dout_pin"
CONF_DEFAULT_FILE = "default_file"
CONF_GAIN         = "gain"
CONF_ENABLE_PIN   = "enable_pin"
CONF_NUM_CHANNELS = "num_channels"

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
        cv.Optional(CONF_SAMPLE_RATE,  default=48000):                   cv.int_range(min=8000, max=48000),
        cv.Optional(CONF_DEFAULT_FILE, default="/sdcard/recording.wav"): cv.string,
        cv.Optional(CONF_GAIN,         default=0.25):                    cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_ENABLE_PIN,   default=0):                       validate_gpio_num,
        # The mixer platform's FINAL_VALIDATE_SCHEMA does
        #   inherit_property_from(CONF_NUM_CHANNELS,    CONF_OUTPUT_SPEAKER)
        #   inherit_property_from(CONF_BITS_PER_SAMPLE, CONF_OUTPUT_SPEAKER)
        # so BOTH keys must exist in this speaker's config dict. If either is
        # missing the inherit yields None and validation fails with
        # "The <key> Must be string, got <class 'NoneType'>".
        # Values use the same spelling as the built-in i2s_audio speaker,
        # which declares default_bits_per_sample="16bit".
        cv.Optional(CONF_NUM_CHANNELS,    default="2"):     cv.string,
        cv.Optional(CONF_BITS_PER_SAMPLE, default="16bit"): cv.string,
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