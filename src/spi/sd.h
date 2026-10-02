#pragma once

#include "core.h"
#include <SdFat.h>

#define __le
#define __be
#define SD_LOGTAG "SD> "

class SD
    : public SPIWrap<SPIOwner::Sd, 49, true, KHZ(400L), MSBFIRST, SPI_MODE0>,
      public FsBlockDeviceInterface {

public:
  using Session = SPISession<SD>;

  struct Packet {
    enum class Cmd : uint8_t {
      CMD0 = 0 | 0x40,   // Note: this is wire result, not a real command value
      CMD8 = 8 | 0x40,   // SEND_IF_COND
      CMD9 = 9 | 0x40,   // get CSD
      CMD12 = 12 | 0x40, // STOP_TRANSMISSION
      CMD17 = 17 | 0x40, // Single block read
      CMD18 = 18 | 0x40, // multi block read (Not implemented)
      CMD24 = 24 | 0x40, // Single block write
      CMD25 = 25 | 0x40, // multi block write (Not implemented)
      ACMD41 = 41 | 0x40,
      CMD55 = 55 | 0x40, // "Next command is application command"
      CMD58 = 58 | 0x40, // Read OCR
      CMD59 = 59 | 0x40, // Enable crc
    } command;
    uint32_t argument;
    uint8_t crc;

    void setbit(int index, bool set);
    bool getbit(int index);
    void updcrc7();
  };

  struct Resp1 {
    bool idle_state;
    bool erase_reset;
    bool illegal_command;
    bool crc_error;
    bool erase_sequence_error;
    bool address_error;
    bool parameter_error;
    bool _valid;

    Resp1(uint8_t byte) {
#define ext(idx) ((byte & (1UL << idx)) != 0)
      idle_state = ext(0);
      erase_reset = ext(1);
      illegal_command = ext(2);
      crc_error = ext(3);
      erase_sequence_error = ext(4);
      address_error = ext(5);
      parameter_error = ext(6);
      _valid = ext(7) == 0;
#undef ext
      vlog(SD_LOGTAG "Resp1: %02x", byte);
    }

    static bool valid(uint8_t b) { return (b & 0x80) == 0; }

    bool hasError() {
      bool has = false;
#define err(x)                                                                 \
  if (x) {                                                                     \
    log(SD_LOGTAG "+" #x);                                                     \
    has = true;                                                                \
  }

      err(illegal_command);
      err(crc_error);
      err(erase_sequence_error);
      err(address_error);
      err(parameter_error);
      return has;
    }
    Resp1() = default;
  };

  struct Resp2 : public Resp1 {
    bool card_locked;
    bool wp_erase_skip_or_lock_fail;
    bool error;
    bool cc_error;
    bool card_ecc_failed;
    bool wp_violation;
    bool erase_param;
    bool out_of_range_or_csd_overwrite;
    Resp2(uint8_t byte1, uint8_t byte2) : Resp1(byte1) {

#define ext(idx) ((byte2 & (1UL << idx)) != 0)
      card_locked = ext(0);
      wp_erase_skip_or_lock_fail = ext(1);
      error = ext(2);
      cc_error = ext(3);
      card_ecc_failed = ext(4);
      wp_violation = ext(5);
      erase_param = ext(6);
      out_of_range_or_csd_overwrite = ext(7);
#undef ext
      log(SD_LOGTAG "Resp2-ext: %x", byte2);
    }

    bool hasError() {
      bool has = Resp1::hasError();

      err(card_locked);
      err(wp_erase_skip_or_lock_fail);
      err(error);
      err(address_error);
      err(card_ecc_failed);
      err(cc_error);
      err(wp_violation);
      err(erase_param);
      err(out_of_range_or_csd_overwrite);
#undef err
      return has;
    }
    Resp2() = default;
  };

  struct Resp3 : public Resp1 {
    bool card_powerup_status;
    enum class AddressMode {
      Byte,         // SDSC
      Block         // SDHC or SDXC
    } address_mode; // CCS
    bool uhs_2_card_status;
    bool s18a; // card accepted 1V8 signal switch
    Resp3(uint8_t byte1, uint32_t ocr) : Resp1(byte1) {
#define ext(idx) ((ocr & (1UL << idx)) != 0)
      card_powerup_status = ext(31);
      address_mode = ext(30) ? AddressMode::Block : AddressMode::Byte;
      uhs_2_card_status = ext(29);
      s18a = ext(24);
      log(SD_LOGTAG "OCR: %08lx, card power: %d, addr mode: %s, UHS-II=%d",
          (unsigned long)ocr, card_powerup_status, ext(30) ? "block" : "byte",
          uhs_2_card_status);
#undef ext
    }
    Resp3() = default;
  };

  struct Resp7 : public Resp1 {
    int32_t __be echo;
    Resp7(uint8_t byte1, uint32_t echo) : Resp1(byte1), echo(echo) {}

    bool valid() { return (echo & 0xFF) == 0xAA; }
    Resp7() = default;
  };

  struct DataToken {
    static bool is(uint8_t byte) { return byte == 0xFE; }
    uint32_t data[128];
    uint16_t __be crc;
    DataToken() = default;

    bool crc_check(size_t data_size_byte) {
      return calc_crc(data_size_byte) == crc;
    }
    void upd_crc(size_t data_size_byte) { crc = calc_crc(data_size_byte); }

    uint16_t __be calc_crc(size_t data_size_byte);
  };

  struct ReceivedDataErrorToken {
    bool generic_error;
    bool controller_error;
    bool card_ecc_error;
    bool out_of_range;

    static bool is(uint8_t byte) {
      // Upper4 bits all zero && one of the error is set
      return ((byte & 0xF0) == 0) && ((byte & 0xF) != 0);
    }

    ReceivedDataErrorToken(uint8_t byte) {
#define grab(off, name)                                                        \
  if (byte & (1 << off)) {                                                     \
    log(SD_LOGTAG "+" #name);                                                  \
    name = true;                                                               \
  }
      grab(0, generic_error);
      grab(1, controller_error);
      grab(2, card_ecc_error);
      grab(3, out_of_range);
    }
  };

  struct SentDataResToken {
    enum class Result { Unknown, Accepted, CrcError, WriteError } result;

    SentDataResToken(uint8_t byte) {
      switch ((byte & 0b1110) >> 1) {
      case 0b010:
        result = Result::Accepted;
        log(SD_LOGTAG "Sent data accepted");
        break;
      case 0b101:
        result = Result::CrcError;
        log(SD_LOGTAG "Sent data has crc error");
        break;
      case 0b110:
        result = Result::WriteError;
        log(SD_LOGTAG "Sent data writing error");
        break;
      default:
        log(SD_LOGTAG "Unknown tok: %x", (byte & 0b1110) >> 1);
        result = Result::Unknown;
        break;
      }
    }

    bool ok() { return result == Result::Accepted; }
    static bool is(uint8_t byte) {
      return ((byte & 0b10000) == 0) && ((byte & 0b1) == 1);
    }
  };

  constexpr static int retries = 8;

private:
  bool sendPacket(Packet p);
  bool _getR1(Packet::Cmd cmd, uint32_t arg, uint8_t *out_resp1);
  bool get4bytes(uint32_t *__be out_bytes);
  bool getR1(Packet::Cmd cmd, uint32_t arg, Resp1 *out_resp1);
  bool getR3(Packet::Cmd cmd, uint32_t arg, Resp3 *out_resp3);
  bool getR7(Packet::Cmd cmd, uint32_t arg, Resp7 *out_resp7);
  bool _readSingleDataToken(Packet::Cmd cmd, DataToken *out_tok,
                            size_t data_len, uint32_t arg);

  bool booted = false;

public:
  // Commands start
  bool goIdle();
  bool sendOpCond(uint32_t opcond);
  bool enableCRC(bool on);
  bool sendIfCond(uint32_t ifcond);
  bool readSingleDataToken(DataToken *out_tok, uint32_t lba);
  bool readCSD(DataToken *out_tok);
  uint64_t readSectorCount();
  bool sendSingleDataToken(DataToken *tok, uint32_t lba);

  Session begin(uint32_t clk = KHZ(400L));
  void end();

  // FsBlockDeviceInterface implementation
  bool isBusy() override;
  bool readSector(uint32_t sector, uint8_t *dst) override;
  bool readSectors(uint32_t sector, uint8_t *dst, size_t count) override;
  uint32_t sectorCount() override;
  bool syncDevice() override;
  bool writeSector(uint32_t sector, const uint8_t *src) override;

  bool writeSectors(uint32_t sector, const uint8_t *src, size_t count) override;

  SD();
  ~SD() override;
};
