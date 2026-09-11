#pragma once
#include <cstdint>

namespace Pinetime::Controllers {
  class Battery {
  public:
    void ReadPowerState();

    void MeasureVoltage() {
      ReadPowerState();
    }

    uint8_t PercentRemaining() const {
      return percent;
    }

    uint16_t Voltage() const {
      return voltage;
    }

    bool IsCharging() const {
      return charging;
    }

    bool IsPowerPresent() const {
      return externalPower;
    }

    bool IsPresent() const {
      return present;
    }

  private:
    uint8_t percent = 0;
    uint16_t voltage = 0;
    bool charging = false;
    bool externalPower = false;
    bool present = false;
  };
}
