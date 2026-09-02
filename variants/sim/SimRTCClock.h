#pragma once

#include <MeshCore.h>
#include <ctime>

// mesh::RTCClock backed by the real host wall clock (time(nullptr)), with
// setCurrentTime() applying an offset so DataStore::restoreRTCTime()
// (persisted last-known time) and the CLI's `time` command still work.
class SimRTCClock : public mesh::RTCClock {
  long _offset = 0;   // added to real wall-clock time()
public:
  uint32_t getCurrentTime() override {
    return (uint32_t)((long)time(NULL) + _offset);
  }
  void setCurrentTime(uint32_t t) override {
    _offset = (long)t - (long)time(NULL);
  }
};
