#pragma once
// Minimal native stand-in for Adafruit's RTClib.h. Real hardware boards pull
// this in transitively for helpers/AutoDiscoverRTCClock.h (not used by the
// sim build -- SimRTCClock.h reads the host wall clock directly) and for
// DateTime, which src/helpers/CommonCLI.cpp's "clock"/"clock sync"/"time"
// CLI commands genuinely construct and call hour()/minute()/day()/month()/
// year() on. Nothing in companion_radio (root files or ui-new/) uses
// DateTime, but CommonCLI.cpp (built for every board, including the sim) does.
//
// Implemented via the real C library's gmtime_r() (UTC calendar breakdown)
// rather than hand-rolling RTClib's own unixtime<->y/m/d/h/m/s conversion --
// same result, zero risk of a transcription bug in that math.
#include <ctime>
#include <cstdint>

class DateTime {
  uint32_t _unixtime;
public:
  DateTime(uint32_t t = 0) : _unixtime(t) { }

  uint16_t year() const   { return 1900 + tmFields().tm_year; }
  uint8_t  month() const  { return (uint8_t)(tmFields().tm_mon + 1); }
  uint8_t  day() const    { return (uint8_t)tmFields().tm_mday; }
  uint8_t  hour() const   { return (uint8_t)tmFields().tm_hour; }
  uint8_t  minute() const { return (uint8_t)tmFields().tm_min; }
  uint8_t  second() const { return (uint8_t)tmFields().tm_sec; }
  uint32_t unixtime() const { return _unixtime; }

private:
  struct tm tmFields() const {
    time_t t = (time_t)_unixtime;
    struct tm out;
    gmtime_r(&t, &out);
    return out;
  }
};
