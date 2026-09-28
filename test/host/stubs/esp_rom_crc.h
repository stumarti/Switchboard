#pragma once
#include <cstdint>
inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* p, uint32_t n) {
  crc = ~crc;
  while (n--) { crc ^= *p++; for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
  return ~crc;
}
