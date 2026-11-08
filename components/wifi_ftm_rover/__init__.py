import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_MAC_ADDRESS,
    CONF_CHANNEL,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_DURATION,
    STATE_CLASS_MEASUREMENT,
    UNIT_METER,
)

UNIT_NANOSECOND = "ns"

AUTO_LOAD = ["sensor"]
CODEOWNERS = ["@rmie"]

CONF_FRAME_COUNT = "frame_count"
CONF_DISTANCE = "distance"
CONF_RTT = "rtt"

wifi_ftm_rover_ns = cg.esphome_ns.namespace("wifi_ftm_rover")
WifiFtmRover = wifi_ftm_rover_ns.class_("WifiFtmRover", cg.PollingComponent)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WifiFtmRover),
            cv.Required(CONF_MAC_ADDRESS): cv.mac_address,
            cv.Optional(CONF_CHANNEL, default=0): cv.int_range(min=0, max=14),
            cv.Optional(CONF_FRAME_COUNT, default=32): cv.one_of(16, 24, 32, 64, int=True),
            cv.Optional(CONF_DISTANCE): sensor.sensor_schema(
                unit_of_measurement=UNIT_METER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_DISTANCE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_RTT): sensor.sensor_schema(
                unit_of_measurement=UNIT_NANOSECOND,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.polling_component_schema("1s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_target_mac(str(config[CONF_MAC_ADDRESS])))
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_frame_count(config[CONF_FRAME_COUNT]))

    if CONF_DISTANCE in config:
        sens = await sensor.new_sensor(config[CONF_DISTANCE])
        cg.add(var.set_distance_sensor(sens))

    if CONF_RTT in config:
        sens = await sensor.new_sensor(config[CONF_RTT])
        cg.add(var.set_rtt_sensor(sens))
