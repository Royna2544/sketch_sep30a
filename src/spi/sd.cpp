#include "sd.h"
#include "../checksums.h"

void SD::Packet::setbit(int index, bool set) {
  if (!ASSERT_TRUE(index < 32 && index >= 0)) {
    return;
  }
  if (set)
    argument |= (1UL << index);
  else
    argument &= ~(1UL << index);
  updcrc7();
}
bool SD::Packet::getbit(int index) {
  if (!ASSERT_TRUE(index < 32 && index >= 0)) {
    return false;
  }
  return argument & (1UL << index);
}

void SD::Packet::updcrc7() {
  uint8_t packet[5];

  packet[0] = (uint8_t)command;
  flip((uint8_t *)&argument, 4, (uint8_t *)&packet[1]);
  crc = (crc7(packet, 5) << 1) | 1;
}

uint16_t __be SD::DataToken::calc_crc(size_t data_size_byte) {
  uint16_t __le calc_crc = crc16_ccitt((uint8_t *)data, data_size_byte);
  uint16_t __be flipped;
  flip((uint8_t *)&calc_crc, 2, (uint8_t *)&flipped);
  return flipped;
}

bool SD::sendPacket(Packet p) {
  p.updcrc7(); // Ensure updated, failsafe

  uint32_t __be argument;
  flip((uint8_t *)&p.argument, sizeof(p.argument), (uint8_t *)&argument);
  p.argument = argument;

  uint8_t *cursor = (uint8_t *)&p;
  bool commandSent = true;
  for (size_t idx = 0; idx < sizeof(p); idx++) {
    commandSent &= transfer(*cursor).success;
    cursor++;
  }
  return commandSent;
}

bool SD::_getR1(Packet::Cmd cmd, uint32_t arg, uint8_t *out_resp1) {
  Packet p{};
  p.command = cmd;
  p.argument = arg;

  if (sendPacket(p)) {
    auto t = Timer::c();
    while (!t.rang()) {
      auto getR1 = transfer();
      if (getR1.success && Resp1::valid(getR1.res)) {
        Resp1 res{getR1.res};
        if (res.hasError()) {
          log(SD_LOGTAG "getR1, errors");
          return false;
        }
        *out_resp1 = getR1.res;
        return true;
      }
    }
  }
  log(SD_LOGTAG "getR1, retry timed out");
  return false;
}

bool SD::get4bytes(uint32_t *__be out_bytes) {
  uint8_t __be outData[4];
  for (int i = 0; i < 4; i++) {
    auto outDataRes = transfer();
    if (outDataRes.success) {
      outData[i] = outDataRes.res;
    } else {
      return false;
    }
  }
  uint32_t __be out;
  flip(outData, sizeof(out), (uint8_t *)&out);
  *out_bytes = out;
  return true;
}

bool SD::getR1(Packet::Cmd cmd, uint32_t arg, Resp1 *out_resp1) {
  uint8_t res;
  if (!_getR1(cmd, arg, &res))
    return false;
  *out_resp1 = Resp1(res);
  return true;
}

bool SD::getR3(Packet::Cmd cmd, uint32_t arg, Resp3 *out_resp3) {
  uint8_t res;
  uint32_t ocr;
  if (!_getR1(cmd, arg, &res))
    return false;
  if (!get4bytes(&ocr))
    return false;

  *out_resp3 = Resp3(res, ocr);
  return true;
}

bool SD::getR7(Packet::Cmd cmd, uint32_t arg, Resp7 *out_resp7) {
  uint8_t res;
  uint32_t echo;
  if (!_getR1(cmd, arg, &res))
    return false;
  if (!get4bytes(&echo))
    return false;

  // Verify byte 4 for repeat
  if ((arg & 0xFF) != (echo & 0xFF))
    return false;

  *out_resp7 = Resp7(res, echo);
  return true;
}

// Commands start

// CMD0
bool SD::goIdle() {
  Resp1 resp1;
  if (!getR1(Packet::Cmd::CMD0, 0, &resp1))
    return false;
  if (!resp1.idle_state) {
    log(SD_LOGTAG "not idle, abort");
    return false;
  }
  return true;
}

