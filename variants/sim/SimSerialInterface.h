#pragma once

#include <helpers/BaseSerialInterface.h>

// BaseSerialInterface stub for the native sim build: no companion app can
// connect over BLE/USB in Phase 1 (there's no real transport), so this
// always reports "not connected, nothing to do". Enough to let MyMesh's
// startInterface()/loop() run unmodified.
class SimSerialInterface : public BaseSerialInterface {
public:
  void begin() { }

  void enable() override { }
  void disable() override { }
  bool isEnabled() const override { return false; }
  bool isConnected() const override { return false; }
  bool isWriteBusy() const override { return false; }
  size_t writeFrame(const uint8_t src[], size_t len) override { return 0; }
  size_t checkRecvFrame(uint8_t dest[]) override { return 0; }
};
