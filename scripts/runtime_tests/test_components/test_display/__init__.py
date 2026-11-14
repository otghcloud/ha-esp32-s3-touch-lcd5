from esphome.components import display
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["display"]
TestDisplay = cg.global_ns.class_("TestDisplay", display.DisplayBuffer)
CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend(
    {cv.GenerateID(): cv.declare_id(TestDisplay)}
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
