#include "FunApps.h"
#include "RoundPaint.h"
#include "PlaySensors.h"
#include "Hardware.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <esp_random.h>

namespace {
  uint64_t secondsNow() {
    return esp_timer_get_time() / 1000000;
  }

  uint64_t unixNow() {
    std::tm t {};
    return Esp32::Hardware::ReadClock(t) ? mktime(&t) : 0;
  }

  struct FocusSave {
    uint32_t magic = 0x47415231, duration = 1500, remaining = 1500, flowers = 0;
    uint64_t endUnix = 0;
    uint8_t phase = 0, running = 0;
  };

  unsigned randomBelow(unsigned count) {
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % count);
    uint32_t value;
    do {
      value = esp_random();
    } while (value >= limit);
    return value % count;
  }

  void flower(const Esp32::Paint& p, int x, int y, uint32_t color, int size) {
    p.line(x, y, x, y + size * 3, 2, 0x75ac80);
    p.line(x, y + size * 2, x - size, y + size, 3, 0x75ac80);
    for (int i = 0; i < 6; ++i) {
      const auto point = Esp32::polar(i * 60, size, x, y);
      p.dot(point.x, point.y, size / 2 + 1, color);
    }
    p.dot(x, y, size / 2, 0xffe2a2);
  }
}

namespace Esp32 {
  void FocusService::Init() {
    preferences.begin("infini-garden", false);
    FocusSave saved;
    if (preferences.getBytesLength("state") != sizeof(saved))
      return;
    preferences.getBytes("state", &saved, sizeof(saved));
    if (saved.magic != 0x47415231 || saved.phase > 3 || saved.duration < 60 || saved.duration > 2700 || saved.remaining > 2700)
      return;
    state.duration = saved.duration;
    state.remaining = saved.remaining;
    state.flowers = saved.flowers;
    state.phase = static_cast<FocusClock::Phase>(saved.phase);
    const auto clock = unixNow();
    if (saved.running && saved.endUnix && clock) {
      state.running = true;
      state.deadline = secondsNow() + (saved.endUnix > clock ? saved.endUnix - clock : 0);
      Tick();
    }
  }

  void FocusService::Save() {
    FocusSave saved;
    saved.duration = state.duration;
    saved.remaining = state.remaining;
    saved.flowers = state.flowers;
    saved.phase = static_cast<uint8_t>(state.phase);
    saved.running = state.running;
    const auto clock = unixNow();
    saved.endUnix = state.running && clock ? clock + state.remaining : 0;
    preferences.putBytes("state", &saved, sizeof(saved));
  }

  bool FocusService::Tick() {
    if (!state.Tick(secondsNow()))
      return false;
    Save();
    return true;
  }

  void FocusService::Toggle() {
    state.Toggle(secondsNow());
    Save();
  }

  void FocusService::ResetOrDuration() {
    if (state.phase == FocusClock::Phase::Ready)
      state.duration = state.duration == 900 ? 1500 : state.duration == 1500 ? 2700 : 900;
    state.Reset();
    Save();
  }

  FunApp::FunApp(Kind kind, FocusService& focus, Pinetime::System::SystemTask& system)
    : RoundArtwork(kind == Kind::Garden ? 500 : 50), kind(kind), focus(focus), wakeLock(system) {
    if (kind == Kind::Marble || kind == Kind::Music)
      wakeLock.Lock();
    if (kind == Kind::Dice || kind == Kind::Marble)
      sensorReady = PlaySensors::MotionStart();
    if (kind == Kind::Music)
      sensorReady = PlaySensors::AudioStart();
    lastStep = millis();
  }

  FunApp::~FunApp() {
    if (kind == Kind::Dice || kind == Kind::Marble)
      PlaySensors::MotionStop();
    if (kind == Kind::Music)
      PlaySensors::AudioStop();
  }

  void FunApp::Roll() {
    lv_disp_trig_activity(nullptr);
    value = randomBelow(mode == 2 ? 2 : 6) + 1;
    rollStart = millis();
    ++rolls;
  }

  void FunApp::Tap(int x, int y) {
    if (kind == Kind::Dice) {
      if (y > 363) {
        mode = (mode + 1) % 3;
        rollStart = 0;
        value = 1;
      } else
        Roll();
    } else if (kind == Kind::Marble) {
      auto sample = PlaySensors::Motion();
      if (sample.valid) {
        zeroX = sample.x;
        zeroY = sample.y;
      }
      marble = {};
    } else if (kind == Kind::Music)
      mode = (mode + 1) % 3;
    else if (y > 352) {
      if (x < 233)
        focus.ResetOrDuration();
      else
        focus.Toggle();
    }
    Refresh();
  }

