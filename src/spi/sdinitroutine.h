#include "core.h"

class SDInitRoutine : public SPIWrap<SPIOwner::SdInit, CS_NONE, true, 400000, MSBFIRST, SPI_MODE0>{
  public:
  SDInitRoutine();
  void run();
};
