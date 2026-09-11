#include "UsbConsole.h"
#include <driver/usb_serial_jtag.h>
#include <algorithm>

namespace Esp32 {
  UsbConsole Usb;

  void UsbConsole::Begin() {
    usb_serial_jtag_driver_config_t config {.tx_buffer_size = 4096, .rx_buffer_size = 1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
  }

  int UsbConsole::available() {
    if (pending >= 0)
      return 1;
    uint8_t value;
    if (usb_serial_jtag_read_bytes(&value, 1, 0) == 1)
      pending = value;
    return pending >= 0 ? 1 : 0;
  }

  int UsbConsole::read() {
    if (!available())
      return -1;
    const int value = pending;
    pending = -1;
    return value;
  }

  size_t UsbConsole::write(uint8_t value) {
    return write(&value, 1);
  }

  size_t UsbConsole::write(const uint8_t* data, size_t size) {
    if (!usb_serial_jtag_is_connected())
      return 0;
    return std::max(0, usb_serial_jtag_write_bytes(data, size, pdMS_TO_TICKS(1000)));
  }
}
