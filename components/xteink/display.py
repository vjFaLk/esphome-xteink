import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display
from esphome.const import CONF_ID, CONF_LAMBDA

from . import CONF_XTEINK_ID, Xteink, get_model, xteink_ns, FILTER_SOURCE_FILES  # noqa: F401

DEPENDENCIES = ["xteink"]

XteinkDisplay = xteink_ns.class_(
    "XteinkDisplay", cg.PollingComponent, display.DisplayBuffer
)
XteinkDisplayRef = XteinkDisplay.operator("ref")

CONF_REFRESH_MODE = "refresh_mode"
CONF_FULL_REFRESH_EVERY = "full_refresh_every"
CONF_GRAYSCALE = "grayscale"
CONF_GRAYSCALE_STRATEGY = "grayscale_strategy"
CONF_GRAYSCALE_SETTLE_TIME = "grayscale_settle_time"
CONF_GRAYSCALE_WINDOWED_UPLOAD = "grayscale_windowed_upload"

REFRESH_MODES = {
    "full": 0,
    "half": 1,
    "fast": 2,
}

GRAYSCALE_STRATEGIES = {
    "native": 0,
    "dither": 1,
    "adaptive": 2,
}

CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(XteinkDisplay),
        cv.GenerateID(CONF_XTEINK_ID): cv.use_id(Xteink),
        cv.Optional(CONF_REFRESH_MODE, default="half"): cv.enum(
            REFRESH_MODES, lower=True
        ),
        cv.Optional(CONF_FULL_REFRESH_EVERY, default=0): cv.int_range(min=0),
        cv.Optional(CONF_GRAYSCALE, default=False): cv.boolean,
        cv.Optional(CONF_GRAYSCALE_STRATEGY): cv.enum(
            GRAYSCALE_STRATEGIES, lower=True
        ),
        cv.Optional(CONF_GRAYSCALE_SETTLE_TIME, default="5s"): (
            cv.positive_time_period_milliseconds
        ),
        cv.Optional(CONF_GRAYSCALE_WINDOWED_UPLOAD, default=False): cv.boolean,
    }
).extend(cv.polling_component_schema("1s"))


def _final_validate(config):
    if config[CONF_GRAYSCALE] and get_model() != "x4_pro":
        raise cv.Invalid(
            "grayscale currently requires the X4 Pro's PSRAM; use monochrome on X4/X3"
        )
    if not config[CONF_GRAYSCALE] and CONF_GRAYSCALE_STRATEGY in config:
        raise cv.Invalid("grayscale_strategy requires grayscale: true")
    if config[CONF_GRAYSCALE_WINDOWED_UPLOAD] and not config[CONF_GRAYSCALE]:
        raise cv.Invalid("grayscale_windowed_upload requires grayscale: true")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_parent(await cg.get_variable(config[CONF_XTEINK_ID])))
    cg.add(var.set_refresh_mode(config[CONF_REFRESH_MODE]))
    cg.add(var.set_full_refresh_every(config[CONF_FULL_REFRESH_EVERY]))
    cg.add(var.set_grayscale(config[CONF_GRAYSCALE]))
    cg.add(var.set_grayscale_windowed_upload(config[CONF_GRAYSCALE_WINDOWED_UPLOAD]))
    cg.add(
        var.set_grayscale_strategy(
            config.get(CONF_GRAYSCALE_STRATEGY, GRAYSCALE_STRATEGIES["native"])
        )
    )
    cg.add(
        var.set_grayscale_settle_time(
            config[CONF_GRAYSCALE_SETTLE_TIME].total_milliseconds
        )
    )
    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(XteinkDisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
    await display.register_display(var, config)
