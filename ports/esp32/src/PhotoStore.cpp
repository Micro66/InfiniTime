#include "PhotoStore.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>
#include <algorithm>
#include <cstring>

namespace Esp32 {
  PhotoStore::PhotoStore(Pinetime::Controllers::FS& fs) : fs(fs) {
    fs.FileDelete("/photo-upload.tmp");
    lfs_info info {};
    if (fs.Stat("/photo.rgb", &info) == 0 && info.size == FrameBytes) {
      auto* buffer = static_cast<uint8_t*>(heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM));
      lfs_file_t input {};
      if (buffer && fs.FileOpen(&input, "/photo.rgb", LFS_O_RDONLY) == 0) {
        const int count = fs.FileRead(&input, buffer, FrameBytes);
        fs.FileClose(&input);
        if (count == FrameBytes)
          image = buffer;
        else
          free(buffer);
      } else
        free(buffer);
    }
    descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
    descriptor.header.w = descriptor.header.h = 466;
    descriptor.data_size = FrameBytes;
  }

  PhotoStore::~PhotoStore() {
    if (open)
      fs.FileClose(&file);
    lv_img_cache_invalidate_src(&descriptor);
    free(image);
    free(incoming);
  }

  void PhotoStore::Fail(Error reason) {
    error = reason;
    state = State::Failed;
  }

  bool PhotoStore::Begin(Owner source, uint32_t token, uint32_t size, uint32_t crc) {
    std::lock_guard lock(mutex);
    if (state == State::Receiving || state == State::Queued || state == State::Writing || open)
      return false;
    if (size != FrameBytes || !token)
      return false;
    free(incoming);
    incoming = static_cast<uint8_t*>(heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM));
    owner = source;
    id = token;
    received = written = 0;
    expectedCrc = crc;
    error = Error::None;
    if (!incoming) {
      Fail(Error::Memory);
      return false;
    }
    state = State::Receiving;
    lastData = millis();
    return true;
  }

  bool PhotoStore::Append(Owner source, uint32_t token, uint32_t offset, const uint8_t* bytes, size_t size) {
    std::lock_guard lock(mutex);
    if (owner != source || id != token || state != State::Receiving)
      return false;
    if (!size || offset != received || size > FrameBytes - received) {
      Fail(Error::Order);
      return false;
    }
    memcpy(incoming + received, bytes, size);
    received += size;
    lastData = millis();
    return true;
  }

  bool PhotoStore::Commit(Owner source, uint32_t token) {
    std::lock_guard lock(mutex);
    if (owner != source || id != token || state != State::Receiving)
      return false;
    if (received != FrameBytes) {
      Fail(Error::Order);
      return false;
    }
    if (esp_rom_crc32_le(0, incoming, FrameBytes) != expectedCrc) {
      Fail(Error::Checksum);
      return false;
    }
    state = State::Queued;
    return true;
  }

  void PhotoStore::Cancel(Owner source, uint32_t token) {
    std::lock_guard lock(mutex);
    if (source == owner && (!token || token == id) && state == State::Receiving)
      Fail(Error::Cancelled);
  }

  void PhotoStore::Tick() {
    std::lock_guard lock(mutex);
    if (state == State::Receiving && millis() - lastData > 20000)
      Fail(Error::Timeout);
    if (state == State::Queued) {
      written = 0;
      open = fs.FileOpen(&file, "/photo-upload.tmp", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) == 0;
      if (open)
        state = State::Writing;
      else
        Fail(Error::Storage);
    }
    if (state == State::Writing) {
      const size_t count = std::min<size_t>(4096, FrameBytes - written);
      if (fs.FileWrite(&file, incoming + written, count) != count)
        Fail(Error::Storage);
      else {
        written += count;
        if (written == FrameBytes) {
          const bool closed = fs.FileClose(&file) == 0;
          open = false;
          if (!closed || fs.Rename("/photo-upload.tmp", "/photo.rgb") != 0)
            Fail(Error::Storage);
          else {
            lv_img_cache_invalidate_src(&descriptor);
            free(image);
            image = incoming;
            incoming = nullptr;
            state = State::Done;
          }
        }
      }
    }
    if (state == State::Failed) {
      if (open) {
        fs.FileClose(&file);
        open = false;
      }
      if (incoming) {
        free(incoming);
        incoming = nullptr;
        fs.FileDelete("/photo-upload.tmp");
      }
    }
  }

  bool PhotoStore::Busy() const {
    const auto current = GetState();
    return current == State::Receiving || current == State::Queued || current == State::Writing;
  }

  PhotoStore::State PhotoStore::GetState() const {
    std::lock_guard lock(mutex);
    return state;
  }

  CompanionProtocol::Packet PhotoStore::Status() const {
    std::lock_guard lock(mutex);
    CompanionProtocol::Packet packet {1, uint8_t(state), uint8_t(error), 0};
    CompanionProtocol::Put32(packet.data() + 4, id);
    CompanionProtocol::Put32(packet.data() + 8, received);
    CompanionProtocol::Put32(packet.data() + 12, FrameBytes);
    CompanionProtocol::Put32(packet.data() + 16, 8192);
    return packet;
  }

  const lv_img_dsc_t* PhotoStore::Image() {
    descriptor.data = image;
    return image ? &descriptor : nullptr;
  }
}
