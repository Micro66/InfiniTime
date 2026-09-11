#pragma once
#include <Print.h>

namespace Esp32 {
  // Use the ESP-IDF USB Serial/JTAG driver for both receive and transmit.
  // One driver owns the peripheral; Arduino HWCDC must not also initialize it.
  class UsbConsole : public Print {
  public:
    void Begin();
    int available();
    int read();
    size_t write(uint8_t value) override;
    size_t write(const uint8_t* data, size_t size) override;
    using Print::write;

  private:
    int pending = -1;
  };

  extern UsbConsole Usb;
}
