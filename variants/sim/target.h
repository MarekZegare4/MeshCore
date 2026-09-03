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
// SimSensorManager.h before SimLocationProvider.h, deliberately: the
// `sensors` global's real type is SimSensorManager (defined in
// target.cpp), and SimLocationProvider.h's own `extern SimSensorManager
// sensors;` declaration (see that file's comment) needs the complete type
// already visible -- C++ requires every declaration of the same global to
// agree on its exact type, not just something covariant/compatible.
#include "SimSensorManager.h"
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
extern SimSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
