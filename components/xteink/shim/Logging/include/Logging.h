#pragma once
// esphome-xteink shim. The FreeInk SDK's FrontlightManager logs through the
// consuming firmware's <Logging.h> (crosspoint's). Libraries can't see ESPHome's
// src/ tree, so route to the IDF logger instead (compiled at the SDK's level).
#include <esp_log.h>
#define LOG_ERR(fmt, ...) ESP_LOGE("xteink.sdk", fmt, ##__VA_ARGS__)
#define LOG_INF(fmt, ...) ESP_LOGI("xteink.sdk", fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) ESP_LOGD("xteink.sdk", fmt, ##__VA_ARGS__)
#define logPrintf(fmt, ...) ESP_LOGD("xteink.sdk", fmt, ##__VA_ARGS__)
