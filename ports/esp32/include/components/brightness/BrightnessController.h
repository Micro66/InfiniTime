#pragma once
#include <cstdint>

namespace Pinetime::Controllers {
  class BrightnessController {
  public:
    enum class Levels { Off, AlwaysOn, Low, Medium, High };
    void Init();
    void Set(Levels value);

    Levels Level() const {
      return level;
    }

    void Lower();
    void Higher();
    void Step();
    const char* GetIcon();
    const char* ToString();

  private:
    Levels level = Levels::Medium;
  };
}
