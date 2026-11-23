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

CONF_DITHER_LUMINANCE = "dither_luminance"
CONF_DITHER_CURVES = "dither_curves"
CONF_STABLE_GRAY_FRAMES = "stable_gray_frames"
CONF_STABLE_GRAY_LUMINANCE = "stable_gray_luminance"


def _ordered_stable_values(values):
    if values != sorted(set(values)):
        raise cv.Invalid("stable gray profile values must increase from G1 to G3")
    return values

def _luminances(values):
    active = [value for value in values if value]
    if not active or len(set(active)) != len(active):
        raise cv.Invalid("dither_luminance requires distinct active interior shades; 0 omits a shade")
    return values

def _coverage_curve(values):
    if not values:
        return values
    if len(values) != 17 or values[0] != 0 or values[-1] != 255 or values != sorted(values):
        raise cv.Invalid("each dither curve must have 17 nondecreasing steps from 0 to 255, or be empty")
    return values

CONF_REFRESH_MODE = "refresh_mode"
CONF_FULL_REFRESH_EVERY = "full_refresh_every"
CONF_GRAYSCALE = "grayscale"
CONF_GRAYSCALE_STRATEGY = "grayscale_strategy"
CONF_GRAYSCALE_SETTLE_TIME = "grayscale_settle_time"
CONF_GRAYSCALE_WINDOWED_UPLOAD = "grayscale_windowed_upload"
CONF_GRAYSCALE_UPDATE_MARGIN = "grayscale_update_margin"

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
        cv.Optional(CONF_GRAYSCALE_UPDATE_MARGIN, default=0): cv.int_range(min=0, max=2),
        cv.Optional(CONF_DITHER_LUMINANCE): cv.All(
            cv.ensure_list(cv.int_range(min=0, max=254)), cv.Length(min=6, max=6), _luminances
        ),
        cv.Optional(CONF_DITHER_CURVES): cv.All(
            cv.ensure_list(cv.All(cv.ensure_list(cv.uint8_t), _coverage_curve)),
            cv.Length(min=2, max=7),
        ),
        cv.Optional(CONF_STABLE_GRAY_FRAMES): cv.All(
            cv.ensure_list(cv.int_range(min=1, max=30)), cv.Length(min=3, max=3),
            _ordered_stable_values,
        ),
        cv.Optional(CONF_STABLE_GRAY_LUMINANCE): cv.All(
            cv.ensure_list(cv.int_range(min=1, max=254)), cv.Length(min=3, max=3),
            _ordered_stable_values,
        ),
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
    if (CONF_STABLE_GRAY_FRAMES in config) != (CONF_STABLE_GRAY_LUMINANCE in config):
        raise cv.Invalid("stable_gray_frames and stable_gray_luminance must be set together")
    if CONF_DITHER_CURVES in config:
        if CONF_DITHER_LUMINANCE not in config:
            raise cv.Invalid("dither_curves requires measured dither_luminance")
        pairs = 1 + sum(value > 0 for value in config[CONF_DITHER_LUMINANCE])
        if len(config[CONF_DITHER_CURVES]) != pairs:
            raise cv.Invalid(f"dither_curves requires {pairs} entries for the active shade pairs")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.set_parent(await cg.get_variable(config[CONF_XTEINK_ID])))
    cg.add(var.set_refresh_mode(config[CONF_REFRESH_MODE]))
    cg.add(var.set_full_refresh_every(config[CONF_FULL_REFRESH_EVERY]))
    cg.add(var.set_grayscale(config[CONF_GRAYSCALE]))
    cg.add(var.set_grayscale_windowed_upload(config[CONF_GRAYSCALE_WINDOWED_UPLOAD]))
    cg.add(var.set_grayscale_update_margin(config[CONF_GRAYSCALE_UPDATE_MARGIN]))
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
    for shade, luma in enumerate(config.get(CONF_DITHER_LUMINANCE, [])):
        cg.add(var.set_dither_luminance(shade, luma))
    for pair, curve in enumerate(config.get(CONF_DITHER_CURVES, [])):
        for step, luma in enumerate(curve):
            cg.add(var.set_dither_curve(pair, step, luma))
    for shade, (frames, luma) in enumerate(zip(
        config.get(CONF_STABLE_GRAY_FRAMES, []), config.get(CONF_STABLE_GRAY_LUMINANCE, [])
    )):
        cg.add(var.set_stable_gray_profile(shade, frames, luma))
    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(XteinkDisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
    await display.register_display(var, config)
