import esphome.codegen as cg

CODEOWNERS = ["@KrX3D"]
AUTO_LOAD = ["microphone"]

# Namespace exported so microphone.py can import it
waveshare_mic_ns = cg.esphome_ns.namespace("waveshare_mic")