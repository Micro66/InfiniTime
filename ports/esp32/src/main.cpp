#include <Arduino.h>
#include <memory>
#include <cstdlib>
#include <esp_system.h>
#include "Hardware.h"
#include "UsbConsole.h"
#include "RoundArtwork.h"
#include "FunApps.h"
#include "PlaySensors.h"
#include "PhotoBadge.h"
#include "Companion.h"
#include "AmbientDisplay.h"
#include <array>
#include <algorithm>
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
  enum class Page {
    Digital,
    Analog,
    Launcher,
    Calculator,
    Stopwatch,
    Twos,
    Settings,
    Orbit,
    Studio,
    Pulse,
    Badge,
    Dice,
    Marble,
    Music,
    Garden,
    Photo,
    Pairing,
    Count
  };
  constexpr std::array watchFaces {Page::Digital, Page::Analog, Page::Orbit, Page::Studio, Page::Pulse};
  Page selectedWatch = Page::Digital;
  Preferences uiPreferences;

  bool isWatch(Page candidate) {
    return std::find(watchFaces.begin(), watchFaces.end(), candidate) != watchFaces.end();
  }

  Page page = Page::Digital, pending = page;
  bool transition = true, sleeping = false;
  bool alwaysOn = true;
  std::unique_ptr<Esp32::AmbientDisplay> ambient;
  std::unique_ptr<Controllers::Settings> settings;
  std::unique_ptr<Controllers::DateTime> dateTime;
  std::unique_ptr<Screens::Screen> screen;
  Controllers::Battery battery;
  Controllers::Ble ble;
  Controllers::NotificationManager notifications;
  Controllers::StopWatchController stopwatch;
  Controllers::BrightnessController brightness;
  System::SystemTask systemTask;
  Esp32::FocusService focus;
  std::unique_ptr<Esp32::PhotoStore> photos;
  uint16_t companionAck = 0;
  uint8_t companionResult = 0;
  uint32_t pairingPin = 0;
  Page beforePairing = Page::Digital;
  bool companionReady = false;
  int themeToApply = -1;
  unsigned launcherSheet = 0;
  bool focusNotice = false;
  lv_obj_t *clockLabel = nullptr, *dateLabel = nullptr, *powerLabel = nullptr;
  uint32_t lastActivity = 0, lastRefresh = 0;
  String serialLine;
  enum class TestButton { None, Boot, Power };
  TestButton testButton = TestButton::None;

  void request(Page next) {
    if (isWatch(next) && selectedWatch != next) {
      selectedWatch = next;
      uiPreferences.putUChar("watch", static_cast<uint8_t>(next));
    }
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
    if (sleeping) {
      ambient.reset();
      Hardware::ResetTouch();
      _lv_disp_refr_task(_lv_disp_get_refr_task(nullptr));
    }
    sleeping = false;
    brightness.Set(settings->GetBrightness());
    lastActivity = millis();
  }

  void sleepDisplay() {
    sleeping = true;
    Hardware::SetBrightness(0);
    ambient.reset();
    if (alwaysOn) {
      ambient = std::make_unique<Esp32::AmbientDisplay>();
      ambient->Refresh(battery.PercentRemaining(), battery.IsCharging());
      Hardware::SetBrightness(8);
    }
  }

  bool setAlwaysOn(bool enabled) {
    if (alwaysOn == enabled)
      return true;
    if (uiPreferences.putBool("aod", enabled) != 1)
      return false;
    alwaysOn = enabled;
    if (sleeping)
      sleepDisplay();
    return true;
  }

  void action(lv_obj_t* obj, lv_event_t event) {
    if (event != LV_EVENT_CLICKED)
      return;
    const auto id = reinterpret_cast<uintptr_t>(obj->user_data);
    if (id < static_cast<uintptr_t>(Page::Count))
      request(static_cast<Page>(id));
    else if (id == 100) {
      brightness.Step();
      settings->SetBrightness(brightness.Level());
      settings->SaveSettings();
      request(Page::Settings);
    } else if (id == 101) {
      const auto old = settings->GetScreenTimeOut();
      settings->SetScreenTimeOut(old < 30000 ? 30000 : old < 60000 ? 60000 : 15000);
      settings->SaveSettings();
      request(Page::Settings);
    } else if (id == 106) {
      if (!setAlwaysOn(!alwaysOn))
        Usb.println("AOD SAVE FAILED");
      request(Page::Settings);
    } else if (id == 105) {
      Esp32::Companion::ForgetBonds();
      pairingPin = 0;
      request(Page::Pairing);
    } else if (id == 104) {
      launcherSheet = (launcherSheet + 1) % 3;
      request(Page::Launcher);
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
      case Page::Orbit:
      case Page::Studio:
      case Page::Pulse:
        screen = std::make_unique<Esp32::ArtWatchFace>(
          static_cast<Esp32::ArtWatchFace::Style>(static_cast<int>(page) - static_cast<int>(Page::Orbit)),
          *dateTime,
          battery);
        break;
      case Page::Badge:
        screen = std::make_unique<Esp32::Badge>(systemTask);
        if (themeToApply >= 0) {
          static_cast<Esp32::Badge*>(screen.get())->SetTheme(themeToApply);
          themeToApply = -1;
        }
        break;
      case Page::Dice:
      case Page::Marble:
      case Page::Music:
      case Page::Garden:
        screen = std::make_unique<Esp32::FunApp>(static_cast<Esp32::FunApp::Kind>(static_cast<int>(page) - static_cast<int>(Page::Dice)),
                                                 focus,
                                                 systemTask);
        break;
      case Page::Photo:
        screen = std::make_unique<Esp32::PhotoBadge>(*photos, systemTask);
        break;
      case Page::Digital:
        label("I N F I N I T I M E", 77, lv_color_hex(0x70e6ca));
        clockLabel = label("--:--", 158, LV_COLOR_WHITE, &jetbrains_mono_76);
        dateLabel = label("", 259, lv_color_hex(0xaeb9c9));
        powerLabel = label("", 310, lv_color_hex(0x70e6ca));
        button("Apps", 163, 357, 140, 50, static_cast<uintptr_t>(Page::Launcher));
        break;
      case Page::Launcher: {
        constexpr Page entries[3][4] {{Page::Calculator, Page::Stopwatch, Page::Twos, Page::Settings},
                                      {Page::Badge, Page::Dice, Page::Marble, Page::Music},
                                      {Page::Garden, Page::Photo, Page::Badge, Page::Settings}};
        constexpr const char* names[3][4] {{"Calculator", "Stopwatch", "Twos", "Settings"},
                                           {"Badge", "Lucky Dice", "Gravity", "Sound Buddy"},
                                           {"Garden", "Photo Badge", "Badge", "Settings"}};
        char title[30];
        snprintf(title, sizeof(title), "YOUR APPS  %u / 3", launcherSheet + 1);
        label(title, 58, lv_color_hex(0x70e6ca));
        for (unsigned i = 0; i < 4; ++i)
          button(names[launcherSheet][i],
                 i % 2 ? 240 : 68,
                 i / 2 ? 238 : 116,
                 158,
                 104,
                 static_cast<uintptr_t>(entries[launcherSheet][i]),
                 i == 2 ? 0x6f3824 : 0x202b38);
        button("Next", 92, 357, 136, 48, 104, 0x694865);
        button("Watch", 240, 357, 136, 48, static_cast<uintptr_t>(selectedWatch));
        break;
      }
      case Page::Settings: {
        label("SETTINGS", 48, lv_color_hex(0x70e6ca));
        char text[64];
        snprintf(text, sizeof(text), "Brightness: %s", brightness.ToString());
        button(text, 88, 88, 290, 50, 100);
        snprintf(text, sizeof(text), "Idle after: %lus", static_cast<unsigned long>(settings->GetScreenTimeOut() / 1000));
        button(text, 88, 148, 290, 50, 101);
        button(alwaysOn ? "Always-on: ON" : "Always-on: OFF", 88, 208, 290, 50, 106);
        const int offset = dateTime->UtcOffset() * 15;
        snprintf(text,
                 sizeof(text),
                 "%s UTC%c%d:%02d",
                 dateTime->FormattedTime().c_str(),
                 offset < 0 ? '-' : '+',
                 abs(offset) / 60,
                 abs(offset) % 60);
        label(text, 272);
        button("Pair iPhone", 88, 311, 290, 50, static_cast<uintptr_t>(Page::Pairing));
        button("Back", 163, 373, 140, 44, static_cast<uintptr_t>(Page::Launcher));
        break;
      }
      case Page::Pairing: {
        label("BADGE STUDIO", 70, lv_color_hex(0x70e6ca));
        label(companionReady ? "Open the iPhone app" : "Bluetooth unavailable", 135);
        char pin[16];
        if (pairingPin)
          snprintf(pin, sizeof(pin), "%06lu", static_cast<unsigned long>(pairingPin));
        else
          strcpy(pin, "------");
        label(pin, 195, LV_COLOR_WHITE, &jetbrains_mono_42);
        label("Enter this code on iPhone", 260);
        button("Forget phones", 103, 310, 260, 45, 105);
        button("Back", 163, 370, 140, 44, static_cast<uintptr_t>(Page::Settings));
        break;
      }
      case Page::Count:
        break;
    }
    lv_obj_invalidate(lv_scr_act());
    Usb.printf("PAGE %u\n", static_cast<unsigned>(page));
  }

  void swipe(TouchEvents event) {
    lastActivity = millis();
    if (screen && screen->OnTouchEvent(event))
      return;
    if (page == Page::Launcher && (event == TouchEvents::SwipeLeft || event == TouchEvents::SwipeRight)) {
      launcherSheet = (launcherSheet + (event == TouchEvents::SwipeLeft ? 1 : 2)) % 3;
      request(Page::Launcher);
    } else if (isWatch(page)) {
      if (event == TouchEvents::SwipeLeft || event == TouchEvents::SwipeRight) {
        const auto index = std::find(watchFaces.begin(), watchFaces.end(), page) - watchFaces.begin();
        request(watchFaces[(index + (event == TouchEvents::SwipeLeft ? 1 : watchFaces.size() - 1)) % watchFaces.size()]);
      } else
        request(Page::Launcher);
    } else if (event == TouchEvents::SwipeDown)
      request(Page::Launcher);
  }

  Esp32::CompanionProtocol::Packet companionState() {
    using namespace Esp32::CompanionProtocol;
    unsigned theme = 0;
    if (page == Page::Badge && screen)
      theme = static_cast<Esp32::Badge*>(screen.get())->Theme();
    else {
      Preferences badgeSettings;
      badgeSettings.begin("infini-badge", true);
      theme = badgeSettings.getUChar("theme", 0) % 3;
    }
    Packet value {1,
                  companionResult,
                  uint8_t(companionAck),
                  uint8_t(companionAck >> 8),
                  battery.PercentRemaining(),
                  uint8_t(settings->GetBrightness()),
                  uint8_t(std::find(watchFaces.begin(), watchFaces.end(), selectedWatch) - watchFaces.begin()),
                  uint8_t(theme)};
    const auto timeout = settings->GetScreenTimeOut() / 1000;
    value[8] = timeout;
    value[9] = timeout >> 8;
    value[10] = (sleeping ? 1 : 0) | (battery.IsCharging() ? 2 : 0) | (Hardware::ClockValid() ? 4 : 0) | (alwaysOn ? 8 : 0) | 16;
    value[11] = static_cast<uint8_t>(page);
    Put32(value.data() + 12, Hardware::ClockValid() ? time(nullptr) : 0);
    Put32(value.data() + 16, dateTime->UtcOffset() * 900);
    return value;
  }

  bool synchronizeClock(uint32_t utcSeconds, int32_t offsetSeconds) {
    if (!Esp32::CompanionProtocol::Valid({1, 0, utcSeconds, static_cast<uint32_t>(offsetSeconds)}))
      return false;
    dateTime->SetTimeZone(offsetSeconds / 900, 0);
    const time_t utc = utcSeconds;
    std::tm local {};
    localtime_r(&utc, &local);
    dateTime->SetTime(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    if (page == Page::Settings)
      request(page);
    return true;
  }

  void companionCommand(const Esp32::CompanionProtocol::Command& cmd) {
    using namespace Esp32;
    companionAck = cmd.sequence;
    companionResult = 0;
    if (!CompanionProtocol::Valid(cmd)) {
      companionResult = cmd.op == 255 ? 2 : 1;
      return;
    }
    switch (cmd.op) {
      case 1:
        synchronizeClock(cmd.a, int32_t(cmd.b));
        break;
      case 2:
        if (cmd.a)
          wake();
        else
          sleepDisplay();
        break;
      case 3:
        settings->SetBrightness(static_cast<Controllers::BrightnessController::Levels>(cmd.a));
        settings->SaveSettings();
        if (!sleeping)
          brightness.Set(settings->GetBrightness());
        if (page == Page::Settings)
          request(page);
        break;
      case 4:
        settings->SetScreenTimeOut(cmd.a * 1000);
        settings->SaveSettings();
        lastActivity = millis();
        if (page == Page::Settings)
          request(page);
        break;
      case 5:
        wake();
        request(watchFaces[cmd.a]);
        show();
        break;
      case 6:
        themeToApply = cmd.a;
        wake();
        request(Page::Badge);
        show();
        break;
      case 7:
        if (!photos->Begin(PhotoStore::Owner::Ble, cmd.c, cmd.a, cmd.b))
          companionResult = 2;
        else {
          wake();
          request(Page::Photo);
          show();
        }
        break;
      case 8:
        if (!photos->Commit(PhotoStore::Owner::Ble, cmd.a))
          companionResult = 3;
        break;
      case 9:
        photos->Cancel(PhotoStore::Owner::Ble, cmd.a);
        break;
      case 10:
        wake();
        request(static_cast<Page>(cmd.a));
        show();
        break;
      case 11:
        if (!setAlwaysOn(cmd.a != 0))
          companionResult = 3;
        if (page == Page::Settings)
          request(page);
        break;
    }
  }

  void command(const String& line) {
    if (line == "test-button boot" || line == "test-button pwr") {
      testButton = line == "test-button boot" ? TestButton::Boot : TestButton::Power;
      Usb.println("TEST BUTTON QUEUED");
      return;
    }
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
      swipe(event);
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
      if (sscanf(line.c_str(), "page %d %c", &id, &extra) == 1 && id >= 0 && id < static_cast<int>(Page::Count)) {
        wake();
        request(static_cast<Page>(id));
      }
    } else if (line.startsWith("time-utc ")) {
      unsigned long utc;
      int offset;
      char extra;
      const bool parsed = sscanf(line.c_str(), "time-utc %lu %d %c", &utc, &offset, &extra) == 2;
      Usb.println(parsed && synchronizeClock(utc, offset) ? "TIME OK" : "TIME INVALID");
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
      Usb.printf("DISPLAY aod_enabled=%d ambient=%d\n", alwaysOn, ambient != nullptr);
      Usb.printf("STATUS page=%u sleep=%d battery=%u mv=%u charging=%d rtc=%d flush=%u heap=%u touch=%u uptime=%lu timeout=%lu\n",
                 static_cast<unsigned>(page),
                 sleeping,
                 battery.PercentRemaining(),
                 battery.Voltage(),
                 battery.IsCharging(),
                 Hardware::ClockValid(),
                 Hardware::FlushCount(),
                 ESP.getFreeHeap(),
                 Hardware::TouchCount(),
                 static_cast<unsigned long>(millis()),
                 static_cast<unsigned long>(settings->GetScreenTimeOut()));
      if (page == Page::Badge && screen) {
        const auto* badge = static_cast<Esp32::Badge*>(screen.get());
        Usb.printf("BADGE theme=%u pinned=%d reactions=%u\n", badge->Theme(), badge->Pinned(), badge->Reactions());
      }
      Usb.printf("COMPANION ready=%d connected=%d paired=%d ack=%u result=%u epoch=%lu zone=%d notify_errors=%u\n",
                 companionReady,
                 Esp32::Companion::Connected(),
                 Esp32::Companion::Paired(),
                 companionAck,
                 companionResult,
                 Hardware::ClockValid() ? static_cast<unsigned long>(time(nullptr)) : 0,
                 dateTime->UtcOffset() * 900,
                 Esp32::Companion::NotificationFailures());
      const auto transfer = photos->Status();
      Usb.printf("TRANSFER state=%u error=%u received=%lu total=%lu\n",
                 transfer[1],
                 transfer[2],
                 static_cast<unsigned long>(Esp32::CompanionProtocol::U32(transfer.data() + 8)),
                 static_cast<unsigned long>(Esp32::CompanionProtocol::U32(transfer.data() + 12)));
      const auto motion = Esp32::PlaySensors::Motion();
      const auto audio = Esp32::PlaySensors::Audio();
      Usb.printf("PLAY sheet=%u motion=%d ax=%.3f ay=%.3f az=%.3f samples=%lu audio=%d rms=%.5f blocks=%lu\n",
                 launcherSheet,
                 motion.valid,
                 motion.x,
                 motion.y,
                 motion.z,
                 static_cast<unsigned long>(motion.serial),
                 audio.valid,
                 audio.rms,
                 static_cast<unsigned long>(audio.blocks));
      const auto& timer = focus.State();
      Usb.printf("GARDEN phase=%u running=%d remaining=%lu flowers=%lu\n",
                 static_cast<unsigned>(timer.phase),
                 timer.running,
                 static_cast<unsigned long>(timer.remaining),
                 static_cast<unsigned long>(timer.flowers));
      if (page == Page::Dice && screen) {
        auto* app = static_cast<Esp32::FunApp*>(screen.get());
        Usb.printf("DICE value=%u rolls=%u\n", app->DiceValue(), app->Rolls());
      }
      if (page == Page::Marble && screen) {
        const auto& ball = static_cast<Esp32::FunApp*>(screen.get())->Marble();
        const auto neutral = static_cast<Esp32::FunApp*>(screen.get())->NeutralTilt();
        Usb.printf("MARBLE x=%.2f y=%.2f score=%u zero_x=%.3f zero_y=%.3f\n", ball.x, ball.y, ball.score, neutral[0], neutral[1]);
      }
      if (page == Page::Photo && screen) {
        auto* app = static_cast<Esp32::PhotoBadge*>(screen.get());
        Usb.printf("PHOTO wifi=%d image=%d clients=%u qr=%s dns=%u\n",
                   app->Connected(),
                   app->HasPhoto(),
                   app->Clients(),
                   app->PageCode() ? "page" : "wifi",
                   app->DnsQueries());
      }
    }
  }
}

