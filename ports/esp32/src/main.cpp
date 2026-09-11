#include <Arduino.h>
#include <memory>
#include <cstdlib>
#include <esp_system.h>
#include "Hardware.h"
#include "UsbConsole.h"
using Esp32::Usb;
#include "drivers/SpiNorFlash.h"
#include "components/fs/FS.h"
#include "components/settings/Settings.h"
#include "components/datetime/DateTimeController.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/stopwatch/StopWatchController.h"
#include "systemtask/SystemTask.h"
#include "displayapp/screens/WatchFaceAnalog.h"
#include "displayapp/screens/Calculator.h"
#include "displayapp/screens/StopWatch.h"
#include "displayapp/screens/Twos.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime;
using namespace Pinetime::Applications;
using Esp32::Hardware;

namespace {
  enum class Page { Digital, Analog, Launcher, Calculator, Stopwatch, Twos, Settings };
  Page page = Page::Digital, pending = page;
  bool transition = true, sleeping = false;
  std::unique_ptr<Controllers::Settings> settings;
  std::unique_ptr<Controllers::DateTime> dateTime;
  std::unique_ptr<Screens::Screen> screen;
  Controllers::Battery battery;
  Controllers::Ble ble;
  Controllers::NotificationManager notifications;
  Controllers::StopWatchController stopwatch;
  Controllers::BrightnessController brightness;
  System::SystemTask systemTask;
  lv_obj_t *clockLabel = nullptr, *dateLabel = nullptr, *powerLabel = nullptr;
  uint32_t lastActivity = 0, lastRefresh = 0;
  String serialLine;

  void request(Page next) {
    pending = next;
    transition = true;
    lastActivity = millis();
  }

