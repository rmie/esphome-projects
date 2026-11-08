import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["text_sensor"]
CODEOWNERS = ["@rmie"]

CONF_OFFSET_CM = "offset_cm"
CONF_BSSID = "bssid"

wifi_ftm_anchor_ns = cg.esphome_ns.namespace("wifi_ftm_anchor")
WifiFtmAnchor = wifi_ftm_anchor_ns.class_("WifiFtmAnchor", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WifiFtmAnchor),
        cv.Optional(CONF_OFFSET_CM, default=0): cv.int_,
        cv.Optional(CONF_BSSID): text_sensor.text_sensor_schema(),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_offset_cm(config[CONF_OFFSET_CM]))

    if CONF_BSSID in config:
        sens = await text_sensor.new_text_sensor(config[CONF_BSSID])
        cg.add(var.set_mac_sensor(sens))