// ACMD41
bool SD::sendOpCond(uint32_t opcond) {
  Resp1 resp1;
  auto t = Timer::c();
  while (!t.rang()) {
    if (!getR1(Packet::Cmd::CMD55, 0, &resp1))
      return false;
    if (!getR1(Packet::Cmd::ACMD41, opcond, &resp1))
      return false;
    if (!resp1.idle_state)
      break;
    delay(50);
  }
  if (resp1.idle_state) {
    log(SD_LOGTAG "Failed to prepare SDHC");
    return false;
  }
  return true;
}

bool SD::enableCRC(bool on) {
  Resp1 resp1;
  return getR1(Packet::Cmd::CMD59, on, &resp1);
}

bool SD::sendIfCond(uint32_t ifcond) {
  Resp7 resp7;
  return getR7(Packet::Cmd::CMD8, ifcond, &resp7);
}

// CMD17 = data_len: based on byte
bool SD::_readSingleDataToken(Packet::Cmd cmd, DataToken *out_tok,
                              size_t data_len, uint32_t arg) {
  Resp1 resp1;
  if (!getR1(cmd, arg, &resp1))
    return false;
  if (resp1.idle_state) {
    log(SD_LOGTAG "read didnt start");
    return false;
  }

  auto t = Timer::io();
  while (!t.rang()) {
    auto dummyRes = transfer();
    if (!dummyRes.success)
      return false;
    if (DataToken::is(dummyRes.res)) {
      log(SD_LOGTAG "Fetching %uB block data", (unsigned int)data_len);
      DataToken tk;
      uint8_t *cursor = (uint8_t *)tk.data;
      for (size_t i = 0; i < data_len; i++) {
        auto dataRes = transfer();
        if (!dataRes.success)
          return false;
        *cursor = dataRes.res;
        cursor++;
      }
      cursor = (uint8_t *)&tk.crc;
      for (size_t i = 0; i < 2; i++) {
        auto dataRes = transfer();
        if (!dataRes.success)
          return false;
        *cursor = dataRes.res;
        cursor++;
      }
      bool crcMatch = tk.crc_check(data_len);
      log(SD_LOGTAG "read done, incl. CRC: got: %04X, match=%d", tk.crc,
          crcMatch);
      if (!crcMatch)
        return false;
      *out_tok = tk;
      return true;
    } else if (ReceivedDataErrorToken::is(dummyRes.res)) {
      log(SD_LOGTAG "getDataTok: Received error tok");
      ReceivedDataErrorToken tok(dummyRes.res);
      return false;
    }
  }
  log(SD_LOGTAG "getdatatok: Timeout");
  return false;
}

bool SD::readSingleDataToken(DataToken *out_tok, uint32_t lba) {
  return _readSingleDataToken(Packet::Cmd::CMD17, out_tok,
                              sizeof(out_tok->data), lba);
}

bool SD::readCSD(DataToken *out_tok) {
  return _readSingleDataToken(Packet::Cmd::CMD9, out_tok, 16, 0);
}

uint64_t SD::readSectorCount() {
  DataToken csdtok;
  if (!readCSD(&csdtok))
    return 0;
  uint8_t *csd = (uint8_t *)csdtok.data;
  if ((csd[0] >> 6) == 0b01) {
    log(SD_LOGTAG "CSDv2 format");
    // 16B = 128b = 127~120 119~112 111~104 103~96 95~88 87~80 79~72 71 70 69 68
    // 67 66 65 64
    uint64_t rawValue = (uint32_t)(csd[7] & 0b111111) << 16 |
                        ((uint32_t)csd[8] << 8) | (uint32_t)csd[9];
    log(SD_LOGTAG "card size: %dMB", (rawValue + 1) / 2);
    return (rawValue + 1) * 1024;
  }
  log(SD_LOGTAG "Unknown CSD format");
  return 0;
}

