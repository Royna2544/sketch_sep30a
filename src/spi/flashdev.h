#pragma once

#include "core.h"

#define FLASH_TAG "FLASH> "

class Flash
    : public SPIWrap<SPIOwner::Flash, CS_NONE, true, MHZ(4), MSBFIRST,
                     SPI_MODE0> {
  using Base =
      SPIWrap<SPIOwner::Flash, CS_NONE, true, MHZ(4), MSBFIRST, SPI_MODE0>;
  using gpio_t = int;

  constexpr static gpio_t CS_GPIO = 53;
  constexpr static gpio_t WP_ENABLE_GPIO = 41;
  constexpr static gpio_t HOLD_GPIO = 40;

  bool sessionActive() const;
  bool beginCommand();
  void endCommand();
  bool isRangeWritable(uint32_t address, uint32_t byte_count);

public:
  Flash();

  enum class Mode { WriteProtect, Hold };

  // WriteProtect asserts the active-low WP# pin. WP# only locks changes to
  // SRWD/BP3..BP0 when SRWD is already set; the BP bits protect array data.
  void setMode(Mode mode, bool enable);
  void setWriteProtectPin(bool asserted) {
    setMode(Mode::WriteProtect, asserted);
  }
  bool writeProtectPinAsserted() const;

public:
#define FLASH_CMD(f)                                                           \
  f(READ_JEDEC_ID) f(READ_STATUS) f(WRITE_ENABLE) f(READ_ADDRESS_DATA)         \
      f(SECTOR_ERASE) f(PROGRAM_PAGE) f(READ_SFDP) f(BLOCK_ERASE)              \
          f(WRITE_STATUS_REGISTER) f(WRITE_DISABLE)
  enum class Cmd {
#define fn(x) x,
    FLASH_CMD(fn)
#undef fn
  };

  struct Payload {
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

        enum class ProtLevel { None, Upper, Lower, All };

        struct ProtectedRange {
          bool enabled;
          uint32_t first;
          uint32_t last;
        };

        uint8_t blockProtectBits() const;
        uint8_t writableValue() const;
        ProtLevel getProtLvl() const;
        ProtectedRange getProtectedRange() const;
        bool protects(uint32_t address, uint32_t byte_count) const;
        static const char *protLvl_str(ProtLevel level);
      } status;
      struct SFDP {
        enum : uint8_t { MAX_PARAMETER_HEADERS = 4 };

        char signature[4];
        uint8_t minor, major;
        uint16_t parameterHeaderCount; // Decoded count: raw NPH + 1.
        uint8_t accessProtocol;
        uint8_t parsedParameterHeaderCount;
        bool parameterHeadersTruncated;

        struct ParamHeader {
          uint8_t id_lsb, minor, major;
          uint8_t tableLengthDwords;
          uint32_t tablePointer;
          uint8_t id_msb;

          uint16_t id() const {
            return (static_cast<uint16_t>(id_msb) << 8) | id_lsb;
          }
        } paramHeaders[MAX_PARAMETER_HEADERS];

        struct BasicFlashParameters {
          struct EraseType {
            uint8_t sizeExponent;
            uint8_t opcode;
          } eraseTypes[4];

          bool present;
          uint8_t minor, major;
          uint8_t tableLengthDwords;
          uint8_t addressBytesMode; // JESD216 DWORD 1 bits 18:17.
          uint8_t erase4kOpcode;
          uint64_t densityBits;
          uint32_t densityBytes;
        } basic;
      };
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

  // Poll sr.WIP until it is clear or timeout_ms has expired.
  // Returns true if the device is ready, false if timeout or error.
  bool pollReady(uint32_t timeout_ms = 2100);

  // Issue WREN and verify WEL. BP bits do not prevent WREN; program/erase
  // commands perform their own address-aware protection check.
  // Returns true if successful, false otherwise.
  bool enableWrite();

  // Bounded raw SFDP access. Each call emits 5Ah, a 24-bit address, one dummy
  // byte, then reads byte_count bytes.
  bool readSFDPBytes(uint32_t address, uint8_t *out, size_t byte_count);

  // Read one parameter header or one little-endian parameter-table DWORD.
  bool readSFDPParameterHeader(uint8_t index,
                               Result::Data::SFDP::ParamHeader *out);
  bool readSFDPDword(const Result::Data::SFDP::ParamHeader &header,
                     uint8_t index, uint32_t *out);

  // Validate the SFDP root, parse NPH+1 parameter headers, and decode the
  // JEDEC Basic Flash Parameter Table without overlaying wire bytes on structs.
  bool readSFDP(Result::Data::SFDP *out_sfdp);
};
