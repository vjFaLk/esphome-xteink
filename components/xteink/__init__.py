"""Xteink X4 / X3 / X4 Pro hub for ESPHome, built on the FreeInk SDK.

The SDK is vendored unmodified under ./sdk (see scripts/sync-sdk.sh). At codegen
time it is copied into <build>/lib/xteink-sdk and every lib is declared as a
PlatformIO lib_dep, so the SDK's own library.json files drive the build.
"""

import hashlib
import json
import shutil
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_MODEL
from esphome.core import CORE
from esphome.helpers import rmtree

xteink_ns = cg.esphome_ns.namespace("xteink")
Xteink = xteink_ns.class_("Xteink", cg.PollingComponent)

CONF_XTEINK_ID = "xteink_id"

# model -> (esp32 variant, FreeInk device flag)
MODELS = {
    "x4": ("esp32c3", "FREEINK_DEVICE_X4"),
    "x3": ("esp32c3", "FREEINK_DEVICE_X3"),
    "x4_pro": ("esp32s3", "FREEINK_DEVICE_X4PRO"),
}

SDK_DIR = Path(__file__).parent / "sdk"
# SDK libraries (paths inside sdk/) that get merged into one PlatformIO library.
SDK_LIBS = [
    "libs/hardware/BoardConfig",
    "libs/display/FreeInkDisplay",
    "libs/hardware/InputManager",
    "libs/hardware/BatteryMonitor",
    "libs/hardware/XteinkDetect",
    "libs/hardware/FrontlightManager",
    "shim/Logging",
]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Xteink),
        cv.Required(CONF_MODEL): cv.one_of(*MODELS, lower=True),
    }
).extend(cv.polling_component_schema("50ms"))


def get_model() -> str:
    return fv.full_config.get()["xteink"][CONF_MODEL]


def require_model(*models):
    """final_validate helper for platforms that only exist on some models."""

    def validator(config):
        model = get_model()
        if model not in models:
            raise cv.Invalid(
                f"This platform is not available on the {model}; only on {', '.join(models)}"
            )
        return config

    return validator


def _final_validate(config):
    from esphome.components.esp32 import get_esp32_variant

    want = MODELS[config[CONF_MODEL]][0]
    have = get_esp32_variant().lower()
    if have != want:
        raise cv.Invalid(
            f"xteink model '{config[CONF_MODEL]}' is an {want} device but esp32 variant is {have}"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate

# ESPHome compiles every source in this folder and #includes every header, so a
# platform's files must be dropped when that ESPHome component isn't in the config
# (e.g. no light/touchscreen headers on an X4 build).
PLATFORM_SOURCES = {
    "display": ["display.h", "display.cpp"],
    "binary_sensor": ["buttons.h", "buttons.cpp"],
    "sensor": ["battery.h", "battery.cpp"],
    "light": ["frontlight.h", "frontlight.cpp"],
    "touchscreen": ["touch.h", "touch.cpp"],
}


def FILTER_SOURCE_FILES() -> list[str]:
    excluded = []
    for domain, files in PLATFORM_SOURCES.items():
        used = any(c.get("platform") == "xteink" for c in CORE.config.get(domain, []))
        if not used:
            excluded.extend(files)
    return excluded


# Bump when _vendor_sdk() changes how it lays out the merged library.
_VENDOR_LAYOUT = "merged-v1"


def _sdk_fingerprint() -> str:
    h = hashlib.sha1(_VENDOR_LAYOUT.encode())
    for f in sorted(SDK_DIR.rglob("*")):
        if f.is_file():
            h.update(str(f.relative_to(SDK_DIR)).encode())
            h.update(f.read_bytes())
    return h.hexdigest()


def _vendor_sdk() -> Path:
    """Merge the SDK libs into one library at <build>/lib/xteink-sdk.

    The SDK libs #include each other (<BoardConfig.h> everywhere), and neither
    PlatformIO with lib_ldf_mode=off nor ESPHome >= 2026.8's library-to-IDF-
    component converter links local libraries to each other. Merging their
    include/ and src/ trees into a single library sidesteps that entirely
    (file names are unique across the libs). Redone only when sdk/ changes.
    """
    dst = CORE.relative_build_path("lib", "xteink-sdk")
    fingerprint = _sdk_fingerprint()
    stamp = dst / ".fingerprint"
    if stamp.is_file() and stamp.read_text() == fingerprint:
        return dst
    if dst.exists():
        rmtree(dst)
    # PlatformIO copies file:// libraries into .piolibdeps; drop those copies too.
    libdeps = CORE.relative_build_path(".piolibdeps")
    if libdeps.exists():
        rmtree(libdeps)
    for lib in SDK_LIBS:
        for sub in ("include", "src"):
            if (SDK_DIR / lib / sub).is_dir():
                shutil.copytree(SDK_DIR / lib / sub, dst / sub, dirs_exist_ok=True)
    (dst / "library.json").write_text(
        json.dumps(
            {
                "name": "xteink-sdk",
                "version": "0.0.0+" + (SDK_DIR / "SDK_COMMIT").read_text().strip()[:12],
                "description": "FreeInk SDK subset merged by esphome-xteink",
                "platforms": "espressif32",
                "frameworks": ["arduino"],
            }
        )
    )
    stamp.write_text(fingerprint)
    return dst


async def to_code(config):
    cg.add_build_flag(f"-D{MODELS[config[CONF_MODEL]][1]}=1")
    # One 48 KB framebuffer; the panel controller's RAM holds the previous frame.
    cg.add_build_flag("-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1")

    # file:///abs/path is understood by PlatformIO (ESPHome <= 2026.7, copied into
    # .piolibdeps) and by ESPHome >= 2026.8's own library-to-IDF-component converter.
    cg.add_library("xteink-sdk", None, _vendor_sdk().resolve().as_uri())
    # esp32 builds with lib_ldf_mode=off, so the SDK's Arduino deps must be listed too.
    cg.add_library("SPI", None)
    cg.add_library("Wire", None)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