bool SD::sendSingleDataToken(DataToken *tok, uint32_t lba) {
  Resp1 resp1;
  if (!getR1(Packet::Cmd::CMD24, lba, &resp1))
    return false;
  if (resp1.idle_state) {
    log(SD_LOGTAG "op didnt start, abort");
    return false;
  }
  // Start data tok
  if (!transfer(0xFE).success)
    return false;
  // Failsafe
  tok->upd_crc(sizeof(tok->data));
  // Send data + crc
  uint8_t *cursor = (uint8_t *)tok->data;
  for (size_t i = 0; i < sizeof(*tok); i++) {
    if (!transfer(*cursor).success) {
      log(SD_LOGTAG "Failed to send data tok");
      return false;
    }
    cursor++;
  }
  // wait response
  bool ready = false;
  auto t = Timer::io();
  while (!t.rang()) {
    auto waitResp = transfer();
    if (!waitResp.success)
      return false;
    if (SentDataResToken::is(waitResp.res)) {
      if (!SentDataResToken{waitResp.res}.ok())
        return false;
      log(SD_LOGTAG "Got Data Resp Tok in %dms", t.dur());
      ready = true;
      break;
    }
  }
  if (!ready) {
    log(SD_LOGTAG "wait-resp timeout");
    return false;
  }

  // Wait for write
  t = Timer::io();
  ready = false;
  while (!t.rang()) {
    auto waitResp = transfer();
    if (!waitResp.success)
      return false;

    // If MISO driven high...
    if (waitResp.res == 0xFF) {
      log(SD_LOGTAG "Sdcard ready in %dms", t.dur());
      ready = true;
      break;
    }
  }
  if (!ready) {
    log(SD_LOGTAG "wait-write timeout");
    return false;
  }

  log(SD_LOGTAG "Write success.");
  return true;
}

SD::Session SD::begin(uint32_t clk) {
  if (!Base::_begin(clk))
    return {false, nullptr};

  Session session{false, this};
  Resp3 resp3;

  log(SD_LOGTAG "Init card in SPI mode, send CMD0");
  if (!goIdle())
    return session;
  log(SD_LOGTAG "CMD0 success, proceed to CMD8");
  if (!sendIfCond(0x01AA /* 2.7–3.6 V, pattern AA */))
    return session;
  log(SD_LOGTAG "CMD8 success, enable crc");
  if (!enableCRC(true))
    return session;
  log(SD_LOGTAG "CRC on, prepare sdhc");
  if (!sendOpCond(/* opcond=HCS */ 1UL << 30))
    return session;
  log(SD_LOGTAG "Card ready on SDHC");

  // read ocr
  if (!getR3(Packet::Cmd::CMD58, 0, &resp3))
    return session;
  if (!resp3.card_powerup_status ||
      resp3.address_mode == Resp3::AddressMode::Byte) {
    log(SD_LOGTAG "Card powerup status false or address mode byte");
    return session;
  }
  log(SD_LOGTAG "Card powered up correctly");
  session.success = true;
  return session;
}

void SD::end() {
  digitalWrite(49, HIGH); // deassert CS
  transfer();             // provide trailing clocks with the card deselected
  Base::end();
}

bool SD::isBusy() { return false; }

bool SD::readSector(uint32_t sector, uint8_t *dst) {
  DataToken tok;
  if (!readSingleDataToken(&tok, sector))
    return false;
  memcpy(dst, tok.data, sizeof(tok.data));
  return true;
}

bool SD::readSectors(uint32_t sector, uint8_t *dst, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (!readSector(sector + i, dst)) {
      return false;
    }
    dst += 512;
  }
  return true;
}

uint32_t SD::sectorCount() { return readSectorCount(); }

bool SD::syncDevice() { return true; }

bool SD::writeSector(uint32_t sector, const uint8_t *src) {
  DataToken tok;
  memcpy(tok.data, src, sizeof(tok.data));
  if (!sendSingleDataToken(&tok, sector))
    return false;
  return true;
}

bool SD::writeSectors(uint32_t sector, const uint8_t *src, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (!writeSector(sector + i, src)) {
      return false;
    }
    src += 512;
  }
  return true;
}

SD::SD() : SPIWrap("SD"), FsBlockDeviceInterface() {}
SD::~SD() = default;
