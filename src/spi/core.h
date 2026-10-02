#pragma once

#include "../helpers.h"
#include <Arduino.h>
#include <SPI.h>

enum class SPIOwner { None, Flash, Sd, SdInit };

extern SPIOwner g_spi_owner;
extern bool g_spi_init;

#define SPI_TAG "SPI> "

#define CS_NONE 0xFFFFFFFF

template <class Device> class SPISession {
  Device *device_;

  void reset() {
    Device *device = device_;
    device_ = nullptr;
    success = false;
    if (device)
      device->end();
  }

public:
  bool success;

  SPISession(bool success = false, Device *device = nullptr)
      : device_(device), success(success) {}

  ~SPISession() { reset(); }

  SPISession(const SPISession &) = delete;
  SPISession &operator=(const SPISession &) = delete;

  SPISession(SPISession &&other) noexcept
      : device_(other.device_), success(other.success) {
    other.device_ = nullptr;
    other.success = false;
  }

  SPISession &operator=(SPISession &&other) noexcept {
    if (this != &other) {
      reset();
      device_ = other.device_;
      success = other.success;
      other.device_ = nullptr;
      other.success = false;
    }
    return *this;
  }
};

// SPIWrap: Wraps SPI transactions for a specific device, ensuring that only one
// device is active at a time.
template <SPIOwner owner, uint32_t CS, bool active_low, uint32_t defClock,
          uint8_t bitOrder, uint8_t dataMode>
class SPIWrap {
  const char *name;

public:
  using Self =
      SPIWrap<owner, CS, active_low, defClock, bitOrder, dataMode>;
  using Session = SPISession<Self>;

  enum class Status {
    None,           // Default state
    Init,           // Initialized and ready for transfers
    TransferActive, // Transfer is in place
  } status{};

  SPIWrap(const char *inName) : name(inName) {
    if (!g_spi_init) {
      SPI.begin();
      delay(100);
      g_spi_init = true;
    }
    if (CS != CS_NONE) {
      pinMode(CS, OUTPUT);
      if (active_low) {
        digitalWrite(CS, HIGH);
      } else {
        digitalWrite(CS, LOW);
      }
    }
    log(SPI_TAG "init dev: %s cs: %d", name, CS);
    status = Status::Init;
  }

  bool _begin(uint32_t clk) {
    if (g_spi_owner != SPIOwner::None || status != Status::Init) {
      log(SPI_TAG "begin: name: %s, owner: %d, status: %d", name,
          (int)g_spi_owner, (int)status);
      return false;
    }
    g_spi_owner = owner;

    log(SPI_TAG "dev: %s Start transact", name);
    SPI.beginTransaction(SPISettings(clk, bitOrder, dataMode));
    log(SPI_TAG "dev: %s Started transact", name);

    if (CS != CS_NONE) {
      log(SPI_TAG "init dev: %s cs assert: %d", name, CS);
      if (active_low) {
        digitalWrite(CS, LOW);
      } else {
        digitalWrite(CS, HIGH);
      }
    }
    status = Status::TransferActive;
    return true;
  }

  Session begin(uint32_t clk = defClock) {
    if (_begin(clk)) {
      return Session{true, this};
    } else
      return Session{false, nullptr};
  }

  struct TransferResult {
    bool success = false;
    uint8_t res = 0;

    TransferResult() = default;
    TransferResult(bool success, uint8_t res) : success(success), res(res) {}
    TransferResult(bool success) : success(success) {}

    bool takeIf(uint8_t *out) {
      if (!success)
        return false;
      *out = res;
      return true;
    }
  };

  TransferResult transfer(uint8_t in) {
    uint8_t out;
    if (g_spi_owner != owner || status != Status::TransferActive) {
      log(SPI_TAG "transfer: name: %s, owner: %d, status: %d", name,
          (int)g_spi_owner, (int)status);
      return TransferResult(false);
    }
    vlog(SPI_TAG "dev: %s send 0x%02x", name, in);
    out = SPI.transfer(in);
    vlog(SPI_TAG "dev: %s got 0x%02x", name, out);
    return TransferResult(true, out);
  }

  TransferResult transfer() {
    uint8_t out;
    if (g_spi_owner != owner || status != Status::TransferActive) {
      log(SPI_TAG "transfer: name: %s, owner: %d, status: %d", name,
          (int)g_spi_owner, (int)status);
      return TransferResult(false);
    }
    vlog(SPI_TAG "dev: %s send dummy", name);
    out = SPI.transfer(0xFF);
    vlog(SPI_TAG "dev: %s got 0x%02x", name, out);
    return TransferResult(true, out);
  }

  void end() {
    if (g_spi_owner != owner || status != Status::TransferActive) {
      log(SPI_TAG "end: name: %s, owner: %d, status: %d", name,
          (int)g_spi_owner, (int)status);
      return;
    }
    if (CS != CS_NONE) {
      log(SPI_TAG "end: name: %s cs deassert: %d", name, CS);
      if (active_low) {
        digitalWrite(CS, HIGH);
      } else {
        digitalWrite(CS, LOW);
      }
    }
    SPI.endTransaction();
    log(SPI_TAG "dev: %s end transact", name);
    status = Status::Init;
    g_spi_owner = SPIOwner::None;
  }
};
