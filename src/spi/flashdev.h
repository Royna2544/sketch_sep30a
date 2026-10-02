#pragma once

#include "core.h"

#define FLASH_TAG "FLASH> "

class Flash
    : public SPIWrap<SPIOwner::Flash, 53, true, MHZ(4), MSBFIRST, SPI_MODE0> {
  using gpio_t = int;

  constexpr static gpio_t WP_ENABLE_GPIO = 41;
  constexpr static gpio_t HOLD_GPIO = 40;

public:
  Flash();

  enum class Mode { WriteProtect, Hold };

  // Set the mode of the flash device. For example, to enable write protection,
  // call setMode(Mode::WriteProtect, true).
  void setMode(Mode mode, bool enable);

public:
#define FLASH_CMD(f)                                                           \
  f(READ_JEDEC_ID) f(READ_STATUS) f(WRITE_ENABLE) f(READ_ADDRESS_DATA)
  enum class Cmd {
#define fn(x) x,
    FLASH_CMD(fn)
#undef fn
  };

  struct ReadData {
    uint32_t address;
    size_t bytes_count;
    uint8_t *data; // At least bytes_count sized please
  };

  struct Result {
    bool ok;
    union Data {
      struct JedecID {
        uint8_t manufacturer, memoryType, density;
      } jedec_id;
      struct Status {
        bool wel;                // Write-enable latch status 1=set, 0=not set
        bool wip;                // Work-in-progress; 1=wip, 0=idle
        bool srwd;               // status-register write protection with WP#
        bool bp0, bp1, bp2, bp3; // Block protection
      } status;
    } data;
  };

  // Transact a command with optional data. Returns a Result struct with the
  // outcome.
  Result transact(Cmd cmd, void *data = NULL);

  // Simple logging with the command's name and action. For example,
  // "READ_STATUS: Start"
  void describe(Cmd cmd, const char *action) {
#define fn(x)                                                                  \
  case Cmd::x:                                                                 \
    log(FLASH_TAG "%s: %s", #x, action);                                       \
    break;
    switch (cmd) { FLASH_CMD(fn) }
#undef fn
  }

  // Describe the result of a command. For example, "READ_STATUS: wel: 1 wip 0"
  void describe(Cmd cmd, Result &res);
#undef FLASH_CMD
};
