#!/usr/bin/env python3
"""Compile the real UC8279 X4 driver against a recording host bus."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
SOURCE = LIB / "src"

with tempfile.TemporaryDirectory(prefix="freeink-uc8279-x4-pipeline-") as directory:
    root = Path(directory)
    for folder in ("driver", "bus", "lut"):
        (root / folder).mkdir()
    for name in ("Uc8279X4Driver.cpp", "Uc8279X4Driver.h", "Uc8279X4RefreshState.h", "PanelDriver.h"):
        shutil.copy2(SOURCE / "driver" / name, root / "driver" / name)
    shutil.copy2(SOURCE / "lut/Uc8279X4VendorLuts.h", root / "lut/Uc8279X4VendorLuts.h")
    shutil.copy2(LIB / "include/GrayscaleCapabilities.h", root / "GrayscaleCapabilities.h")
    panel = root / "driver/PanelDriver.h"
    panel.write_text(panel.read_text().replace("../../include/GrayscaleCapabilities.h", "../GrayscaleCapabilities.h"))

    (root / "Arduino.h").write_text(r'''#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#define HIGH 1
#define LOW 0
#define PROGMEM
inline unsigned long millis() { static unsigned long value; return ++value; }
inline void delay(unsigned long) {}
inline int digitalRead(int) { return LOW; }
''')
    (root / "BoardConfig.h").write_text(r'''#pragma once
#include <cstdint>
namespace BoardConfig {
inline struct ActiveConfig {
  uint16_t displayWidth = 8;
  uint16_t displayHeight = 4;
  uint32_t displaySpiHz = 0;
  uint8_t displayControllerVariant = 0x02;
} ACTIVE;
inline bool isX4Classic() { return false; }
}
''')
    (root / "esp_heap_caps.h").write_text(r'''#pragma once
#include <cstdlib>
#include <cstddef>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t size, int) { return std::malloc(size); }
''')
    (root / "bus/EpdBus.h").write_text(r'''#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace freeink {
enum class BusyPolarity : uint8_t { UcIdleHigh };
struct EpdPins { int8_t busy = 0; };
class EpdBus {
 public:
  struct PlaneUpload { uint8_t command; std::vector<uint8_t> data; };
  std::vector<uint8_t> commands;
  std::vector<PlaneUpload> uploads;
  void reset(uint16_t = 0) {}
  void cmd(uint8_t command) { current_ = command; commands.push_back(command); }
  void data(uint8_t) {}
  void data(const uint8_t*, uint16_t) {}
  void beginTxn() {
    if (current_ == 0x10 || current_ == 0x13) uploads.push_back({current_, {}});
  }
  void endTxn() {}
  void rawWriteBytes(const uint8_t* data, uint16_t size) {
    if (!uploads.empty() && uploads.back().command == current_)
      uploads.back().data.insert(uploads.back().data.end(), data, data + size);
  }
  void fillPlane(uint8_t command, uint8_t value, uint16_t height, uint16_t widthBytes) {
    commands.push_back(command);
    uploads.push_back({command, std::vector<uint8_t>(static_cast<size_t>(height) * widthBytes, value)});
    current_ = command;
  }
  void waitBusy(const char* = nullptr) {}
  void waitRefreshComplete(const char* = nullptr) {}
  bool isBusy() const { return false; }
  const EpdPins& pins() const { return pins_; }
 private:
  uint8_t current_ = 0;
  EpdPins pins_{};
};
}
''')

    executable = root / "test_uc8279_x4_pipeline"
    subprocess.run(
        [
            os.environ.get("CXX", "c++"),
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-I" + str(root),
            str(HERE / "test_uc8279_x4_pipeline.cpp"),
            str(root / "driver/Uc8279X4Driver.cpp"),
            "-o",
            str(executable),
        ],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
