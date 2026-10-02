#include "flashdev.h"

// MX25L6406E

constexpr auto SECTOR_SHIFT = 12;
constexpr auto SECTOR_MASK = ((1U << SECTOR_SHIFT) - 1);
constexpr size_t PAGE_SIZE = 256;

Flash::Flash() : SPIWrap("flash") {
  pinMode(WP_ENABLE_GPIO, OUTPUT);
  pinMode(HOLD_GPIO, OUTPUT);
  digitalWrite(WP_ENABLE_GPIO, HIGH);
  digitalWrite(HOLD_GPIO, HIGH);
}

void Flash::setMode(Mode mode, bool enable) {
  uint8_t val = enable ? LOW : HIGH;
  uint8_t pin = 0;
  switch (mode) {
  case Mode::WriteProtect:
    pin = WP_ENABLE_GPIO;
    break;
  case Mode::Hold:
    pin = HOLD_GPIO;
    break;
  }
  log(FLASH_TAG "setMode: %s to %s",
      mode == Mode::WriteProtect ? "WriteProtect" : "Hold",
      enable ? "ENABLED" : "DISABLED");
  digitalWrite(pin, val);
}

static void de_reassert_cs() {
  digitalWrite(53, HIGH);
  delay(50);
  digitalWrite(53, LOW);
}

