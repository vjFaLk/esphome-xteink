import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import touchscreen
from esphome.const import CONF_ID

from . import CONF_XTEINK_ID, Xteink, require_model, xteink_ns, FILTER_SOURCE_FILES  # noqa: F401

DEPENDENCIES = ["xteink"]

XteinkTouchscreen = xteink_ns.class_("XteinkTouchscreen", touchscreen.Touchscreen)

CONFIG_SCHEMA = touchscreen.touchscreen_schema().extend(
    {
        cv.GenerateID(): cv.declare_id(XteinkTouchscreen),
        cv.GenerateID(CONF_XTEINK_ID): cv.use_id(Xteink),
    }
)

FINAL_VALIDATE_SCHEMA = require_model("x4_pro")


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_parent(await cg.get_variable(config[CONF_XTEINK_ID])))
    await touchscreen.register_touchscreen(var, config)
