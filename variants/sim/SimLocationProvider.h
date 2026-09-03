#pragma once

#include <helpers/sensors/LocationProvider.h>
#include <helpers/SensorManager.h>
#include <ctime>

// LocationProvider stub for the sim build. Phase 1/2: no real GPS, always
// reports "no fix" (default-constructed state below reproduces that
// exactly). Phase 3: JS-settable per instance via sim_location_set(),
// declared at the bottom of this file.
//
// Note on how this actually reaches the advertised location: companion_radio
// itself never calls a LocationProvider through this class -- it reads
// `sensors.node_lat`/`node_lon` directly (see e.g.
// examples/companion_radio/MyMesh.cpp's createSelfAdvert() call sites), and
// `sensors` (declared in target.h) is a plain base `SensorManager`, not a
// subclass wired to any LocationProvider -- there was never a real GPS
// object plugged in here to begin with (see SensorManager.h: "modify
// node_lat/node_lon directly, if you want to affect Advert location"). So
// sim_location_set() below writes BOTH this class's own state (satisfying
// the LocationProvider interface faithfully, in case future sim code wants
// a real one) AND `sensors.node_lat/node_lon` directly (the field
// companion_radio's advert path actually reads) -- belt and suspenders,
// same underlying values either way.
class SimLocationProvider : public LocationProvider {
  long _lat_e6 = 0, _lon_e6 = 0, _alt_mm = 0;
  bool _valid = false;

public:
  long getLatitude() override { return _lat_e6; }
  long getLongitude() override { return _lon_e6; }
  long getAltitude() override { return _alt_mm; }
  long satellitesCount() override { return _valid ? 8 : 0; }
  bool isValid() override { return _valid; }
  long getTimestamp() override { return _valid ? (long)time(NULL) : 0; }
  void reset() override { _valid = false; }
  void begin() override { }
  void stop() override { }
  void loop() override { }
  bool isEnabled() override { return _valid; }
  void sendSentence(const char* sentence) override { }

  void set(float lat_deg, float lon_deg, float alt_m = 0.0f) {
    _lat_e6 = (long)(lat_deg * 1000000.0f);
    _lon_e6 = (long)(lon_deg * 1000000.0f);
    _alt_mm = (long)(alt_m * 1000.0f);
    _valid = true;
  }
};

// Single instance, mirroring the pattern of every other Sim* global
// (board/radio_driver/rtc_clock/sensors) declared in target.h/target.cpp --
// this one isn't wired into target.h/.cpp itself since nothing on the
// SIM_PLATFORM boot path currently constructs a LocationProvider at all
// (see the class comment above), so it lives here instead as a
// self-contained, opt-in addition.
extern SensorManager sensors;   // defined in variants/sim/target.cpp
inline SimLocationProvider& sim_location_provider() {
  static SimLocationProvider instance;
  return instance;
}

inline void sim_location_apply(float lat_deg, float lon_deg, float alt_m = 0.0f) {
  sim_location_provider().set(lat_deg, lon_deg, alt_m);
  sensors.node_lat = lat_deg;
  sensors.node_lon = lon_deg;
  sensors.node_altitude = alt_m;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// C linkage (no default args, unlike sim_location_apply() above) so JS's
// ccall('sim_location_set', ...) can find it by its exact, unmangled name.
extern "C" inline EMSCRIPTEN_KEEPALIVE void sim_location_set(float lat_deg, float lon_deg, float alt_m) {
  sim_location_apply(lat_deg, lon_deg, alt_m);
}
#endif