Flash::Result Flash::transact(Cmd cmd, void *data) {
  Result res{};
  describe(cmd, "Start");

  switch (cmd) {
  case Cmd::READ_JEDEC_ID: {
    res.ok = true;
    res.ok &= transfer(0x9F).success;
    res.ok &= transfer().takeIf(&res.data.jedec_id.manufacturer);
    res.ok &= transfer().takeIf(&res.data.jedec_id.memoryType);
    res.ok &= transfer().takeIf(&res.data.jedec_id.density);
    break;
  }
  case Cmd::READ_STATUS: {
    auto status = transfer(0x05);
    res.ok = status.success;
    status = transfer();
    res.ok &= status.success;
    if (!res.ok) {
      log(FLASH_TAG "READ_STATUS failed");
      break;
    }
    res.data.status.wel = extractbit(status.res, 1);
    res.data.status.wip = extractbit(status.res, 0);
    res.data.status.bp0 = extractbit(status.res, 2);
    res.data.status.bp1 = extractbit(status.res, 3);
    res.data.status.bp2 = extractbit(status.res, 4);
    res.data.status.bp3 = extractbit(status.res, 5);
    res.data.status.srwd = extractbit(status.res, 7);
    break;
  }
  case Cmd::WRITE_ENABLE: {
    auto status = transfer(0x06);
    res.ok = status.success;
    break;
  }
  case Cmd::READ_ADDRESS_DATA: {
    Payload *range = (Payload *)data;

    if (data == NULL) {
      res.ok = false;
      break;
    }
    res.ok = true;

    res.ok &= transfer(0x03).success;
    res.ok &= transfer((uint8_t)(range->address >> 16)).success;
    res.ok &= transfer((uint8_t)(range->address >> 8)).success;
    res.ok &= transfer((uint8_t)range->address).success;

    for (size_t idx = 0; idx < range->bytes_count; idx++) {
      res.ok &= transfer().takeIf(&range->data[idx]);
    }
    break;
  }
  case Cmd::PROGRAM_PAGE: {
    Payload *range = (Payload *)data;

    if (data == NULL) {
      res.ok = false;
      break;
    }
    res.ok = true;

    // Check address is page-aligned, if not, partial flash.
    int modulo = range->address % PAGE_SIZE;
    if (modulo != 0) {
      log(FLASH_TAG "prog-page: page+%d..page-end", modulo);
    }
    if (range->bytes_count > PAGE_SIZE - modulo) {
      log(FLASH_TAG "prog-page: bytes_count %d exceeds page end, fixing it",
          (int)range->bytes_count);
      range->bytes_count = PAGE_SIZE - modulo;
    }

    // Enable write
    if (!enableWrite()) {
      log(FLASH_TAG "prog-page: enableWrite failed");
      res.ok = false;
      break;
    }

    res.ok &= transfer(0x02).success;
    res.ok &= transfer((uint8_t)(range->address >> 16)).success;
    res.ok &= transfer((uint8_t)(range->address >> 8)).success;
    res.ok &= transfer((uint8_t)range->address).success;

    for (size_t idx = 0; idx < range->bytes_count; idx++) {
      res.ok &= transfer(range->data[idx]).success;
    }

    if (!res.ok) {
      log(FLASH_TAG "prog-page: transfer failed");
      break;
    }

    // Release CS
    de_reassert_cs();

    if (!pollReady()) {
      log(FLASH_TAG "prog-page: pollReady failed");
      res.ok = false;
      break;
    }

    log(FLASH_TAG "prog-page: WIP bit clear, write complete");
    break;
  }
  case Cmd::SECTOR_ERASE: {
    Payload *range = (Payload *)data;

    if (data == NULL) {
      res.ok = false;
      break;
    }
    res.ok = true;

    if (!enableWrite()) {
      log(FLASH_TAG "sector-erase: enableWrite failed");
      res.ok = false;
      break;
    }

    if ((range->address & SECTOR_MASK) != 0) {
      log(FLASH_TAG "sector-erase: Clearing non-sector-aligned address part %x",
          range->address & SECTOR_MASK);
      range->address &= ~SECTOR_MASK;
    }

    res.ok &= transfer(0x20).success; // Sector Erase command
    res.ok &= transfer((uint8_t)(range->address >> 16)).success;
    res.ok &= transfer((uint8_t)(range->address >> 8)).success;
    res.ok &= transfer((uint8_t)range->address).success;

    if (!res.ok) {
      log(FLASH_TAG "sector-erase: transfer failed");
      break;
    }

    // Release CS
    de_reassert_cs();

    if (!pollReady()) {
      log(FLASH_TAG "sector-erase: pollReady failed");
      res.ok = false;
      break;
    }

    log(FLASH_TAG "sector-erase: WIP bit clear, erase complete");
    break;
  }
  case Cmd::BLOCK_ERASE: {
    Payload *range = (Payload *)data;

    if (data == NULL) {
      res.ok = false;
      break;
    }
    res.ok = true;

    if (!enableWrite()) {
      log(FLASH_TAG "block-erase: enableWrite failed");
      res.ok = false;
      break;
    }

    if ((range->address & 0xFFFF) != 0) {
      log(FLASH_TAG
          "block-erase: Not block-aligned address, use sector-erase instead");
      res.ok = false;
      break;
    }

    res.ok &= transfer(0xD8).success; // Block Erase command
    res.ok &= transfer((uint8_t)(range->address >> 16)).success;
    res.ok &= transfer((uint8_t)(range->address >> 8)).success;
    res.ok &= transfer((uint8_t)range->address).success;

    if (!res.ok) {
      log(FLASH_TAG "block-erase: transfer failed");
      break;
    }

    // Release CS
    de_reassert_cs();

    if (!pollReady()) {
      log(FLASH_TAG "block-erase: pollReady failed");
      res.ok = false;
      break;
    }

    log(FLASH_TAG "block-erase: WIP bit clear, erase complete");
    break;
  }
  case Cmd::READ_SFDP: {
    Payload *range = (Payload *)data;

    if (data == NULL) {
      res.ok = false;
      break;
    }
    res.ok = true;

    res.ok &= transfer(0x5A).success;
    res.ok &= transfer((uint8_t)(range->address >> 16)).success;
    res.ok &= transfer((uint8_t)(range->address >> 8)).success;
    res.ok &= transfer((uint8_t)range->address).success;
    res.ok &= transfer(0x00).success; // Dummy byte

    for (size_t idx = 0; idx < range->bytes_count; idx++) {
      res.ok &= transfer().takeIf(&range->data[idx]);
    }

    // Done? make it shut up
    de_reassert_cs();
    break;
  }
  case Cmd::WRITE_STATUS_REGISTER: {
    Result::Data::Status *status = (Result::Data::Status *)data;
    if (data == NULL) {
      res.ok = false;
      break;
    }

    if (!enableWrite()) {
      log(FLASH_TAG "write-status-register: enableWrite failed");
      res.ok = false;
      break;
    }

    res.ok = true;
    res.ok &= transfer(0x01).success; // Write Status Register command
    res.ok &=
        transfer(((status->srwd << 7) | (status->bp3 << 5) |
                  (status->bp2 << 4) | (status->bp1 << 3) | (status->bp0 << 2)))
            .success;

    if (!res.ok) {
      log(FLASH_TAG "write-status-register: transfer failed");
      break;
    }

    de_reassert_cs();

    if (!pollReady()) {
      log(FLASH_TAG "write-status-register: pollReady failed");
      res.ok = false;
      break;
    }
    log(FLASH_TAG "write-status-register: WIP bit clear, write complete");
    break;
  }
  case Cmd::WRITE_DISABLE: {
    auto status = transfer(0x04);
    res.ok = status.success;
    break;
  }
  default:
    log(FLASH_TAG "transact: No implementation for command %d", (int)cmd);
    res.ok = false;
    break;
  } // switch

  if (res.ok) {
    describe(cmd, res);
    describe(cmd, "End. Ok");
  } else {
    describe(cmd, "End. Fail");
  }

  return res;
}

