// Compile with photo_stubs first; exercise the actual firmware PhotoStore.cpp.
#include "PhotoStore.h"
#include <esp_rom_crc.h>
#include <cassert>
#include <cstdio>
#include <vector>
uint32_t fakeMillis = 0;
int main() {
  using namespace Esp32;
  using Owner = PhotoStore::Owner;
  using State = PhotoStore::State;
  Pinetime::Controllers::FS fs;
  const std::vector<uint8_t> old(PhotoStore::FrameBytes, 0x33);
  const std::vector<uint8_t> next(PhotoStore::FrameBytes, 0xa5);
  fs.files["/photo.rgb"] = old;
  PhotoStore store(fs);
  const auto crc = esp_rom_crc32_le(0, next.data(), next.size());
  auto begin = [&](uint32_t id, uint32_t checksum) { return store.Begin(Owner::Ble, id, next.size(), checksum); };
  auto append = [&](uint32_t id) { return store.Append(Owner::Ble, id, 0, next.data(), next.size()); };
  auto unchanged = [&] { assert(fs.files.at("/photo.rgb") == old); assert(store.Image()->data[0] == 0x33); };
  assert(!store.Begin(Owner::Ble, 1, 10, crc));
  assert(begin(1, crc));
  assert(!store.Begin(Owner::Wifi, 2, next.size(), crc));
  assert(!store.Append(Owner::Wifi, 1, 0, next.data(), 5));
  assert(!store.Append(Owner::Ble, 2, 0, next.data(), 5));
  assert(!store.Append(Owner::Ble, 1, 1, next.data(), 5));
  store.Tick(); unchanged();
  assert(begin(3, crc ^ 1)); assert(append(3)); assert(!store.Commit(Owner::Ble, 3));
  store.Tick(); unchanged();
  assert(begin(4, crc)); store.Cancel(Owner::Ble); store.Tick(); unchanged();
  assert(begin(5, crc)); fakeMillis += 20001; store.Tick();
  assert(store.GetState() == State::Failed); unchanged();
  for (int failure = 0; failure < 3; ++failure) {
    assert(begin(6 + failure, crc)); assert(append(6 + failure)); assert(store.Commit(Owner::Ble, 6 + failure));
    fs.failWrite = failure == 0; fs.failClose = failure == 1; fs.failRename = failure == 2;
    while (store.Busy()) store.Tick();
    assert(store.GetState() == State::Failed); unchanged();
    fs.failWrite = fs.failClose = fs.failRename = false;
  }
  assert(begin(9, crc));
  for (size_t offset = 0; offset < next.size();) {
    const auto count = std::min<size_t>(236, next.size() - offset);
    assert(store.Append(Owner::Ble, 9, offset, next.data() + offset, count)); offset += count;
  }
  assert(store.Commit(Owner::Ble, 9));
  store.Cancel(Owner::Ble); // An accepted commit must finish even after disconnection.
  while (store.Busy()) store.Tick();
  assert(store.GetState() == State::Done); assert(fs.files["/photo.rgb"] == next);
  assert(!fs.files.count("/photo-upload.tmp"));
  assert(store.Image()->data[0] == 0xa5);
  auto status = store.Status(); assert(CompanionProtocol::U32(status.data() + 8) == next.size());
  using namespace CompanionProtocol;
  assert(Valid({1, 1, 1800000000, uint32_t(-12600)}));
  assert(!Valid({1, 1, 0, 28800})); assert(!Valid({1, 1, 1800000000, 1}));
  assert(!Valid({3, 1, 0})); assert(!Valid({5, 1, 5})); assert(!Valid({10, 1, 16}));
  assert(Valid({11, 1, 0}) && Valid({11, 1, 1}) && !Valid({11, 1, 2}));
  Command command; Packet packet {1, 3, 0x34, 0x12}; Put32(packet.data() + 4, 4);
  assert(Decode(packet.data(), packet.size(), command) && command.sequence == 0x1234 && command.a == 4);
  assert(!Decode(packet.data(), 19, command));
  puts("PASS: protocol validation; shared receiver ownership, ordering, CRC, timeout, cancellation, storage failures, atomic commit");
}