void setup() {
  Usb.Begin();
  delay(1200);
  Usb.println("InfiniTime ESP32-S3 Waveshare 1.75C starting");
  Hardware::Init();
  Hardware::InitGui();
  uiPreferences.begin("infini-ui", false);
  alwaysOn = uiPreferences.getBool("aod", true);
  const auto savedWatch = static_cast<Page>(uiPreferences.getUChar("watch", 0));
  if (isWatch(savedWatch))
    selectedWatch = pending = savedWatch;
  static Drivers::SpiNorFlash flash;
  static Controllers::FS filesystem(flash);
  filesystem.Init();
  settings = std::make_unique<Controllers::Settings>(filesystem);
  settings->Init();
  dateTime = std::make_unique<Controllers::DateTime>(*settings);
  focus.Init();
  photos = std::make_unique<Esp32::PhotoStore>(filesystem);
  companionReady = Esp32::Companion::Init(*photos);
  if (companionReady)
    ble.EnableRadio();
  else
    ble.DisableRadio();
  battery.ReadPowerState();
  srand(esp_random());
  if (settings->GetBrightness() < Controllers::BrightnessController::Levels::Low ||
      settings->GetBrightness() > Controllers::BrightnessController::Levels::High)
    settings->SetBrightness(Controllers::BrightnessController::Levels::Medium);
  wake();
  show();
  Esp32::Companion::Publish(companionState());
  Usb.println("READY");
}

