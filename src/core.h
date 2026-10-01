#include <SdFat.h>
#include <SdFatConfig.h>
#include <SdFatDebugConfig.h>
#include <stdint.h>
#include <sdios.h>

namespace {
  #include "helpers.h"
}
#include "spi/flashdev.h"
#include "spi/sd.h"

// TODO: Random 22 unused pin to match interface, find a better way later.
class SDInit : public SPIWrap<SPIOwner::SdInit, 22, true, 400000, MSBFIRST, SPI_MODE0>{
  public:
  SDInit() : SPIWrap("SDInit"){
  }
  void init() {
    log("Init SDHC");
    bool ok = true;
    // Provide >=74 clocks, send 10 bytes.
    for (int x = 0; x < 10; x++)
      ok &= transfer().res;
    ASSERT_TRUE(ok);
  }
};
