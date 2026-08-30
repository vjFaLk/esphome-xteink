import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

from . import CONF_XTEINK_ID, Xteink, get_model, xteink_ns, FILTER_SOURCE_FILES  # noqa: F401

DEPENDENCIES = ["xteink"]

XteinkButtons = xteink_ns.class_("XteinkButtons", cg.PollingComponent)

# Physical names; index = SDK InputManager::BTN_* (which are named for a reader
# app: back/confirm/left/right = the front row 1-4), 7 = GT911 capacitive home key.
#   button_1..4  front row, left to right
#   up / down    side keys (rocker on X4/X3; the two edge keys on X4 Pro)
#   power        power button
#   home         capacitive home key (X4 Pro)
BUTTONS = ["button_1", "button_2", "button_3", "button_4", "up", "down", "power", "home"]
MODEL_BUTTONS = {
    "x4": BUTTONS[:7],
    "x3": BUTTONS[:7],
    "x4_pro": ["up", "down", "power", "home"],
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(XteinkButtons),
        cv.GenerateID(CONF_XTEINK_ID): cv.use_id(Xteink),
        **{cv.Optional(b): binary_sensor.binary_sensor_schema() for b in BUTTONS},
    }
).extend(cv.polling_component_schema("50ms"))


def _final_validate(config):
    model = get_model()
    for b in BUTTONS:
        if b in config and b not in MODEL_BUTTONS[model]:
            raise cv.Invalid(
                f"The {model} has no '{b}' button; available: {', '.join(MODEL_BUTTONS[model])}"
            )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_XTEINK_ID])))
    for i, b in enumerate(BUTTONS):
        if b in config:
            cg.add(var.set_button(i, await binary_sensor.new_binary_sensor(config[b])))
