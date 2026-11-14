from esphome.components import touchscreen
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["touchscreen"]
TestTouchscreen = cg.global_ns.class_("TestTouchscreen", touchscreen.Touchscreen)
CONFIG_SCHEMA = touchscreen.TOUCHSCREEN_SCHEMA.extend(
    {cv.GenerateID(): cv.declare_id(TestTouchscreen)}
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await touchscreen.register_touchscreen(var, config)
