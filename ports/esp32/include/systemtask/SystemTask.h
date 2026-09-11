#pragma once
#include "systemtask/Messages.h"

namespace Pinetime::System {
  // The ESP32 shell owns the event loop. Upstream WakeLock messages update its
  // sleep-inhibition count on that same task; no Nordic task/ISR emulation.
  class SystemTask {
  public:
    void PushMessage(Messages message) {
      if (message == Messages::DisableSleeping)
        ++wakeLocks;
      if (message == Messages::EnableSleeping && wakeLocks > 0)
        --wakeLocks;
    }

    bool IsSleepDisabled() const {
      return wakeLocks != 0;
    }

  private:
    unsigned wakeLocks = 0;
  };
}
