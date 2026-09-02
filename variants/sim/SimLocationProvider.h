#pragma once

#include <helpers/sensors/LocationProvider.h>

// LocationProvider stub for the native sim build: no real GPS, always
// reports "no fix". A settable lat/lon (JS-driven) is a Phase-2/3 concern.
class SimLocationProvider : public LocationProvider {
public:
  long getLatitude() override { return 0; }
  long getLongitude() override { return 0; }
  long getAltitude() override { return 0; }
  long satellitesCount() override { return 0; }
  bool isValid() override { return false; }
  long getTimestamp() override { return 0; }
  void reset() override { }
  void begin() override { }
  void stop() override { }
  void loop() override { }
  bool isEnabled() override { return false; }
};
