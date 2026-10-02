#include "core.h"

class SDInitRoutine : public SPIWrap<SPIOwner::SdInit, CS_NONE, true, KHZ(400L),
                                     MSBFIRST, SPI_MODE0> {
public:
  SDInitRoutine();
  void run();
};