  void FunApp::Refresh() {
    const auto now = millis();
    if (kind == Kind::Dice) {
      auto sample = PlaySensors::Motion();
      if (sample.valid && sample.serial != motionSerial) {
        motionSerial = sample.serial;
        if (std::abs(std::sqrt(sample.x * sample.x + sample.y * sample.y + sample.z * sample.z) - 1) > 0.7f && now - rollStart > 1200)
          Roll();
      }
    } else if (kind == Kind::Marble) {
      const auto sample = PlaySensors::Motion();
      // A suspended GUI must not simulate the entire sleeping interval on wake.
      accumulator += std::min(0.1f, (now - lastStep) / 1000.0f);
      const auto tilt = ScreenTilt(sample.x, sample.y, zeroX, zeroY);
      while (accumulator >= 1.0f / 120) {
        if (sample.valid)
          marble.Step(tilt[0], tilt[1], 1.0f / 120);
        accumulator -= 1.0f / 120;
      }
    } else if (kind == Kind::Music) {
      auto sample = PlaySensors::Audio();
      loudness = sample.valid ? std::clamp((20 * std::log10(std::max(sample.rms, 0.00001f)) + 55) / 45, 0.0f, 1.0f) : 0;
      for (unsigned i = 0; i < 8; ++i) {
        const float target = sample.valid ? std::clamp((20 * std::log10(std::max(sample.bands[i], 0.00001f)) + 60) / 50, 0.0f, 1.0f) : 0;
        bands[i] = std::max(target, bands[i] * 0.78f);
      }
    }
    lastStep = now;
    RoundArtwork::Refresh();
  }