void loop() {
  Hardware::Tick();
  photos->Tick();
  Esp32::Companion::Poll();
  if (Esp32::Companion::Paired())
    ble.Connect();
  else
    ble.Disconnect();
  uint32_t code;
  if (Esp32::Companion::TakePairingCode(code)) {
    pairingPin = code;
    if (page != Page::Pairing)
      beforePairing = page;
    wake();
    request(Page::Pairing);
  }
  if (page == Page::Pairing && pairingPin && Esp32::Companion::Paired()) {
    pairingPin = 0;
    request(beforePairing);
  }
  Esp32::CompanionProtocol::Command remote;
  if (Esp32::Companion::TakeCommand(remote) && Esp32::Companion::Paired()) {
    companionCommand(remote);
    Esp32::Companion::Publish(companionState());
  }
  Esp32::PlaySensors::Tick();
  if (focus.Tick())
    focusNotice = true;
  if (focusNotice && page != Page::Photo) {
    focusNotice = false;
    wake();
    request(Page::Garden);
  }
  if (page == Page::Photo && screen)
    static_cast<Esp32::PhotoBadge*>(screen.get())->Poll();
  if (!sleeping)
    lv_task_handler();
  const auto now = millis();
  static bool bootWasDown = false;
  const bool bootDown = digitalRead(0) == LOW;
  if ((bootDown && !bootWasDown) || testButton == TestButton::Boot) {
    if (sleeping)
      wake();
    else if (!screen || !screen->OnButtonPushed())
      request(page == Page::Launcher ? selectedWatch : Page::Launcher);
    if (testButton == TestButton::Boot) {
      Usb.printf("TEST BUTTON boot sampled=%lu activity=%lu\n", static_cast<unsigned long>(now), static_cast<unsigned long>(lastActivity));
      testButton = TestButton::None;
    }
  }
  bootWasDown = bootDown;
  static uint32_t powerPoll = 0;
  if (now - powerPoll > 100) {
    powerPoll = now;
    if (Hardware::PowerButtonPressed() || testButton == TestButton::Power) {
      if (sleeping)
        wake();
      else
        sleepDisplay();
      if (testButton == TestButton::Power) {
        Usb.printf("TEST BUTTON pwr sampled=%lu activity=%lu\n", static_cast<unsigned long>(now), static_cast<unsigned long>(lastActivity));
        testButton = TestButton::None;
      }
    }
  }
  if (!sleeping && lv_disp_get_inactive_time(nullptr) < 100)
    lastActivity = millis();
  int dx, dy;
  if (!sleeping && Hardware::TakeSwipe(dx, dy)) {
    lastActivity = millis();
    TouchEvents event = abs(dx) > abs(dy) ? (dx > 0 ? TouchEvents::SwipeRight : TouchEvents::SwipeLeft)
                                          : (dy > 0 ? TouchEvents::SwipeDown : TouchEvents::SwipeUp);
    swipe(event);
  }
  if (!sleeping && screen && !screen->IsRunning())
    request(Page::Launcher);
  if (!sleeping && transition)
    show();
  if (now - lastRefresh >= 1000) {
    lastRefresh = now;
    battery.ReadPowerState();
    dateTime->CurrentDateTime();
    if (ambient)
      ambient->Refresh(battery.PercentRemaining(), battery.IsCharging());
    Esp32::Companion::Publish(companionState());
    if (clockLabel) {
      lv_label_set_text(clockLabel, Hardware::ClockValid() ? dateTime->FormattedTime().c_str() : "--:--");
      lv_obj_align(clockLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 158);
      lv_label_set_text_fmt(dateLabel, "%s, %02u %s", dateTime->DayOfWeekShortToString(), dateTime->Day(), dateTime->MonthShortToString());
      lv_obj_align(dateLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 259);
      lv_label_set_text_fmt(powerLabel, "%u%%  %s", battery.PercentRemaining(), battery.IsCharging() ? "CHARGING" : "BATTERY");
      lv_obj_align(powerLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 310);
    }
  }
  // Button and navigation handlers can advance lastActivity beyond the loop's
  // initial timestamp. Sample after those handlers to preserve unsigned elapsed time.
  if (!sleeping && !systemTask.IsSleepDisabled() && page != Page::Pairing && !photos->Busy() &&
      millis() - lastActivity > settings->GetScreenTimeOut())
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
