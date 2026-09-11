#include "RoundArtwork.h"
#include "Hardware.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "RoundPaint.h"

namespace Esp32 {
  RoundArtwork::RoundArtwork(uint32_t period) {
    surface = lv_obj_create(lv_scr_act(), nullptr);
    lv_obj_set_pos(surface, 0, 0);
    lv_obj_set_size(surface, 466, 466);
    lv_obj_set_style_local_border_width(surface, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    surface->user_data = this;
    lv_obj_set_design_cb(surface, Design);
    lv_obj_set_event_cb(surface, Event);
    task = lv_task_create(RefreshTaskCallback, period, LV_TASK_PRIO_MID, this);
    frameTime = lv_tick_get();
  }

  RoundArtwork::~RoundArtwork() {
    lv_task_del(task);
    lv_obj_del(surface);
  }

  void RoundArtwork::Refresh() {
    frameTime = lv_tick_get();
    lv_obj_invalidate(surface);
  }

  lv_design_res_t RoundArtwork::Design(lv_obj_t* obj, const lv_area_t* clip, lv_design_mode_t mode) {
    if (mode == LV_DESIGN_COVER_CHK)
      return LV_DESIGN_RES_COVER;
    if (mode == LV_DESIGN_DRAW_MAIN)
      static_cast<RoundArtwork*>(obj->user_data)->Draw(clip);
    return LV_DESIGN_RES_OK;
  }

  void RoundArtwork::Event(lv_obj_t* obj, lv_event_t event) {
    auto* self = static_cast<RoundArtwork*>(obj->user_data);
    auto* input = lv_indev_get_act();
    if (!input)
      return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (event == LV_EVENT_PRESSED)
      self->pressStart = point;
    if (event == LV_EVENT_SHORT_CLICKED && std::abs(point.x - self->pressStart.x) < 20 && std::abs(point.y - self->pressStart.y) < 20)
      self->Tap(point.x, point.y);
  }

  ArtWatchFace::ArtWatchFace(Style style, Pinetime::Controllers::DateTime& clock, const Pinetime::Controllers::Battery& battery)
    : RoundArtwork(1000), style(style), clock(clock), battery(battery) {
    Refresh();
  }

  void ArtWatchFace::Refresh() {
    clock.CurrentDateTime();
    RoundArtwork::Refresh();
  }

  void ArtWatchFace::Draw(const lv_area_t* clip) {
    Paint p {clip};
    char time[16], date[32], power[24], seconds[8];
    const bool valid = Hardware::ClockValid();
    snprintf(time, sizeof(time), valid ? "%02u:%02u" : "--:--", clock.Hours(), clock.Minutes());
    snprintf(date, sizeof(date), "%s  %02u %s", clock.DayOfWeekShortToString(), clock.Day(), clock.MonthShortToString());
    snprintf(power, sizeof(power), "%u%% %s", battery.PercentRemaining(), battery.IsCharging() ? "CHG" : "PWR");
    snprintf(seconds, sizeof(seconds), valid ? "%02u" : "--", clock.Seconds());
    if (style == Style::Orbit) {
      p.box(0, 0, 466, 466, 0x050a12);
      p.arc(233, 233, 219, 0, 360, 3, 0x172939);
      for (int i = 0; i < 60; ++i)
        p.line(polar(i * 6, 203), polar(i * 6, i % 5 == 0 ? 193 : 199), i % 5 == 0 ? 3 : 1, 0x38536b);
      p.arc(233, 233, 219, 270, 270 + std::max(1, clock.Seconds() * 6), 5, 0x70efd2);
      auto satellite = polar(clock.Seconds() * 6, 217);
      p.dot(satellite.x, satellite.y, 7, 0xf2fffa);
      p.text("O R B I T", 96, 0x70efd2);
      p.text(time, 171, 0xf5fafb, &jetbrains_mono_76);
      p.text(valid ? date : "SYNC CLOCK VIA USB", 266, 0x93aabc);
      p.box(151, 316, 164, 37, 0x112936, 18);
      p.text(power, 322, 0x70efd2);
      p.text(seconds, 361, 0x92a8b8, &jetbrains_mono_42);
    } else if (style == Style::Studio) {
      p.box(0, 0, 466, 466, 0xf1e9d9);
      p.arc(233, 233, 219, 0, 360, 2, 0xd1c6b2);
      for (int i = 0; i < 60; ++i)
        p.line(polar(i * 6, 204), polar(i * 6, i % 5 == 0 ? 189 : 199), i % 5 == 0 ? 4 : 1, 0x454a42);
      p.text("12", 47, 0x333b35, &jetbrains_mono_42);
      p.text("6", 364, 0x333b35, &jetbrains_mono_42);
      p.text("9", 212, 0x333b35, &jetbrains_mono_42, 50, 50);
      p.text("3", 212, 0x333b35, &jetbrains_mono_42, 366, 50);
      p.text("S T U D I O", 126, 0x777866);
      p.text(valid ? date : "SYNC CLOCK", 299, 0x777866);
      p.text(power, 328, 0x777866);
      if (valid) {
        p.line(polar(clock.Hours() * 30 + clock.Minutes() * 0.5f + 180, 18),
               polar(clock.Hours() * 30 + clock.Minutes() * 0.5f, 99),
               12,
               0x303f3c);
        p.line(polar(clock.Minutes() * 6 + clock.Seconds() * 0.1f + 180, 22),
               polar(clock.Minutes() * 6 + clock.Seconds() * 0.1f, 153),
               7,
               0x303f3c);
        p.line(polar(clock.Seconds() * 6 + 180, 32), polar(clock.Seconds() * 6, 171), 3, 0xe26843);
      }
      p.dot(233, 233, 11, 0x303f3c);
      p.dot(233, 233, 5, 0xe26843);
    } else {
      p.box(0, 0, 466, 466, 0x141716);
      p.box(0, 233, 466, 233, 0xd9f76b);
      p.text("P U L S E   /   2 4 H", 52, 0xd9f76b);
      char hour[4], minute[4];
      snprintf(hour, sizeof(hour), "%02u", clock.Hours());
      snprintf(minute, sizeof(minute), "%02u", clock.Minutes());
      p.text(valid ? hour : "--", 99, 0xf5f2df, valid ? &open_sans_light : &jetbrains_mono_76);
      p.text(valid ? minute : "--", 249, 0x172016, valid ? &open_sans_light : &jetbrains_mono_76);
      p.box(346, 209, 65, 46, 0xf5f2df, 22);
      p.text(seconds, 219, 0x172016, &jetbrains_mono_bold_20, 346, 65);
      p.text(valid ? date : "SYNC CLOCK VIA USB", 376, 0x273326);
      p.text(power, 404, 0x405334);
    }
  }

  Badge::Badge(Pinetime::System::SystemTask& system) : RoundArtwork(100), wakeLock(system) {
    preferences.begin("infini-badge", false);
    theme = preferences.getUChar("theme", 0) % 3;
  }

  bool Badge::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
    using Pinetime::Applications::TouchEvents;
    if (event != TouchEvents::SwipeLeft && event != TouchEvents::SwipeRight)
      return false;
    theme = (theme + (event == TouchEvents::SwipeLeft ? 1 : 2)) % 3;
    preferences.putUChar("theme", theme);
    reactionStart = 0;
    Refresh();
    return true;
  }

