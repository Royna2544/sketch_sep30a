#include "led/led.h"
#include "spi/flashdev.h"
#include "spi/sd.h"
#include <SdFat.h>

extern Flash *g_flash;
extern SD *g_sd;
extern FatVolume g_fat;

struct Command {
  const char *name;
  const char *help;
  int (*function)(
      int argc,
      char **argv); // return 0 for no errors, >0 for command specific errors.

  constexpr static int SUBSYS_SD = 0;
  constexpr static int SUBSYS_FLASH = 1;
} commands[2][10] = {
    {{"ls", "List files, needs target directory",
      [](int argc, char **argv) {
        if (argc != 1)
          return 1;
        auto s = g_sd->begin();
        return (int)!g_fat.ls(*argv, LS_DATE | LS_SIZE);
      }},
     {"cat", "Read file to serial. needs target file,128 bytes",
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
     {"flash", "Flash file name in sdcard argv[0] to flash",
      [](int argc, char **argv) {
        if (argc != 1)
          return 1;
        auto s = g_flash->begin();
        if (!s.success) {
          log("COMMAND> flash: begin failed");
          return 2;
        }
        auto file = g_fat.open(argv[0]);
        if (!file.isOpen()) {
          log("COMMAND> flash: open failed");
          return 3;
        }
        log("COMMAND> flash: file size: %d", file.fileSize());
        uint64_t pages = file.fileSize() / Flash::PAGE_SIZE;
        auto res = g_flash->transact(Flash::Cmd::PROGRAM_PAGE);
        log("COMMAND> flash: done");
        return 0;
      }},
     {}}};

static bool findAndRun(Command *array, const char *commandName, int argc,
                       char **argv) {
  for (Command *cur = array; cur->name != nullptr; cur++) {
    if (!strcmp(cur->name, commandName)) {
      log("COMMAND> Running command %s with %d arguments", cur->name, argc);
      int rc = cur->function(argc, argv);
      if (rc != 0)
        log("COMMAND> warn: Command %s return nonzero %d", cur->name, rc);
      return true;
    }
  }
  return false;
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
      if (found >= kSplitMax) {
        vlog("STRSPLIT>: Ending at four found=%d", found);
        break; // Overflow-ready
      }
      found++;
      in_str = true;
    }
    for (int i = 0; i < found; i++) {
      vlog("STRSPLIT PARSED>: #%d %s", i, split[i]);
    }

    if (found == 1 && split[0][0] == '?') {
      Serial.println("Available commands:");
      for (Command *cur = commands[Command::SUBSYS_SD]; cur->name != nullptr;
           cur++) {
        log("sd:  %s: %s", cur->name, cur->help);
      }
      for (Command *cur = commands[Command::SUBSYS_FLASH]; cur->name != nullptr;
           cur++) {
        log("flash:  %s: %s", cur->name, cur->help);
      }
      return;
    }

    if (found < 2) {
      Serial.println("Usage: <section> <subcommand> (<args>...)");
      return;
    }
    Command *subsys = nullptr;

    if (!strcmp(split[0], "sd")) {
      subsys = commands[Command::SUBSYS_SD];
    } else if (!strcmp(split[0], "flash")) {
      subsys = commands[Command::SUBSYS_FLASH];
    } else if (!strcmp(split[0], "led")) {
      if (found != 2) {
        Serial.println("Usage: led <subcommand> (<args>...)");
        return;
      }
      RGBLED led(5, 6, 7);
      if (!strcmp(split[1], "rainbow")) {
        led.playEffect(&led.effects[0]);
      } else if (!strcmp(split[1], "sunset")) {
        led.playEffect(&led.effects[2]);
      } else if (!strcmp(split[1], "ocean")) {
        led.playEffect(&led.effects[3]);
      } else if (!strcmp(split[1], "neon")) {
        led.playEffect(&led.effects[4]);
      } else if (!strcmp(split[1], "off")) {
        led.setColor(0, 0, 0);
      } else if (!strcmp(split[1], "i_want_full_led_mode")) {
        Serial.println(
            "Fine, but this will loop forever. Press reset to exit.");
        // TODO: Use millis() instead of delay() to avoid
        // blocking the main loop.
        while (true) {
          led.playEffect(&led.effects[0]);
          delay(100);
          led.playEffect(&led.effects[1]);
          delay(100);
          led.playEffect(&led.effects[2]);
          delay(100);
          led.playEffect(&led.effects[3]);
          delay(100);
          led.playEffect(&led.effects[4]);
          delay(100);
        }
      } else {
        Serial.println("Unknown LED effect");
      }
      return;
    } else {
      Serial.println("Unknown subsystem");
      return;
    }

    if (!findAndRun(subsys, split[1], found - 2, &split[2])) {
      Serial.println("Unknown subsystem");
      return;
    }
  }
}
