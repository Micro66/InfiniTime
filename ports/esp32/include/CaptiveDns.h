#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Esp32::PhotoNetwork {
  inline constexpr char Ssid[] = "InfiniTime-Badge";
  inline constexpr char Url[] = "http://192.168.4.1/";
  inline constexpr uint32_t Address = 0xc0a80401;

  // One uncompressed IN question; bounded parsing avoids pointer loops and
  // malformed labels. AAAA/other queries receive an empty answer, never a false
  // IPv6 address. No query name is logged or retained.
  inline size_t DnsReply(const uint8_t* query, size_t length, uint8_t* reply, size_t capacity) {
    if (length < 12 || length > 512 || (query[2] & 0xf8) || query[4] != 0 || query[5] != 1)
      return 0;
    size_t end = 12;
    while (end < length && query[end]) {
      const auto label = query[end++];
      if (label > 63 || end + label >= length || end + label - 12 > 254)
        return 0;
      end += label;
    }
    if (end + 5 > length || query[end + 3] != 0 || query[end + 4] != 1)
      return 0;
    const bool ipv4 = query[end + 1] == 0 && query[end + 2] == 1;
    end += 5;
    const size_t size = end + (ipv4 ? 16 : 0);
    if (capacity < size)
      return 0;
    memcpy(reply, query, end);
    reply[2] = 0x84 | (query[2] & 1); // Response, authoritative, preserve RD.
    reply[3] = 0;
    reply[6] = reply[8] = reply[9] = reply[10] = reply[11] = 0;
    reply[7] = ipv4 ? 1 : 0;
    if (ipv4) {
      const uint8_t answer[] {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 192, 168, 4, 1};
      memcpy(reply + end, answer, sizeof(answer));
    }
    return size;
  }
}
