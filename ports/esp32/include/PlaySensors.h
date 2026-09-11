#pragma once
#include <array>
#include <cstdint>

namespace Esp32 {
  struct MotionSample {
    float x = 0, y = 0, z = 0;
    uint32_t serial = 0;
    bool valid = false;
  };

  struct AudioSample {
    std::array<float, 8> bands {};
    float rms = 0;
    uint32_t blocks = 0;
    bool valid = false;
  };

  class PlaySensors {
  public:
    static bool MotionStart();
    static void MotionStop();
    static bool AudioStart();
    static void AudioStop();
    static void Tick();
    static MotionSample Motion();
    static AudioSample Audio();
  };
}
