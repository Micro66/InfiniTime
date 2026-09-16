#pragma once
#include "CompanionProtocol.h"
#include "components/fs/FS.h"
#include <lvgl/lvgl.h>
#include <mutex>

namespace Esp32 {
  class PhotoStore {
  public:
    static constexpr size_t FrameBytes = CompanionProtocol::FrameBytes;
    enum class State : uint8_t { Idle, Receiving, Queued, Writing, Done, Failed };
    enum class Error : uint8_t { None, Invalid, Busy, Memory, Order, Checksum, Storage, Cancelled, Timeout };
    enum class Owner { Wifi, Ble };
    explicit PhotoStore(Pinetime::Controllers::FS& fs);
    ~PhotoStore();
    bool Begin(Owner owner, uint32_t id, uint32_t size, uint32_t crc);
    bool Append(Owner owner, uint32_t id, uint32_t offset, const uint8_t* bytes, size_t size);
    bool Commit(Owner owner, uint32_t id);
    void Cancel(Owner owner, uint32_t id = 0);
    void Tick();
    bool Busy() const;
    State GetState() const;
    CompanionProtocol::Packet Status() const;
    const lv_img_dsc_t* Image();

  private:
    void Fail(Error error);
    Pinetime::Controllers::FS& fs;
    mutable std::mutex mutex;
    Owner owner = Owner::Wifi;
    State state = State::Idle;
    Error error = Error::None;
    uint32_t id = 0, received = 0, expectedCrc = 0, lastData = 0;
    uint8_t* incoming = nullptr;
    uint8_t* image = nullptr;
    lv_img_dsc_t descriptor {};
    lfs_file_t file {};
    bool open = false;
    size_t written = 0;
  };
}
