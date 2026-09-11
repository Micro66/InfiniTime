#pragma once
#include <cstddef>
#include <cstdint>
#include <esp_partition.h>

namespace Pinetime::Drivers {
  // Implements the upstream 4 MiB flash interface inside a dedicated partition.
  // Addresses passed by LittleFS are relative to this partition, never raw flash.
  class SpiNorFlash {
  public:
    SpiNorFlash();
    void Read(size_t address, uint8_t* buffer, size_t size);
    void Write(size_t address, uint8_t* buffer, size_t size);
    void SectorErase(size_t address);

    bool EraseFailed() const {
      return eraseFailed;
    }

    bool ProgramFailed() const {
      return programFailed;
    }

  private:
    const esp_partition_t* partition;
    bool eraseFailed = false;
    bool programFailed = false;
  };
}
