#include <Arduino.h>
#include "target.h"
#include "SimRNG.h"

// Global stdout-backed Serial object, declared extern in Arduino.h.
SimSerialClass Serial;

SimMainBoard board;
SimRadio radio_driver;
SimRTCClock rtc_clock;
SensorManager sensors;   // base class: no real sensors in Phase 1

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
#endif

bool radio_init() {
  // No real radio hardware to initialise -- always succeeds (see
  // variants/sim/SimRadio.h; Phase 3 of the sim plan is where two SimRadio
  // instances actually exchange bytes through a shared in-memory "ether").
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  static SimRNG rng;
  rng.begin();
  return mesh::LocalIdentity(&rng);
}
