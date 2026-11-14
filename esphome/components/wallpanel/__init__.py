import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["api", "lvgl"]
AUTO_LOAD = ["json"]

wallpanel_ns = cg.global_ns.namespace("wallpanel")
Dashboard = wallpanel_ns.class_("Dashboard", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(Dashboard)}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
