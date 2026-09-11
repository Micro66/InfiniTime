#include "Hardware.h"
#include "UsbConsole.h"
using Esp32::Usb;
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrvCSTXXX.hpp>
#include <soc/esp32s3/rtc.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_rom_crc.h>
#include <XPowersLib.h>
#include <esp_heap_caps.h>
#include "displayapp/InfiniTimeTheme.h"
#include <cstring>
#include <algorithm>

namespace {
  constexpr int width = 466;
  constexpr int height = 466;
  constexpr int sda = 15, scl = 14;
  Arduino_ESP32QSPI bus(12, 38, 4, 5, 6, 7);
  Arduino_CO5300 panel(&bus, 1, 0, width, height, 6, 0, 0, 0);
  TouchDrvCST92xx touch;

  // 1.75C has no PCF85063. Retain a calibrated wall-time anchor in the
  // ESP32 RTC domain across software resets; total power loss requires USB sync.
  struct ClockAnchor {
    uint64_t unixUs;
    uint64_t rtcUs;
    uint64_t check;
  };

  RTC_NOINIT_ATTR ClockAnchor clockAnchor;
  constexpr uint64_t clockMarker = 0x494e46494e49544dULL;
  XPowersPMU power;
  lv_disp_buf_t drawBuffer;
  lv_color_t* framebuffer = nullptr;
  bool clockValid = false;
  unsigned flushes = 0;
  bool pressed = false;
  int16_t startX = 0, startY = 0, lastX = 0, lastY = 0;
  int swipeX = 0, swipeY = 0;
  bool swipePending = false;
  portMUX_TYPE touchMux = portMUX_INITIALIZER_UNLOCKED;
  volatile bool touchPending = false;
  unsigned touchEvents = 0;
  uint32_t lastTouchReport = 0;
  int testX = 0, testY = 0, testPhase = 0;

  void IRAM_ATTR touchInterrupt() {
    portENTER_CRITICAL_ISR(&touchMux);
    touchPending = true;
    portEXIT_CRITICAL_ISR(&touchMux);
  }

  void roundArea(lv_disp_drv_t*, lv_area_t* area) {
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 = std::min<int>(width - 1, area->x2 | 1);
    area->y2 = std::min<int>(height - 1, area->y2 | 1);
  }

  void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    const int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    for (int row = 0; row < h; ++row) {
      memcpy(framebuffer + (area->y1 + row) * width + area->x1, pixels + row * w, w * sizeof(lv_color_t));
    }
    panel.draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(pixels), w, h);
    ++flushes;
    lv_disp_flush_ready(driver);
  }

  bool readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
    int16_t x[2] {}, y[2] {};
    portENTER_CRITICAL(&touchMux);
    const bool report = touchPending;
    touchPending = false;
    portEXIT_CRITICAL(&touchMux);
    bool down = pressed;
    x[0] = lastX;
    y[0] = lastY;
    if (testPhase > 0) {
      down = testPhase > 1;
      x[0] = testX;
      y[0] = testY;
      --testPhase;
    } else if (report) {
      const auto count = touch.getPoint(x, y, 2);
      down = count > 0 && x[0] >= 0 && y[0] >= 0 && x[0] < width && y[0] < height;
      ++touchEvents;
      lastTouchReport = millis();
    } else if (millis() - lastTouchReport > 1500) {
      down = false; // Recover if a release interrupt was lost.
    }
    if (down) {
      lastX = x[0];
      lastY = y[0];
      if (!pressed) {
        startX = lastX;
        startY = lastY;
      }
    } else if (pressed) {
      swipeX = lastX - startX;
      swipeY = lastY - startY;
      swipePending = std::max(std::abs(swipeX), std::abs(swipeY)) > 55;
    }
    pressed = down;
    data->point = {lastX, lastY};
    data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    return false;
  }
}

namespace Esp32 {
  void Hardware::Init() {
    Wire.begin(sda, scl, 400000);
    Wire.setTimeOut(30);
    ESP_ERROR_CHECK(psramFound() ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(power.begin(Wire, AXP2101_SLAVE_ADDRESS, sda, scl) ? ESP_OK : ESP_ERR_NOT_FOUND);
    // Preserve the manufacturer's charging limits and regulator voltages.
    power.enableBattDetection();
    power.enableBattVoltageMeasure();
    power.enableVbusVoltageMeasure();
    power.enableSystemVoltageMeasure();
    power.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    power.clearIrqStatus();
    Usb.println("HW AXP2101 OK");
    Usb.print("HW I2C addresses:");
    for (uint8_t address = 8; address < 120; ++address) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0)
        Usb.printf(" %02x", address);
    }
    Usb.println();

    const auto rtcNow = esp_rtc_get_time_us();
    clockValid = esp_reset_reason() != ESP_RST_POWERON && esp_reset_reason() != ESP_RST_BROWNOUT &&
                 clockAnchor.check == (clockAnchor.unixUs ^ clockAnchor.rtcUs ^ clockMarker) && clockAnchor.unixUs >= 1577836800000000ULL &&
                 clockAnchor.unixUs < 4102444800000000ULL && rtcNow >= clockAnchor.rtcUs;
    Usb.printf("HW internal RTC valid=%d\n", clockValid);

