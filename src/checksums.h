#pragma once

#include <stddef.h>
#include <stdint.h>

uint8_t inline crc7(const uint8_t *data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x12;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc >> 1;
}

uint16_t inline crc16_ccitt(unsigned char const* data, unsigned int len)
{
    uint16_t crc = 0x0000;

    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;

        for (uint8_t i = 0; i < 8; ++i) {
            crc = (crc & 0x8000)
                ? (uint16_t)((crc << 1) ^ 0x1021)
                : (uint16_t)(crc << 1);
        }
    }

    return crc;
}
