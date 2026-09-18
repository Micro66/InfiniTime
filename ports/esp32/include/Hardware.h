#pragma once
#include <cstdint>
#include <ctime>
#include <lvgl/lvgl.h>

namespace Esp32 {
  struct PowerState {
    int percent;
    uint16_t millivolts;
    bool present;
    bool charging;
    bool external;
  };

  class Hardware {
  public:
    static void Init();
    static void InitGui();
    static PowerState ReadPower();
    static bool PowerButtonPressed();
    static void SetBrightness(uint8_t value);
    static bool ReadClock(std::tm& value);
    static void WriteClock(const std::tm& value);
    static void Tick();
    static bool TakeSwipe(int& dx, int& dy);
    static void ResetTouch();
    static void Capture();
    static unsigned FlushCount();
    static bool ClockValid();
    static unsigned TouchCount();
    static bool TestTap(int x, int y);
    static bool TestRawTap(int x, int y);
  };
}
