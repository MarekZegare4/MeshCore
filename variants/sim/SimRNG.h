#pragma once

#include <Utils.h>
#include <cstdlib>
#include <ctime>
#include "SimInstance.h"

// mesh::RNG implementation for the native sim build. Not cryptographically
// strong (rand() under the hood) -- fine for a terminal demo. Phase 3 mixes
// in sim_instance_salt() so that two module instances of the SAME compiled
// binary (the browser's two companion_radio instances) can't end up with
// the same seed -- see SimInstance.h for why that's a real risk here, not
// a hypothetical one.
class SimRNG : public mesh::RNG {
public:
  SimRNG() { }
  void begin() {
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)this ^ sim_instance_salt();
    srand(seed);
  }
  void random(uint8_t* dest, size_t sz) override {
    for (size_t i = 0; i < sz; i++) dest[i] = (uint8_t)rand();
  }
};
