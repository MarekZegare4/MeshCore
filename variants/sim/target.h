#pragma once

// variants/sim -- native (host) "board" for running the real companion_radio
// app logic (MyMesh/UITask/DataStore) as a plain terminal program, with no
// real radio/display/BLE hardware. Follows the same target.h/target.cpp/
// platformio.ini triplet convention as every other board variant (see
// variants/generic-e22/ for the template this was modelled on).

#include <Mesh.h>
#include "SimRadio.h"
#include "SimMainBoard.h"
#include "SimRTCClock.h"
#include <helpers/SensorManager.h>
// Included here (rather than only where it's used) so its JS-facing
// sim_location_set() EMSCRIPTEN_KEEPALIVE export actually gets compiled
// into every SIM_PLATFORM target that includes target.h (companion_radio
// AND simple_repeater) -- an inline function nobody #includes never gets
// emitted at all, KEEPALIVE or not.
#include "SimLocationProvider.h"

#ifdef DISPLAY_CLASS
  #include "SimDisplayDriver.h"
#endif

extern SimMainBoard board;
extern SimRadio radio_driver;
extern SimRTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
