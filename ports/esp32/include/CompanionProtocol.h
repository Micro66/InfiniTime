#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace Esp32::CompanionProtocol {
  constexpr char Service[] = "7d2ea28a-f7bd-485a-bd9d-92ad6ecfe93e";
  constexpr char CommandId[] = "7d2ea28b-f7bd-485a-bd9d-92ad6ecfe93e";
  constexpr char StateId[] = "7d2ea28c-f7bd-485a-bd9d-92ad6ecfe93e";
  constexpr char DataId[] = "7d2ea28d-f7bd-485a-bd9d-92ad6ecfe93e";
  constexpr char PhotoId[] = "7d2ea28e-f7bd-485a-bd9d-92ad6ecfe93e";
  constexpr char VersionId[] = "7d2ea28f-f7bd-485a-bd9d-92ad6ecfe93e";
  constexpr uint32_t FrameBytes = 466 * 466 * 2;
  using Packet = std::array<uint8_t, 20>;

  inline uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
  }

  inline void Put32(uint8_t* p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
      p[i] = v >> (8 * i);
  }

  struct Command {
    uint8_t op = 0;
    uint16_t sequence = 0;
    uint32_t a = 0, b = 0, c = 0, d = 0;
  };

  inline bool Decode(const uint8_t* bytes, size_t length, Command& result) {
    if (length != 20 || bytes[0] != 1)
      return false;
    result = {bytes[1], uint16_t(bytes[2] | bytes[3] << 8), U32(bytes + 4), U32(bytes + 8), U32(bytes + 12), U32(bytes + 16)};
    return true;
  }

  inline bool Valid(const Command& c) {
    switch (c.op) {
      case 1:
        return c.a >= 1577836800 && c.a <= 4102444799U && int32_t(c.b) >= -43200 && int32_t(c.b) <= 50400 && int32_t(c.b) % 900 == 0;
      case 2:
        return c.a <= 1;
      case 3:
        return c.a >= 2 && c.a <= 4;
      case 4:
        return c.a == 15 || c.a == 30 || c.a == 60 || c.a == 120 || c.a == 300;
      case 5:
        return c.a < 5;
      case 6:
        return c.a < 3;
      case 7:
        return c.a == FrameBytes && c.c != 0;
      case 8:
      case 9:
        return c.a != 0;
      case 10:
        return c.a < 16;
      default:
        return false;
    }
  }
}
