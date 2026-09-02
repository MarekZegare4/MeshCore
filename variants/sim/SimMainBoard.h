#pragma once

#include <MeshCore.h>
#include <cstdio>
#include <cstdlib>

// mesh::MainBoard implementation for the native sim build -- no real
// hardware, so battery/manufacturer/reboot are all just plausible fakes.
class SimMainBoard : public mesh::MainBoard {
public:
  void begin() { }

  uint16_t getBattMilliVolts() override { return 4000; }  // pretend full battery
  const char* getManufacturerName() const override { return "MeshCore Sim (native)"; }

  void reboot() override {
    printf("\n[sim] reboot() requested -- exiting process (rerun the binary to simulate a reboot)\n");
    fflush(stdout);
    exit(0);
  }

  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }

  void onBootComplete() override { }
  void sleep(uint32_t secs) override { }   // no-op: native process never actually sleeps the CPU
};