bool Flash::pollReady() {
  auto t = Timer::io();
  while (!t.rang()) {
    auto res = transact(Cmd::READ_STATUS);
    if (!res.ok) {
      log(FLASH_TAG "pollReady: READ_STATUS failed");
      return false;
    }
    if (!res.data.status.wip) {
      log(FLASH_TAG "pollReady: Ready");
      return true;
    }
  }
  log(FLASH_TAG "pollReady: Timeout");
  return false;
}

bool Flash::enableWrite() {
  auto res = transact(Cmd::READ_STATUS);
  if (!res.ok) {
    log(FLASH_TAG "enableWrite: READ_STATUS failed");
    return false;
  }
  if (res.data.status.getProtLvl() != Result::Data::Status::ProtLevel::None) {
    log(FLASH_TAG "enableWrite: Device is write-protected, abort");
    return false;
  }
  res = transact(Cmd::WRITE_ENABLE);
  if (!res.ok) {
    log(FLASH_TAG "enableWrite: WRITE_ENABLE failed");
    return false;
  }
  res = transact(Cmd::READ_STATUS);
  if (!res.ok) {
    log(FLASH_TAG "enableWrite: READ_STATUS failed");
    return false;
  }
  if (!res.data.status.wel) {
    log(FLASH_TAG "enableWrite: WEL bit not set, abort");
    return false;
  }
  log(FLASH_TAG "enableWrite: WEL bit set, write enabled");
  return true;
}

bool Flash::readSFDP(Result::Data::SFDP *out_sfdp) {
  Payload range{};
  range.address = 0;
  range.bytes_count = sizeof(Result::Data::SFDP);
  range.data = (uint8_t *)out_sfdp;

  auto res = transact(Cmd::READ_SFDP, &range);
  if (!res.ok) {
    log(FLASH_TAG "readSFDP: READ_SFDP failed");
    return false;
  }
  return true;
}

void Flash::describe(Cmd cmd, Result &res) {
  switch (cmd) {
  case Cmd::READ_JEDEC_ID: {
    log(FLASH_TAG "JEDEC ID: 0x%02x:0x%02x:0x%02x",
        res.data.jedec_id.manufacturer, res.data.jedec_id.memoryType,
        res.data.jedec_id.density);
    break;
  }
  case Cmd::READ_STATUS: {
    log(FLASH_TAG "Status: wel: %d wip %d bp3..0: 0b%d%d%d%d protLvl: %s",
        res.data.status.wel, res.data.status.wip, res.data.status.bp3,
        res.data.status.bp2, res.data.status.bp1, res.data.status.bp0,
        Result::Data::Status::protLvl_str(res.data.status.getProtLvl())
            .c_str());
    break;
  }
  case Cmd::WRITE_ENABLE: {
    log(FLASH_TAG "Write enabled: %d", res.ok);
    break;
  }
  default:
    log(FLASH_TAG "Describe: No description for command %d", (int)cmd);
  }
}

Flash::Result::Data::Status::ProtLevel
Flash::Result::Data::Status::getProtLvl() const {
  /*
    BP3…BP0	Protected area
    0000	none
    0001	blocks 126–127
    0010	blocks 124–127
    0011	blocks 120–127
    0100	blocks 112–127
    0101	blocks 96–127
    0110	blocks 64–127
    0111	entire chip
    1000	entire chip
    1001	blocks 0–63
    1010	blocks 0–95
    1011	blocks 0–111
    1100	blocks 0–119
    1101	blocks 0–123
    1110	blocks 0–125
    1111	entire chip

    if 0 = None;
    if 0111 or 0111 or 1111 = Entire chip;
    if first bit is 1; upper half protected; if first bit is 0; lower half
    protected
  */
  int bp = (bp3 << 3) | (bp2 << 2) | (bp1 << 1) | bp0;
  if (bp == 0)
    return ProtLevel::None;
  if (bp == 0b0111 || bp == 0b1000 || bp == 0b1111)
    return ProtLevel::All;
  if (bp & 0b1000)
    return ProtLevel::Upper;
  return ProtLevel::Lower;
}