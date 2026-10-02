#include <RtcDS1302.h>

#include "led/led.h"
#include "spi/flashdev.h"
#include "spi/sd.h"
#include "spi/sdinitroutine.h"
#include <SPI.h>

Flash *g_flash;
SD *g_sd;
FatVolume g_fat;
bool ready = false;
ThreeWire myWire(26, 24, 25); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

void initrtc() {
  Rtc.Begin();

  if (Rtc.GetIsWriteProtected()) {
    Rtc.SetIsWriteProtected(false);
  }

  if (!Rtc.GetIsRunning()) {
    Rtc.SetIsRunning(true);
  }

  if (!Rtc.IsDateTimeValid()) {
    Serial.println("RTC time invalid - setting compile time");

    RtcDateTime compiled(__DATE__, __TIME__);
    Rtc.SetDateTime(compiled);
  }
}

void dumpSDInfo() {
  auto session = g_sd->begin();
  if (!session.success) {
    log(SD_LOGTAG "Failed to init SD card");
    return;
  }
  if (!g_fat.begin(g_sd, true, 0, 0)) {
    logs("Failed to open FAT-FS :(");
    return;
  }
  log("FAT%d-FS recognized", g_fat.fatType());
  uint8_t buf[512];

  // Vol name: read from FAT LBA0
  int index = 0;
  switch (g_fat.fatType()) {
  case 12:
  case 16:
    index = 43;
    break;
  case 32:
    index = 71;
    break;
  default:
    log("Unknown FAT type: %d", g_fat.fatType());
    break;
  }
  if (!g_sd->readSector(0, buf)) {
    log("Failed to read sector 0");
    return;
  }
  char label[12];
  memcpy(label, buf + index, 11);
  label[11] = '\0';
  // Trim ending spaces
  for (int x = 10; x >= 0; x--) {
    if (label[x] == ' ') {
      label[x] = '\0';
    } else
      break;
  }
  log("Vol name: %s, size: %dMB", label, g_fat.volumeSectorCount() / 2048);

  logs("Listing files in /");
  g_fat.ls(LS_DATE | LS_SIZE | LS_R);
}

void do_setup() {
  // Begin serial
  Serial.begin(115200);
  logs("=====================================");
  logs("Hi! Mega2560 SD-Card flasher starting up! :)");

  static Flash flash;
  g_flash = &flash;
  static SD sd;
  g_sd = &sd;

  {
    SDInitRoutine sdinit;
    auto s = sdinit.begin();
    sdinit.run();
  }
  logs("Init SD done");

  // initrtc();
  dumpSDInfo();

  {
    auto s = g_flash->begin();
    if (!s.success) {
      log(FLASH_TAG "Failed to init flash device");
      return;
    }
    g_flash->transact(Flash::Cmd::READ_JEDEC_ID);
    g_flash->transact(Flash::Cmd::READ_STATUS);
    Flash::Result::Data::Status status{};
    status.bp0 = 1;
    status.bp1 = 1;
    status.bp2 = 0;
    status.bp3 = 0;
    status.srwd = 1;
    g_flash->transact(Flash::Cmd::WRITE_STATUS_REGISTER, &status);
    g_flash->setWriteProtectPin(true);
    // WP# + SRWD locks WRSR, not WREN. Do not leave WEL armed after checking.
    g_flash->transact(Flash::Cmd::WRITE_ENABLE);
    g_flash->transact(Flash::Cmd::READ_STATUS);
    g_flash->transact(Flash::Cmd::WRITE_DISABLE);
  }
}

void do_loop() {
  extern void handle_commands();
  handle_commands();

  RGBLED led(5, 6, 7);
  led.playEffect(&led.effects[0]);
  return;
  // put your main code here, to run repeatedly:

  g_flash->transact(Flash::Cmd::WRITE_ENABLE);
  Flash::Payload rdata{};
  rdata.bytes_count = 10;
  uint8_t d[23] = {};
  rdata.data = d;
  g_flash->transact(Flash::Cmd::READ_ADDRESS_DATA, &rdata);
}
