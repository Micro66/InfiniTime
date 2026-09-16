#pragma once
#include <zlib.h>
#include <cstdint>
inline uint32_t esp_rom_crc32_le(uint32_t seed, const uint8_t* bytes, size_t size) { return crc32(seed, bytes, size); }
