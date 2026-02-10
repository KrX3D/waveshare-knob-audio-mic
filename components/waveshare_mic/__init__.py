import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_SAMPLE_RATE

DEPENDENCIES = []
CODEOWNERS = ["@KrX3D"]

CONF_PDM_CLOCK_PIN = "pdm_clock_pin"
CONF_PDM_DATA_PIN = "pdm_data_pin"
CONF_PATH = "path"
CONF_BUFFER_BYTES = "buffer_bytes"

waveshare_mic_ns = cg.esphome_ns.namespace("waveshare_mic")
WaveshareMic = waveshare_mic_ns.class_("WaveshareMic", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WaveshareMic),
        cv.Required(CONF_PDM_CLOCK_PIN): cv.int_range(min=0, max=48),
        cv.Required(CONF_PDM_DATA_PIN): cv.int_range(min=0, max=48),
        cv.Optional(CONF_SAMPLE_RATE, default=44100): cv.int_range(min=8000, max=48000),
        cv.Optional(CONF_PATH, default="/sdcard/recording.wav"): cv.string,
        cv.Optional(CONF_BUFFER_BYTES, default=4096): cv.int_range(min=1024, max=16384),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_pdm_clock_pin(config[CONF_PDM_CLOCK_PIN]))
    cg.add(var.set_pdm_data_pin(config[CONF_PDM_DATA_PIN]))
    cg.add(var.set_sample_rate(config[CONF_SAMPLE_RATE]))
    cg.add(var.set_default_path(config[CONF_PATH]))
    cg.add(var.set_buffer_bytes(config[CONF_BUFFER_BYTES]))
