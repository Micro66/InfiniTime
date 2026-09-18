#pragma once
#include <lvgl/lvgl.h>
#include <cstdint>
#include <string>

namespace Esp32 {
  // Owns a separate screen so wake restores the application without recreating it.
  class AmbientDisplay {
  public:
    AmbientDisplay();
    ~AmbientDisplay();
    AmbientDisplay(const AmbientDisplay&) = delete;
    AmbientDisplay& operator=(const AmbientDisplay&) = delete;
    void Refresh(unsigned battery, bool charging);

  private:
    lv_obj_t *previous, *root, *clock, *date, *power;
    std::string lastTime, lastDate, lastPower;
    uint32_t lastPosition = UINT32_MAX;
  };
}