  void FunApp::Draw(const lv_area_t* clip) {
    Paint p {clip};
    char text[48];
    if (kind == Kind::Dice) {
      p.box(0, 0, 466, 466, 0x201b35);
      p.arc(233, 233, 219, 0, 360, 2, 0x55446e);
      const char* modes[] {"LUCKY DICE", "FORTUNE COOKIE", "YES / NO"};
      p.text(modes[mode], 61, 0xebc68a);
      const bool rolling = rollStart && millis() - rollStart < 650;
      const unsigned shown = rolling ? (millis() / 80) % 6 + 1 : value;
      if (mode == 0) {
        const int bob = rolling ? int(std::sin(frameTime * 0.025) * 7) : 0;
        p.box(144, 143 + bob, 178, 178, 0xffead5, 30);
        auto pip = [&](int x, int y) {
          p.dot(x, y + bob, 12, 0x433051);
        };
        if (shown % 2)
          pip(233, 232);
        if (shown >= 2) {
          pip(183, 182);
          pip(283, 282);
        }
        if (shown >= 4) {
          pip(283, 182);
          pip(183, 282);
        }
        if (shown == 6) {
          pip(183, 232);
          pip(283, 232);
        }
      } else {
        p.dot(233, 230, 94, 0x40314f);
        const char* fortunes[] {"GO FOR IT", "TAKE A BREAK", "GOOD THINGS", "TRY AGAIN", "TRUST YOURSELF", "A LUCKY DAY"};
        p.text(rolling ? "..." : mode == 2 ? (value == 1 ? "YES!" : "NOPE") : fortunes[value - 1], 216, 0xffe1aa);
        p.star(233, 171, 12, 0xebc68a);
      }
      p.text(rolling ? "ROLLING..." : sensorReady ? "SHAKE OR TAP" : "TAP TO ROLL", 337, 0xbeabc9);
      p.box(143, 376, 180, 39, 0x544064, 20);
      p.text("CHANGE MODE", 383, 0xffead5);
    } else if (kind == Kind::Marble) {
      p.box(0, 0, 466, 466, 0x0a1c25);
      p.arc(233, 233, 200, 0, 360, 7, 0x5dcebd);
      p.arc(233, 233, 207, 0, 360, 2, 0x255e60);
      p.text("GRAVITY CLUB", 71, 0x8bebd6);
      snprintf(text, sizeof(text), "%04u", marble.score);
      p.text(text, 107, 0xe0f1e8, &jetbrains_mono_42);
      for (unsigned i = 0; i < 3; ++i) {
        int x = MarblePhysics::bumpers[i][0], y = MarblePhysics::bumpers[i][1];
        p.dot(x, y, 24, marble.hitCooldown[i] > 0 ? 0xffed9f : 0xdba37a);
        p.dot(x, y, 15, 0x503b47);
        p.star(x, y, 6, 0xffdab0);
      }
      p.dot(marble.x + 3, marble.y + 5, 11, 0x163947);
      p.dot(marble.x, marble.y, 11, 0xe6f6ec);
      p.dot(marble.x - 3, marble.y - 3, 3, 0xffffff);
      p.text(sensorReady ? "TILT TO BOUNCE" : "IMU UNAVAILABLE", 353, 0x87b9b6);
      p.text("TAP TO ZERO", 380, 0x87b9b6);
    } else if (kind == Kind::Music) {
      const uint32_t colors[] {0xf79dbd, 0x83ead2, 0xc2adff};
      const uint32_t color = colors[mode];
      p.box(0, 0, 466, 466, 0x141628);
      p.text("S O U N D  B U D D Y", 87, color);
      for (unsigned i = 0; i < 40; ++i) {
        const auto a = polar(i * 9, 194), b = polar(i * 9, 198 + int(bands[i % 8] * 25));
        p.line(a, b, 5, color);
      }
      int bounce = loudness * 24;
      p.box(151, 159 - bounce, 164, 128, color, 35);
      p.box(168, 177 - bounce, 130, 75, 0x20283a, 22);
      p.box(191, 195 - bounce, 13, 21 + int(loudness * 13), color, 6);
      p.box(262, 195 - bounce, 13, 21 + int(loudness * 13), color, 6);
      p.box(216, 235 - bounce, 34, 3 + int(loudness * 9), color, 4);
      for (unsigned i = 0; i < 8; ++i) {
        int height = 3 + bands[i] * 48;
        p.box(137 + i * 25, 335 - height, 17, height, color, 6);
      }
      auto sample = PlaySensors::Audio();
      snprintf(text, sizeof(text), "MIC %5.1f dBFS", 20 * std::log10(std::max(sample.rms, 0.00001f)));
      p.text(!sensorReady ? "MIC UNAVAILABLE" : !sample.valid ? "WAITING FOR AUDIO" : text, 352, 0xb6b9cf);
      p.text("TAP FOR COLOR", 377, color);
    } else {
      const auto& s = focus.State();
      p.box(0, 0, 466, 466, 0x14251f);
      p.arc(233, 233, 219, 0, 360, 3, 0x365341);
      const unsigned total = s.phase == FocusClock::Phase::Rest ? 300 : s.duration;
      if (s.phase != FocusClock::Phase::Ready)
        p.arc(233, 233, 219, 270, 270 + std::max(1u, 360u * (total - std::min<unsigned>(total, s.remaining)) / total), 5, 0xb7dda1);
      p.text("L I T T L E  G A R D E N", 78, 0xb7dda1);
      snprintf(text, sizeof(text), "%02u:%02u", s.remaining / 60, s.remaining % 60);
      p.text(text, 115, 0xf0edcf, &jetbrains_mono_76);
      const char* title = s.phase == FocusClock::Phase::Complete ? "A NEW FLOWER!"
                          : s.phase == FocusClock::Phase::Rest   ? "TIME TO BREATHE"
                          : s.running                            ? "ONE THING AT A TIME"
                                                                 : "GROW WITH YOUR FOCUS";
      p.text(title, 210, 0xb7dda1);
      for (unsigned i = 0; i < std::min<uint32_t>(s.flowers, 12); ++i)
        flower(p, 143 + (i % 6) * 36, 258 + (i / 6) * 34, i % 2 ? 0xf5abb6 : 0xc5d98b, 7);
      if (!s.flowers) {
        flower(p, 233, 276, 0xa7c4a3, 11);
        p.text("YOUR FIRST SEED", 320, 0x84a78a);
      } else {
        snprintf(text, sizeof(text), "%u FLOWERS GROWN", s.flowers);
        p.text(text, 326, 0x84a78a);
      }
      p.box(94, 370, 134, 42, 0x354e3c, 20);
      p.box(238, 370, 134, 42, 0xb7dda1, 20);
      snprintf(text, sizeof(text), "%u MIN", s.duration / 60);
      p.text(s.phase == FocusClock::Phase::Ready ? text : "RESET", 381, 0xd5e4c4, &jetbrains_mono_bold_20, 94, 134);
      p.text(s.running                                ? "PAUSE"
             : s.phase == FocusClock::Phase::Complete ? "REST"
                                                      : "START",
             381,
             0x213728,
             &jetbrains_mono_bold_20,
             238,
             134);
    }
  }
}
