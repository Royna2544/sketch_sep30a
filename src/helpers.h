#pragma once

#include <Arduino.h>
#include <stdarg.h>

// Log string literal without format
#define logs Serial.println
// Log string literal with printf-style format
#define log logging
inline void logging(const char *fmt, ...) {
  va_list args;
  char logbuf[64] = {};

  va_start(args, fmt);
  vsnprintf(logbuf, sizeof(logbuf), fmt, args);
  va_end(args);

  Serial.println(logbuf);
}

// Verbose variants
#ifdef DEBUG
#define vlogs(x, ...) logging(x, ##__VA_ARGS__)
#define vlog(x, ...) logging(x, ##__VA_ARGS__)
#else
#define vlogs(x, ...)
#define vlog(x, ...)
#endif

// Give Mhz value from raw number. Example: MHZ(8) = 8000000
#define MHZ(x) (x * 1000000)
#define KHZ(x) (x * 1000)

// Extract bit at index from byte value
inline bool extractbit(uint8_t val, int index) { return val & (1UL << index); }

// Assert that condition is true, log if false
#define ASSERT_TRUE(cond)                                                      \
  ({                                                                           \
    if (!(cond))                                                               \
      log("ASSERT: func: %s, line %d, %s == FALSE", __func__, __LINE__,        \
          #cond);                                                              \
    cond;                                                                      \
  })

// Flip byte order of input array into output array
inline void flip(uint8_t *in, size_t bytes, uint8_t *out) {
  if (!ASSERT_TRUE(in != out))
    return;
  for (size_t i = 1; i <= bytes; i++)
    out[bytes - i] = in[i - 1];
}
