#pragma once
#include "RoundArtwork.h"
#include "PlayLogic.h"

namespace Esp32 {
  class FocusService {
  public:
    void Init();
    bool Tick();
    void Toggle();
    void ResetOrDuration();

    const FocusClock& State() const {
      return state;
    }

  private:
    void Save();
    FocusClock state;
    Preferences preferences;
  };

  class FunApp : public RoundArtwork {
  public:
    enum class Kind { Dice, Marble, Music, Garden };
    FunApp(Kind kind, FocusService& focus, Pinetime::System::SystemTask& system);
    ~FunApp() override;

    unsigned DiceValue() const {
      return value;
    }

    unsigned Rolls() const {
      return rolls;
    }

    const MarblePhysics& Marble() const {
      return marble;
    }

    std::array<float, 2> NeutralTilt() const {
      return {zeroX, zeroY};
    }

  private:
    void Refresh() override;
    void Draw(const lv_area_t*) override;
    void Tap(int x, int y) override;
    void Roll();
    Kind kind;
    FocusService& focus;
    Pinetime::System::WakeLock wakeLock;
    bool sensorReady = false;
    unsigned mode = 0, value = 1, rolls = 0;
    uint32_t rollStart = 0, motionSerial = 0, lastStep = 0;
    float zeroX = 0, zeroY = 0, accumulator = 0;
    MarblePhysics marble;
    std::array<float, 8> bands {};
    float loudness = 0;
  };
}
