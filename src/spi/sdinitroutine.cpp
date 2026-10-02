#include "sdinitroutine.h"

SDInitRoutine::SDInitRoutine() : SPIWrap("SDInit") {}

void SDInitRoutine::run() {
  log("Send 80 clocks for SD card");
  bool ok = true;
  // Provide >=74 clocks, send 10 bytes.
  for (int x = 0; x < 10; x++) {
    if (x % 3 == 0) {
      log("Tick... #%d", x);
    } else if (x % 3 == 2) {
      log("Tok... #%d", x);
    }
    ok &= transfer().res;
  }
  ASSERT_TRUE(ok);
  log("SD card init routine complete, sent 80 clocks");
}
