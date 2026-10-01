#include "flashdev.h"

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
  digitalWrite(pin, val);
}

Flash::Result Flash::transact(Cmd cmd, void *data) {
  Result res{};
  auto session = begin();
  res.ok = session.success;
  if (!res.ok) {
    log(FLASH_TAG "transact failed: SPI begin failed: cmd: %d", (int)cmd);
    return res;
  }
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
    transfer(0x05);
    auto status = transfer();
    res.ok = status.success;
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
    ReadData *range = (ReadData *)data;

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
  }

  if (res.ok) {
    describe(cmd, res);
    describe(cmd, "End. Ok");
  } else {
    describe(cmd, "End. Fail");
  }

  return res;
}

void Flash::describe(Cmd cmd, Result &res) {
  switch (cmd) {
  case Cmd::READ_JEDEC_ID: {
    log(FLASH_TAG "JEDEC ID: 0x%02x:0x%02x:0x%02x", res.data.jedec_id.manufacturer,
        res.data.jedec_id.memoryType, res.data.jedec_id.density);
    break;
  }
  case Cmd::READ_STATUS: {
    log(FLASH_TAG "Status: wel: %d wip %d", res.data.status.wel, res.data.status.wip);
    break;
  }
  case Cmd::WRITE_ENABLE: {
    log(FLASH_TAG "Write enabled: %d", res.ok);
    break;
  }
  case Cmd::READ_ADDRESS_DATA: {
    break;
  }
  }
}