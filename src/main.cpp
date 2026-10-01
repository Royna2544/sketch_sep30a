#include <RtcDS1302.h>

#include "core.h"
#include <SPI.h>
#include <stdarg.h>

Flash *g_flash;
SD *g_sd;
FatVolume g_fat;
bool ready = false;
ThreeWire myWire(26, 24, 25); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

#define logs Serial.println
#define vlogs(x, ...)

struct Command {
  const char *name;
  const char *help;
  int (*function)(
      int argc,
      char **argv); // return 0 for no errors, >0 for command specific errors.

  constexpr static int SUBSYS_SD = 0;
  constexpr static int SUBSYS_FLASH = 2;
} commands[2][90] = {
    {{"ls", "List files, needs target directory",
      [](int argc, char **argv) {
        if (argc != 1)
          return 1;
        auto s = g_sd->begin();
        return (int)!g_fat.ls(*argv, LS_DATE | LS_SIZE);
      }},
     {"cat", "Read file to serial. needs target file, first 128 bytes",
      [](int argc, char **argv) {
        if (argc != 1)
          return 1;
        auto s = g_sd->begin();
        auto f = g_fat.open(argv[0]);
        if (!f.isOpen())
          return 2;
        char buf[129] = {};
        if (!f.read(&buf, sizeof(buf) - 1))
          return 3;
        log("COMMAND> cat: Content of %s:", argv[0]);
        Serial.println(buf);
        f.close();
        return 0;
      }}},
    {{"yay", "YAY YIPEE",
      [](int argc, char **argv) {
        logs("YAY");
        return 0;
      }},
     {}}};

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

void do_setup() {
  // Begin serial
  Serial.begin(115200);
  logs("=====================================");
  logs("Hi! Mega2560 SD-Card flasher starting up! :)");
  {
    SDInit sdinit;
    auto s = sdinit.begin();
    sdinit.init();
  }
  logs("Init SD done");

  // initrtc();

  static Flash flash;
  g_flash = &flash;
  static SD sd;
  g_sd = &sd;

  {
    auto session = g_sd->begin();
    if (!g_fat.begin(g_sd, true, 0, 0)) {
      logs("Failed to open FAT-FS :(");
      return;
    }
    log("FAT%d-FS recognized", g_fat.fatType());
    uint8_t buf[512];

    // Vol name: read from FAT LBA0
    g_sd->readSector(0, buf);
    char label[12];
    memcpy(label, buf + 71, 11);
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

  {
    g_flash->transact(Flash::Cmd::READ_JEDEC_ID);
    g_flash->transact(Flash::Cmd::READ_STATUS);
  }
}

void handle_commands() {
  if (Serial.available() > 0) {
    char in_buffer[64] = {};

    size_t len = Serial.readBytesUntil('\n', in_buffer, sizeof(in_buffer) - 1);

    if (len == 0)
      return;

    // Remove CR if terminal sends "\r\n"
    if (in_buffer[len - 1] == '\r')
      in_buffer[len - 1] = '\0';
    else
      in_buffer[len] = '\0';

    // Arg policy: split by space. max additional args: 3. hence max 5 total.
    constexpr int kSplitMax = 5;
    char *split[kSplitMax];
    int found = 0;
    bool in_str = false;
    auto max = strlen(in_buffer);
    for (size_t idx = 0; idx < max; idx++) {
      vlog("STRSPLIT>: #%d/%u '%c' in_str=%d", idx, (unsigned int)max,
           in_buffer[idx], in_str);
      if (in_buffer[idx] == ' ') {
        if (in_str) {
          vlogs("STRSPLIT>: Clear in_str");
          in_str = false;
        }
        in_buffer[idx] = '\0';
        vlogs("STRSPLIT>: Continue");
        continue;
      }
      if (in_str) {
        vlogs("STRSPLIT>: in_str=true, skip");
        continue;
      }
      vlog("STRSPLIT>: appending string at idx %d", idx);
      split[found] = &in_buffer[idx];
      if (found > kSplitMax) {
        vlog("STRSPLIT>: Ending at four found=%d", found);
        break; // Overflow-ready
      }
      found++;
      in_str = true;
    }
    for (int i = 0; i < found; i++) {
      vlog("STRSPLIT PARSED>: #%d %s", i, split[i]);
    }
    if (found < 2) {
      Serial.println("Usage: <section> <subcommand> (<args>...)");
      return;
    }
    bool foundHandler = false;

    if (!strcmp(split[0], "sd")) {
      for (Command *cur = commands[Command::SUBSYS_SD]; cur->name != nullptr;
           cur++) {
        if (!strcmp(cur->name, split[1])) {
          int argc = found - 2;
          char **argv = &split[2];
          int rc = cur->function(argc, argv);
          if (rc != 0)
            log("COMMAND> warn: Command %s return nonzero %d", cur->name, rc);
          foundHandler = true;
          break;
        }
      }
      if (!foundHandler)
        log("COMMAND> No such command %s for '%s'", split[1], split[0]);
    } else if (!strcmp(split[0], "flash")) {
      for (Command *cur = commands[Command::SUBSYS_FLASH]; cur->name != nullptr;
           cur++) {
        if (!strcmp(cur->name, split[1])) {
          int argc = found - 2;
          char **argv = &split[2];
          int rc = cur->function(argc, argv);
          if (rc != 0)
            log("COMMAND> warn: Command %s return nonzero %d", cur->name, rc);
          foundHandler = true;
          break;
        }
      }
      if (!foundHandler)
        log("COMMAND> No such command %s for '%s'", split[1], split[0]);
    } else if (in_buffer[0] == '?') {
      Serial.println("Available commands: NO");
    }
  }
}

void do_loop() {
  handle_commands();

  return;
  // put your main code here, to run repeatedly:

  g_flash->transact(Flash::Cmd::WRITE_ENABLE);
  Flash::ReadData rdata{};
  rdata.bytes_count = 10;
  uint8_t d[23] = {};
  rdata.data = d;
  g_flash->transact(Flash::Cmd::READ_ADDRESS_DATA, &rdata);
}