  void Badge::Tap(int x, int y) {
    if (x >= 148 && x <= 318 && y >= 370 && y <= 419) {
      pinned = !pinned;
      if (pinned)
        wakeLock.Lock();
      else
        wakeLock.Release();
    } else {
      reactionStart = lv_tick_get();
      ++reactions;
    }
    Refresh();
  }

  void Badge::Draw(const lv_area_t* clip) {
    Paint p {clip};
    const bool happy = reactionStart != 0 && frameTime - reactionStart < 1800;
    const bool blink = frameTime % 4200 < 180;
    const int bob = std::lround(std::sin(frameTime % 4000 * 2 * Pi / 4000) * 5);
    const uint32_t background[] {0x221a30, 0x091f2b, 0x111832};
    const uint32_t accent[] {0xfbb8cf, 0x8aeadb, 0xf9cd82};
    const uint32_t color = accent[theme];
    p.box(0, 0, 466, 466, background[theme]);
    p.arc(233, 233, 220, 0, 360, 2, theme == 0 ? 0x544057 : 0x2b405c);
    p.text(theme == 0 ? "M O C H I" : theme == 1 ? "B E E P !" : "L I L '  O R B I T", 54, color);
    p.star(92, 157 + bob, 6, color);
    p.star(366, 142 - bob, 8, color);
    p.dot(117, 296 - bob, 3, color);
    p.dot(358, 285 + bob, 3, color);
    if (theme == 0) {
      p.dot(233, 228 + bob, 119, 0x493047);
      p.triangle({131, coord(206 + bob)}, {139, coord(108 + bob)}, {207, coord(164 + bob)}, 0xffe8d1);
      p.triangle({259, coord(164 + bob)}, {327, coord(108 + bob)}, {335, coord(206 + bob)}, 0xffe8d1);
      p.triangle({149, coord(169 + bob)}, {149, coord(129 + bob)}, {185, coord(167 + bob)}, 0xee9db4);
      p.triangle({281, coord(167 + bob)}, {317, coord(129 + bob)}, {317, coord(169 + bob)}, 0xee9db4);
      p.box(132, 157 + bob, 202, 163, 0xffe8d1, 77);
      p.box(156, 250 + bob, 30, 13, 0xf6abbf, 6);
      p.box(280, 250 + bob, 30, 13, 0xf6abbf, 6);
      if (happy || blink) {
        p.arc(192, 230 + bob, 12, 205, 335, 5, 0x493047);
        p.arc(274, 230 + bob, 12, 205, 335, 5, 0x493047);
      } else {
        p.box(184, 217 + bob, 12, 23, 0x493047, 6);
        p.box(270, 217 + bob, 12, 23, 0x493047, 6);
      }
      p.triangle({226, coord(244 + bob)}, {240, coord(244 + bob)}, {233, coord(251 + bob)}, 0xb97085);
      p.arc(225, 252 + bob, 8, 0, 90, 3, 0x493047);
      p.arc(241, 252 + bob, 8, 90, 180, 3, 0x493047);
      p.line(168, 252 + bob, 139, 246 + bob, 2, 0xb78a88);
      p.line(298, 252 + bob, 327, 246 + bob, 2, 0xb78a88);
      p.text(happy ? "PURR-FECT COMPANY" : "A LITTLE SOFTNESS", 332, color);
    } else if (theme == 1) {
      p.line(233, 132 + bob, 233, 156 + bob, 5, 0x8aeadb);
      p.dot(233, 121 + bob, 11, happy ? 0xffc685 : color);
      p.box(117, 201 + bob, 21, 52, color, 9);
      p.box(328, 201 + bob, 21, 52, color, 9);
      p.box(132, 151 + bob, 202, 150, color, 38);
      p.box(149, 173 + bob, 168, 93, 0x102e3b, 24);
      if (happy) {
        p.heart(192, 211 + bob, 13, 0xffb894);
        p.heart(274, 211 + bob, 13, 0xffb894);
      } else {
        p.box(182, 200 + bob, 19, blink ? 4 : 29, 0xa6ffe3, 4);
        p.box(265, 200 + bob, 19, blink ? 4 : 29, 0xa6ffe3, 4);
      }
      p.box(216, 243 + bob, 34, 4, 0x8aeadb, 2);
      p.dot(217, 282 + bob, 4, 0x1b5360);
      p.dot(233, 282 + bob, 4, 0x1b5360);
      p.dot(249, 282 + bob, 4, 0x1b5360);
      p.text(happy ? "YOU MAKE ME BEEP" : "FRIEND MODE: ON", 332, color);
    } else {
      // Include the moon's radius and the bob amplitude in the caption clearance.
      p.arc(233, 215 + bob, 101, 0, 360, 2, 0x364263);
      auto moon = polar(frameTime % 12000 * 0.03f, 101, 233, 215 + bob);
      p.dot(moon.x, moon.y, 10, 0xb4c8ff);
      p.dot(233, 208 + bob, 83, 0xf4c383);
      p.dot(205, 168 + bob, 17, 0xe6ac72);
      p.dot(276, 210 + bob, 12, 0xe6ac72);
      p.arc(233, 213 + bob, 94, 12, 170, 12, 0x9d9bec);
      p.line(199, 201 + bob, 199, (blink ? 201 : 212) + bob, 7, 0x4b3553);
      p.line(251, 201 + bob, 251, (blink ? 201 : 212) + bob, 7, 0x4b3553);
      p.arc(225, 218 + bob, happy ? 14 : 9, 15, 165, 4, 0x4b3553);
      p.text(happy ? "YOU ARE MY UNIVERSE" : "IN YOUR ORBIT", 332, color);
    }
    if (happy) {
      const int lift = (frameTime - reactionStart) / 55;
      p.heart(104, 215 - lift, 10, color);
      p.heart(353, 245 - lift, 8, color);
      p.star(308, 113 - lift / 2, 6, color);
    }
    for (unsigned i = 0; i < 3; ++i)
      p.dot(215 + i * 18, 94, i == theme ? 4 : 2, i == theme ? color : 0x646179);
    p.box(148, 374, 170, 40, pinned ? color : 0x303345, 20);
    p.text(pinned ? "STAY ON: YES" : "STAY ON: NO", 382, pinned ? 0x172330 : 0xd7d9e4);
  }
}
