#pragma once

#include "displayapp/screens/Screen.h"
#include "components/datetime/DateTimeController.h"
#include "components/battery/BatteryController.h"
#include "systemtask/WakeLock.h"
#include <Preferences.h>

namespace Esp32 {
  // One LVGL object and refresh task per screen; no framebuffer or per-frame objects.
  class RoundArtwork : public Pinetime::Applications::Screens::Screen {
  public:
    explicit RoundArtwork(uint32_t period);
    ~RoundArtwork() override;

  protected:
    void Refresh() override;
    virtual void Draw(const lv_area_t* clip) = 0;

    virtual void Tap(int x, int y) {
    }

    uint32_t frameTime = 0;

  private:
    lv_obj_t* surface;
    lv_task_t* task;
    lv_point_t pressStart {};
    static lv_design_res_t Design(lv_obj_t*, const lv_area_t*, lv_design_mode_t);
    static void Event(lv_obj_t*, lv_event_t);
  };

  class ArtWatchFace : public RoundArtwork {
  public:
    enum class Style { Orbit, Studio, Pulse };
    ArtWatchFace(Style style, Pinetime::Controllers::DateTime& clock, const Pinetime::Controllers::Battery& battery);

  private:
    void Refresh() override;
    void Draw(const lv_area_t* clip) override;
    Style style;
    Pinetime::Controllers::DateTime& clock;
    const Pinetime::Controllers::Battery& battery;
  };

  class Badge : public RoundArtwork {
  public:
    explicit Badge(Pinetime::System::SystemTask& system);
    bool OnTouchEvent(Pinetime::Applications::TouchEvents event) override;

    void SetTheme(unsigned value);

    unsigned Theme() const {
      return theme;
    }

    bool Pinned() const {
      return pinned;
    }

    unsigned Reactions() const {
      return reactions;
    }

  private:
    void Draw(const lv_area_t* clip) override;
    void Tap(int x, int y) override;
    Pinetime::System::WakeLock wakeLock;
    Preferences preferences;
    unsigned theme = 0, reactions = 0;
    bool pinned = false;
    uint32_t reactionStart = 0;
  };
}