    touch.setPins(2, 11);
    ESP_ERROR_CHECK(touch.begin(Wire, 0x5A, -1, -1) ? ESP_OK : ESP_ERR_NOT_FOUND);
    touch.wakeup();
    pinMode(11, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(11), touchInterrupt, FALLING);
    Usb.printf("HW TOUCH %s OK\n", touch.getModelName());
    ESP_ERROR_CHECK(panel.begin(40000000) ? ESP_OK : ESP_FAIL);
    panel.fillScreen(0);
    panel.setBrightness(100);
    Usb.printf("HW CO5300 466x466 OK psram=%u\n", ESP.getPsramSize());
    pinMode(0, INPUT_PULLUP);
  }

  void Hardware::InitGui() {
    lv_init();
    lv_theme_set_act(lv_pinetime_theme_init());
    framebuffer = static_cast<lv_color_t*>(heap_caps_calloc(width * height, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* pixels = static_cast<lv_color_t*>(heap_caps_malloc(width * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ESP_ERROR_CHECK(framebuffer && pixels ? ESP_OK : ESP_ERR_NO_MEM);
    lv_disp_buf_init(&drawBuffer, pixels, nullptr, width * 40);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = width;
    driver.ver_res = height;
    driver.flush_cb = flush;
    driver.rounder_cb = roundArea;
    driver.buffer = &drawBuffer;
    lv_disp_drv_register(&driver);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = readTouch;
    lv_indev_drv_register(&input);
  }

  PowerState Hardware::ReadPower() {
    return {power.getBatteryPercent(), power.getBattVoltage(), power.isBatteryConnect(), power.isCharging(), power.isVbusIn()};
  }

  bool Hardware::PowerButtonPressed() {
    power.getIrqStatus();
    const bool hit = power.isPekeyShortPressIrq();
    if (hit)
      power.clearIrqStatus();
    return hit;
  }

  void Hardware::SetBrightness(uint8_t value) {
    panel.setBrightness(value);
  }

  bool Hardware::ReadClock(std::tm& value) {
    if (!clockValid)
      return false;
    const time_t epoch = (clockAnchor.unixUs + esp_rtc_get_time_us() - clockAnchor.rtcUs) / 1000000;
    localtime_r(&epoch, &value);
    return true;
  }

  void Hardware::WriteClock(const std::tm& value) {
    auto local = value;
    clockAnchor.unixUs = static_cast<uint64_t>(mktime(&local)) * 1000000;
    clockAnchor.rtcUs = esp_rtc_get_time_us();
    clockAnchor.check = clockAnchor.unixUs ^ clockAnchor.rtcUs ^ clockMarker;
    clockValid = true;
  }

  void Hardware::Tick() {
    static uint32_t previous = millis();
    const uint32_t now = millis();
    lv_tick_inc(now - previous);
    previous = now;
  }

  bool Hardware::TakeSwipe(int& dx, int& dy) {
    if (!swipePending)
      return false;
    swipePending = false;
    dx = swipeX;
    dy = swipeY;
    return true;
  }

  unsigned Hardware::FlushCount() {
    return flushes;
  }

  bool Hardware::ClockValid() {
    return clockValid;
  }

  unsigned Hardware::TouchCount() {
    return touchEvents;
  }

  bool Hardware::TestTap(int x, int y) {
    if (testPhase || x < 0 || y < 0 || x >= width || y >= height)
      return false;
    testX = x;
    testY = y;
    testPhase = 4;
    return true;
  }

  void Hardware::Capture() {
    Usb.printf("FRAME %d %d %u\n", width, height, width * height * sizeof(lv_color_t));
    const auto* data = reinterpret_cast<const uint8_t*>(framebuffer);
    const size_t length = width * height * sizeof(lv_color_t);
    for (size_t offset = 0; offset < length;) {
      const auto count = std::min<size_t>(1024, length - offset);
      const auto crc = esp_rom_crc32_le(0, data + offset, count);
      bool accepted = false;
      for (int retry = 0; retry < 3 && !accepted; ++retry) {
        Usb.printf("CHUNK %u %u %08lx\n", static_cast<unsigned>(offset), static_cast<unsigned>(count), static_cast<unsigned long>(crc));
        if (Usb.write(data + offset, count) != count)
          return;
        const auto deadline = millis() + 3000;
        String reply;
        while (static_cast<int32_t>(deadline - millis()) > 0) {
          while (Usb.available()) {
            char c = Usb.read();
            if (c == '\n') {
              unsigned acknowledged;
              if (sscanf(reply.c_str(), "ACK %u", &acknowledged) == 1 && acknowledged == offset)
                accepted = true;
              reply = "";
            } else if (c != '\r' && reply.length() < 40)
              reply += c;
          }
          if (accepted)
            break;
          delay(1);
        }
      }
      if (!accepted)
        return;
      offset += count;
    }
    Usb.println("\nFRAME_END");
  }
}
