#pragma once

#include <Utils.h>
#include <cstdlib>
#include <ctime>

// mesh::RNG implementation for the native sim build. Not cryptographically
// strong (rand() under the hood) -- fine for a terminal demo; a real
// two-device-messaging phase (Phase 3 of the sim plan) may want to swap
// this for something seeded from the OS CSPRNG.
class SimRNG : public mesh::RNG {
public:
  SimRNG() { }
  void begin() {
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)this;
    srand(seed);
  }
  void random(uint8_t* dest, size_t sz) override {
    for (size_t i = 0; i < sz; i++) dest[i] = (uint8_t)rand();
  }
};
