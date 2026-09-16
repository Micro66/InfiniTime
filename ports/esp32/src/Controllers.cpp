#include "components/battery/BatteryController.h"
#include "components/brightness/BrightnessController.h"
#include "components/datetime/DateTimeController.h"
#include "drivers/SpiNorFlash.h"
#include "displayapp/screens/Symbols.h"
#include "Hardware.h"
#include <esp_err.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <algorithm>
#include <cstdlib>
#include <Preferences.h>

using namespace Pinetime::Controllers;

void Battery::ReadPowerState() {
  const auto state = Esp32::Hardware::ReadPower();
  percent = std::clamp(state.percent, 0, 100);
  voltage = state.millivolts;
  charging = state.charging;
  externalPower = state.external;
  present = state.present;
}

void BrightnessController::Init() {
  Set(level);
}

void BrightnessController::Set(Levels value) {
  level = value;
  constexpr uint8_t values[] = {0, 8, 35, 100, 210};
  Esp32::Hardware::SetBrightness(values[static_cast<unsigned>(value)]);
}

void BrightnessController::Lower() {
  Set(static_cast<Levels>(std::max(0, static_cast<int>(level) - 1)));
}

void BrightnessController::Higher() {
  Set(static_cast<Levels>(std::min(4, static_cast<int>(level) + 1)));
}

void BrightnessController::Step() {
  Set(level == Levels::High ? Levels::Low : static_cast<Levels>(static_cast<int>(level) + 1));
}

const char* BrightnessController::GetIcon() {
  return Pinetime::Applications::Screens::Symbols::sun;
}

const char* BrightnessController::ToString() {
  constexpr const char* names[] = {"Off", "Always on", "Low", "Medium", "High"};
  return names[static_cast<unsigned>(level)];
}

Pinetime::Drivers::SpiNorFlash::SpiNorFlash()
  : partition(esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "infinitime")) {
  ESP_ERROR_CHECK(partition && partition->size >= 0x400000 ? ESP_OK : ESP_ERR_NOT_FOUND);
}

void Pinetime::Drivers::SpiNorFlash::Read(size_t address, uint8_t* buffer, size_t size) {
  ESP_ERROR_CHECK(esp_partition_read(partition, address, buffer, size));
}

void Pinetime::Drivers::SpiNorFlash::Write(size_t address, uint8_t* buffer, size_t size) {
  programFailed = esp_partition_write(partition, address, buffer, size) != ESP_OK;
}

void Pinetime::Drivers::SpiNorFlash::SectorErase(size_t address) {
  eraseFailed = esp_partition_erase_range(partition, address, 4096) != ESP_OK;
}

// InfiniTime's clock stores local wall time. ESP32 uses UTC internally and the
// standard TZ conversion; a UTC anchor is retained in the internal RTC domain.
DateTime::DateTime(Settings& settings) : localTime {}, settingsController(settings) {
  Preferences clockSettings;
  clockSettings.begin("infini-clock", true);
  const auto savedZone = clockSettings.getChar("zone", 32);
  clockSettings.end();
  tzOffset = std::clamp<int>(savedZone, -48, 56);
  dstOffset = 0;
  const int offset = -tzOffset * 15;
  char zone[24];
  snprintf(zone, sizeof(zone), "UTC%s%d:%02d", offset < 0 ? "-" : "+", abs(offset) / 60, abs(offset) % 60);
  setenv("TZ", zone, 1);
  tzset();
  std::tm rtc {};
  if (Esp32::Hardware::ReadClock(rtc)) {
    timeval tv {.tv_sec = mktime(&rtc), .tv_usec = 0};
    settimeofday(&tv, nullptr);
  }
  CurrentDateTime();
}

std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> DateTime::CurrentDateTime() {
  const time_t now = time(nullptr);
  localtime_r(&now, &localTime);
  uptime = std::chrono::seconds(esp_timer_get_time() / 1000000);
  currentDateTime = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>(
    std::chrono::seconds(now + (tzOffset + dstOffset) * 15 * 60));
  return currentDateTime;
}

void DateTime::SetTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  std::tm value {};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  value.tm_hour = hour;
  value.tm_min = minute;
  value.tm_sec = second;
  value.tm_isdst = -1;
  const auto epoch = mktime(&value);
  timeval tv {.tv_sec = epoch, .tv_usec = 0};
  ESP_ERROR_CHECK(settimeofday(&tv, nullptr) == 0 ? ESP_OK : ESP_FAIL);
  Esp32::Hardware::WriteClock(value);
  CurrentDateTime();
}

void DateTime::SetCurrentTime(std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> value) {
  auto epoch = std::chrono::system_clock::to_time_t(value) - (tzOffset + dstOffset) * 900;
  std::tm tm {};
  localtime_r(&epoch, &tm);
  SetTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void DateTime::SetTimeZone(int8_t timezone, int8_t dst) {
  if (timezone < -48 || timezone > 56 || (dst != 0 && dst != 2 && dst != 4 && dst != 8))
    return;
  const bool changed = tzOffset != timezone || dstOffset != dst;
  tzOffset = timezone;
  dstOffset = dst;
  const int offset = -(timezone + dst) * 15;
  char zone[24];
  snprintf(zone, sizeof(zone), "UTC%s%d:%02d", offset < 0 ? "-" : "+", abs(offset) / 60, abs(offset) % 60);
  setenv("TZ", zone, 1);
  tzset();
  CurrentDateTime();
  if (Esp32::Hardware::ClockValid())
    Esp32::Hardware::WriteClock(localTime);
  if (changed) {
    Preferences clockSettings;
    clockSettings.begin("infini-clock", false);
    clockSettings.putChar("zone", timezone + dst);
  }
}

void DateTime::Register(System::SystemTask* task) {
  systemTask = task;
}

std::string DateTime::FormattedTime() {
  char text[24];
  strftime(text, sizeof(text), "%H:%M", &localTime);
  return text;
}

const char* DateTime::MonthShortToStringLow(Months month) {
  static const char* values[] = {"???", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return values[std::min(12u, static_cast<unsigned>(month))];
}

const char* DateTime::DayOfWeekShortToStringLow(Days day) {
  static const char* values[] = {"???", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  return values[std::min(7u, static_cast<unsigned>(day))];
}

const char* DateTime::DayOfWeekToStringLow(Days day) {
  static const char* values[] = {"Unknown", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
  return values[std::min(7u, static_cast<unsigned>(day))];
}

const char* DateTime::MonthShortToString() const {
  return MonthShortToStringLow(Month());
}

const char* DateTime::DayOfWeekShortToString() const {
  return DayOfWeekShortToStringLow(DayOfWeek());
}

const char* DateTime::DayOfWeekToString() const {
  return DayOfWeekToStringLow(DayOfWeek());
}