  lv_obj_t* label(const char* text, int y, lv_color_t color = LV_COLOR_WHITE, const lv_font_t* font = LV_THEME_DEFAULT_FONT_NORMAL) {
    auto* obj = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_text(obj, text);
    lv_obj_set_style_local_text_color(obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, color);
    lv_obj_set_style_local_text_font(obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
    lv_obj_align(obj, nullptr, LV_ALIGN_IN_TOP_MID, 0, y);
    return obj;
  }

  void wake() {
    sleeping = false;
    brightness.Set(settings->GetBrightness());
    lastActivity = millis();
  }

  void sleepDisplay() {
    sleeping = true;
    Hardware::SetBrightness(0);
  }

  void action(lv_obj_t* obj, lv_event_t event) {
    if (event != LV_EVENT_CLICKED)
      return;
    const auto id = reinterpret_cast<uintptr_t>(obj->user_data);
    if (id < 10)
      request(static_cast<Page>(id));
    else if (id == 10) {
      brightness.Step();
      settings->SetBrightness(brightness.Level());
      settings->SaveSettings();
      request(Page::Settings);
    } else if (id == 11) {
      const auto old = settings->GetScreenTimeOut();
      settings->SetScreenTimeOut(old < 30000 ? 30000 : old < 60000 ? 60000 : 15000);
      settings->SaveSettings();
      request(Page::Settings);
    } else if (id == 12 || id == 13) {
      dateTime->SetCurrentTime(dateTime->CurrentDateTime() + std::chrono::minutes(id == 12 ? -1 : 1));
      request(Page::Settings);
    }
  }

  void button(const char* text, int x, int y, int w, int h, uintptr_t id, uint32_t color = 0x202b38) {
    auto* obj = lv_btn_create(lv_scr_act(), nullptr);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_local_radius(obj, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 24);
    lv_obj_set_style_local_bg_color(obj, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
    obj->user_data = reinterpret_cast<void*>(id);
    lv_obj_set_event_cb(obj, action);
    auto* txt = lv_label_create(obj, nullptr);
    lv_label_set_text(txt, text);
  }

  void show() {
    screen.reset();
    lv_obj_clean(lv_scr_act());
    clockLabel = dateLabel = powerLabel = nullptr;
    page = pending;
    transition = false;
    lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    switch (page) {
      case Page::Analog:
        screen = std::make_unique<Screens::WatchFaceAnalog>(*dateTime, battery, ble, notifications, *settings);
        break;
      case Page::Calculator:
        screen = std::make_unique<Screens::Calculator>();
        break;
      case Page::Stopwatch:
        screen = std::make_unique<Screens::StopWatch>(systemTask, stopwatch);
        break;
      case Page::Twos:
        screen = std::make_unique<Screens::Twos>();
        break;
      case Page::Digital:
        label("I N F I N I T I M E", 77, lv_color_hex(0x70e6ca));
        clockLabel = label("--:--", 158, LV_COLOR_WHITE, &jetbrains_mono_76);
        dateLabel = label("", 259, lv_color_hex(0xaeb9c9));
        powerLabel = label("", 310, lv_color_hex(0x70e6ca));
        button("Apps", 163, 357, 140, 50, static_cast<uintptr_t>(Page::Launcher));
        break;
      case Page::Launcher:
        label("YOUR APPS", 58, lv_color_hex(0x70e6ca));
        button("Calculator", 68, 116, 158, 104, static_cast<uintptr_t>(Page::Calculator));
        button("Stopwatch", 240, 116, 158, 104, static_cast<uintptr_t>(Page::Stopwatch));
        button("Twos", 68, 238, 158, 104, static_cast<uintptr_t>(Page::Twos), 0x6f3824);
        button("Settings", 240, 238, 158, 104, static_cast<uintptr_t>(Page::Settings));
        button("Watch", 163, 361, 140, 48, static_cast<uintptr_t>(Page::Digital));
        break;
      case Page::Settings: {
        label("SETTINGS", 48, lv_color_hex(0x70e6ca));
        char text[64];
        snprintf(text, sizeof(text), "Brightness: %s", brightness.ToString());
        button(text, 88, 94, 290, 57, 10);
        snprintf(text, sizeof(text), "Display off: %lus", static_cast<unsigned long>(settings->GetScreenTimeOut() / 1000));
        button(text, 88, 163, 290, 57, 11);
        snprintf(text, sizeof(text), "%s  UTC+8", dateTime->FormattedTime().c_str());
        label(text, 242);
        button("-1 min", 96, 286, 130, 57, 12);
        button("+1 min", 240, 286, 130, 57, 13);
        button("Back", 163, 361, 140, 48, static_cast<uintptr_t>(Page::Launcher));
        break;
      }
    }
    lv_obj_invalidate(lv_scr_act());
    Usb.printf("PAGE %u\n", static_cast<unsigned>(page));
  }

  void command(const String& line) {
    if (line.startsWith("test-raw-tap ")) {
      int x, y;
      char extra;
      if (sscanf(line.c_str(), "test-raw-tap %d %d %c", &x, &y, &extra) == 2) {
        wake();
        Usb.println(Hardware::TestRawTap(x, y) ? "TEST RAW TAP OK" : "TEST RAW TAP INVALID");
      }
      return;
    }
    if (line.startsWith("test-tap ")) {
      int x, y;
      char extra;
      if (sscanf(line.c_str(), "test-tap %d %d %c", &x, &y, &extra) == 2) {
        wake();
        Usb.println(Hardware::TestTap(x, y) ? "TEST TAP OK" : "TEST TAP INVALID");
      }
      return;
    }
    if (line.startsWith("test-swipe ")) {
      const auto direction = line.substring(11);
      TouchEvents event;
      if (direction == "left")
        event = TouchEvents::SwipeLeft;
      else if (direction == "right")
        event = TouchEvents::SwipeRight;
      else if (direction == "up")
        event = TouchEvents::SwipeUp;
      else if (direction == "down")
        event = TouchEvents::SwipeDown;
      else
        return;
      wake();
      if (screen)
        screen->OnTouchEvent(event);
      return;
    }
    if (line == "capture") {
      Hardware::Capture();
      return;
    }
    if (line == "wake") {
      wake();
      return;
    }
    if (line == "sleep") {
      sleepDisplay();
      return;
    }
    if (line.startsWith("page ")) {
      int id = -1;
      char extra;
      if (sscanf(line.c_str(), "page %d %c", &id, &extra) == 1 && id >= 0 && id <= 6) {
        wake();
        request(static_cast<Page>(id));
      }
    } else if (line.startsWith("time ")) {
      int y, m, d, h, min, s;
      char extra;
      if (sscanf(line.c_str(), "time %d-%d-%d %d:%d:%d %c", &y, &m, &d, &h, &min, &s, &extra) == 6 && y >= 2020 && y <= 2099 && m >= 1 &&
          m <= 12 && d >= 1 && d <= 31 && h >= 0 && h < 24 && min >= 0 && min < 60 && s >= 0 && s < 60) {
        std::tm candidate {};
        candidate.tm_year = y - 1900;
        candidate.tm_mon = m - 1;
        candidate.tm_mday = d;
        mktime(&candidate);
        if (candidate.tm_mon == m - 1 && candidate.tm_mday == d) {
          dateTime->SetTime(y, m, d, h, min, s);
          Usb.println("TIME OK");
        } else
          Usb.println("TIME INVALID");
      } else
        Usb.println("TIME INVALID");
    } else if (line == "status") {
      Usb.printf("STATUS page=%u sleep=%d battery=%u mv=%u charging=%d rtc=%d flush=%u heap=%u touch=%u uptime=%lu\n",
                 static_cast<unsigned>(page),
                 sleeping,
                 battery.PercentRemaining(),
                 battery.Voltage(),
                 battery.IsCharging(),
                 Hardware::ClockValid(),
                 Hardware::FlushCount(),
                 ESP.getFreeHeap(),
                 Hardware::TouchCount(),
                 static_cast<unsigned long>(millis()));
    }
  }
}

void setup() {
  Usb.Begin();
  delay(1200);
  Usb.println("InfiniTime ESP32-S3 Waveshare 1.75C starting");
  Hardware::Init();
  Hardware::InitGui();
  static Drivers::SpiNorFlash flash;
  static Controllers::FS filesystem(flash);
  filesystem.Init();
  settings = std::make_unique<Controllers::Settings>(filesystem);
  settings->Init();
  dateTime = std::make_unique<Controllers::DateTime>(*settings);
  ble.DisableRadio();
  battery.ReadPowerState();
  srand(esp_random());
  if (settings->GetBrightness() < Controllers::BrightnessController::Levels::Low ||
      settings->GetBrightness() > Controllers::BrightnessController::Levels::High)
    settings->SetBrightness(Controllers::BrightnessController::Levels::Medium);
  wake();
  show();
  Usb.println("READY");
}

void loop() {
  Hardware::Tick();
  if (!sleeping)
    lv_task_handler();
  const auto now = millis();
  static bool bootWasDown = false;
  const bool bootDown = digitalRead(0) == LOW;
  if (bootDown && !bootWasDown) {
    if (sleeping)
      wake();
    else if (!screen || !screen->OnButtonPushed())
      request(page == Page::Launcher ? Page::Digital : Page::Launcher);
  }
  bootWasDown = bootDown;
  static uint32_t powerPoll = 0;
  if (now - powerPoll > 100) {
    powerPoll = now;
    if (Hardware::PowerButtonPressed()) {
      if (sleeping)
        wake();
      else
        sleepDisplay();
    }
  }
  if (!sleeping && lv_disp_get_inactive_time(nullptr) < 100)
    lastActivity = now;
  int dx, dy;
  if (!sleeping && Hardware::TakeSwipe(dx, dy)) {
    lastActivity = now;
    TouchEvents event = abs(dx) > abs(dy) ? (dx > 0 ? TouchEvents::SwipeRight : TouchEvents::SwipeLeft)
                                          : (dy > 0 ? TouchEvents::SwipeDown : TouchEvents::SwipeUp);
    if (!screen || !screen->OnTouchEvent(event)) {
      if (page == Page::Digital || page == Page::Analog)
        request(abs(dx) > abs(dy) ? (page == Page::Digital ? Page::Analog : Page::Digital) : Page::Launcher);
      else if (dy > 55)
        request(Page::Launcher);
    }
  }
  if (screen && !screen->IsRunning())
    request(Page::Launcher);
  if (transition)
    show();
  if (now - lastRefresh >= 1000) {
    lastRefresh = now;
    battery.ReadPowerState();
    dateTime->CurrentDateTime();
    if (clockLabel) {
      lv_label_set_text(clockLabel, Hardware::ClockValid() ? dateTime->FormattedTime().c_str() : "--:--");
      lv_obj_align(clockLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 158);
      lv_label_set_text_fmt(dateLabel, "%s, %02u %s", dateTime->DayOfWeekShortToString(), dateTime->Day(), dateTime->MonthShortToString());
      lv_obj_align(dateLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 259);
      lv_label_set_text_fmt(powerLabel, "%u%%  %s", battery.PercentRemaining(), battery.IsCharging() ? "CHARGING" : "BATTERY");
      lv_obj_align(powerLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 310);
    }
  }
  if (!sleeping && !systemTask.IsSleepDisabled() && now - lastActivity > settings->GetScreenTimeOut())
    sleepDisplay();
  while (Usb.available()) {
    const char c = Usb.read();
    if (c == '\n') {
      command(serialLine);
      serialLine = "";
    } else if (c != '\r' && serialLine.length() < 100)
      serialLine += c;
  }
  delay(sleeping ? 20 : 5);
}
