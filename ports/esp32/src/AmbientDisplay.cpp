#include "AmbientDisplay.h"
#include "Hardware.h"
#include "displayapp/InfiniTimeTheme.h"
#include <Arduino.h>
#include <cstdio>

namespace Esp32 {
  AmbientDisplay::AmbientDisplay() : previous(lv_scr_act()), root(lv_obj_create(nullptr, nullptr)) {
    lv_obj_set_style_local_bg_color(root, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_bg_opa(root, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    const auto makeLabel = [this](const lv_font_t* font, uint32_t color) {
      auto* obj = lv_label_create(root, nullptr);
      lv_obj_set_style_local_text_font(obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
      lv_obj_set_style_local_text_color(obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
      return obj;
    };
    clock = makeLabel(&jetbrains_mono_76, 0xffffff);
    date = makeLabel(LV_THEME_DEFAULT_FONT_NORMAL, 0xb8bec6);
    power = makeLabel(LV_THEME_DEFAULT_FONT_NORMAL, 0xb8bec6);
    lv_scr_load(root);
  }

  AmbientDisplay::~AmbientDisplay() {
    lv_scr_load(previous);
    lv_obj_del(root);
  }

  void AmbientDisplay::Refresh(unsigned battery, bool charging) {
    std::tm local {};
    char timeText[12] = "--:--", dateText[32] = "SYNC WITH PHONE", powerText[32];
    if (Hardware::ReadClock(local)) {
      strftime(timeText, sizeof(timeText), "%H:%M", &local);
      strftime(dateText, sizeof(dateText), "%a  %d %b", &local);
    }
    snprintf(powerText, sizeof(powerText), "%u%%%s", battery, charging ? "  +" : "");
    // The drift also continues when the wall clock has not been synchronized.
    const auto position = (millis() / 60000) % 9;
    if (lastTime == timeText && lastDate == dateText && lastPower == powerText && lastPosition == position)
      return;
    lastTime = timeText;
    lastDate = dateText;
    lastPower = powerText;
    lastPosition = position;
    lv_label_set_text(clock, timeText);
    lv_label_set_text(date, dateText);
    lv_label_set_text(power, powerText);
    const int x = (int(position % 3) - 1) * 12, y = (int(position / 3) - 1) * 12;
    lv_obj_align(date, root, LV_ALIGN_CENTER, x, y - 65);
    lv_obj_align(clock, root, LV_ALIGN_CENTER, x, y);
    lv_obj_align(power, root, LV_ALIGN_CENTER, x, y + 70);
    // Render only. The foreground application's tasks and animations stay paused.
    _lv_disp_refr_task(_lv_disp_get_refr_task(nullptr));
  }
}
