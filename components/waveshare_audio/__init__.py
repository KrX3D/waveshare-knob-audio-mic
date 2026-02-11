import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_SAMPLE_RATE

CODEOWNERS = ["@KrX3D"]

CONF_BCLK_PIN = "bclk_pin"
CONF_WS_PIN = "ws_pin"
CONF_DOUT_PIN = "dout_pin"
CONF_DEFAULT_FILE = "default_file"
CONF_GAIN = "gain"
CONF_ENABLE_PIN = "enable_pin"

waveshare_audio_ns = cg.esphome_ns.namespace("waveshare_audio")
WaveshareAudio = waveshare_audio_ns.class_("WaveshareAudio", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WaveshareAudio),
        cv.Required(CONF_BCLK_PIN): cv.int_range(min=0, max=48),
        cv.Required(CONF_WS_PIN): cv.int_range(min=0, max=48),
        cv.Required(CONF_DOUT_PIN): cv.int_range(min=0, max=48),
        cv.Optional(CONF_SAMPLE_RATE, default=44100): cv.int_range(min=8000, max=48000),
        cv.Optional(CONF_DEFAULT_FILE, default="/sdcard/recording.wav"): cv.string,
        cv.Optional(CONF_GAIN, default=0.25): cv.float_range(min=0.0, max=1.0),
        cv.Optional(CONF_ENABLE_PIN, default=0): cv.int_range(min=0, max=48),
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
