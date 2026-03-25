import esphome.codegen as cg

CODEOWNERS = ["@KrX3D"]
AUTO_LOAD = ["speaker"]

# Namespace exported so speaker.py can import it
waveshare_audio_ns = cg.esphome_ns.namespace("waveshare_audio")