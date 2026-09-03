#pragma once

// Minimal native Arduino-core shim for the SIM_PLATFORM (variants/sim)
// native build. Scoped empirically (see the Phase-1 status report) by
// grepping examples/companion_radio + ui-new + the src/helpers files the
// sim build actually compiles for real Arduino API usage -- the app logic
// turned out to use almost none of it: no Arduino String anywhere, no
// PROGMEM/pgm_read, no digitalWrite/pinMode/analogRead/Wire/SPI reachable
// (all gated behind board-specific PIN_* defines this build never sets),
// and Serial. only in main.cpp's Serial.begin() plus MyMesh.cpp's
// CLI-rescue debug command handler.
//
// Deliberately NOT defining the ARDUINO preprocessor macro: a couple of
// vendored third-party libs (CayenneLPP.cpp, ArduinoJson) branch on
// `#ifdef ARDUINO` to choose between Arduino String/Stream and plain
// std::string/std::ostream -- leaving ARDUINO undefined routes them onto
// their portable std:: path, which is exactly what we want and needs no
// Arduino String implementation at all.

#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <thread>
#include <algorithm>
#include <type_traits>

#include "Stream.h"

using std::isnan;
using std::isinf;

// --- timing -----------------------------------------------------------

inline uint32_t _sim_millis_start_epoch_ms() {
  static const uint32_t t0 = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  return t0;
}

inline unsigned long millis() {
  uint32_t now = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  return (unsigned long)(now - _sim_millis_start_epoch_ms());
}

inline unsigned long micros() {
  uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  return (unsigned long)now;
}

// Real delay(); MyMesh.cpp:2848 and UITask.cpp:2619's one-shot pre-reboot/
// pre-shutdown pauses get their own SIM_PLATFORM branch that skips calling
// this entirely (see the Phase-1 status report), so an actual sleep here
// never blocks the sim's stdin-reader thread for long.
inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// --- randomSeed()/random() (Arduino's global RNG, used by StdRNG in
//     src/helpers/ArduinoHelpers.h -- NOT the same thing as mesh::RNG) ----
inline void randomSeed(unsigned long seed) { if (seed != 0) ::srand((unsigned)seed); }
inline long random(long howbig) {
  if (howbig <= 0) return 0;
  return (long)(::rand() % howbig);
}
inline long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  return howsmall + (long)(::rand() % (howbig - howsmall));
}

// --- misc Arduino macros/helpers ---------------------------------------

#ifndef PROGMEM
#define PROGMEM
#endif
// Real pgm_read_*() on a native host is just a plain dereference -- there's
// no separate flash address space to special-case. Needed once Adafruit_GFX
// + MiscFixedFont.h (variants/sim/thirdparty/gfx/, src/helpers/ui/) entered
// the sim build; nothing before that read PROGMEM data at all (see this
// file's own header comment).
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#endif
#ifndef F
#define F(x) (x)
#endif
#ifndef PSTR
#define PSTR(x) (x)
#endif

// Real Arduino cores define min()/max()/constrain() as untyped macros, which
// works but is notorious for silently corrupting any <algorithm>/<vector>/etc
// internals that use those same names if such a header is ever included
// afterwards (this codebase's CayenneLPPPolyline.h uses std::vector/std::map,
// and SimFS.h/etc use std::string) -- so these are real (mixed-argument-type)
// templates instead, deducing a common value type via std::common_type
// rather than decltype(a<b?a:b) (which can deduce a *reference* type when
// both arguments are same-typed lvalues, and then fail to bind that
// reference to the temporary a cast/mixed-type branch produces).
#ifndef min
template <typename T, typename U>
typename std::common_type<T, U>::type min(T a, U b) { return a < b ? a : b; }
#endif
#ifndef max
template <typename T, typename U>
typename std::common_type<T, U>::type max(T a, U b) { return a > b ? a : b; }
#endif
#ifndef constrain
template <typename T, typename U, typename V>
typename std::common_type<T, U, V>::type constrain(T amt, U lo, V hi) {
  return amt < lo ? lo : (amt > hi ? hi : amt);
}
#endif
#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif

// Arduino global itoa/ltoa/utoa/ultoa (AVR-libc-style, not in standard C++);
// src/helpers/TxtDataHelpers.cpp's float-formatting code calls ltoa()
// directly. glibc/libc++ don't provide these, so implement via snprintf's
// base-10/16/8/2 support for 10, else a manual digit loop for other bases.
inline char* ltoa(long value, char* result, int base) {
  if (base == 10) { snprintf(result, 32, "%ld", value); return result; }
  if (value == 0) { result[0] = '0'; result[1] = 0; return result; }
  bool neg = value < 0;
  unsigned long v = neg ? (unsigned long)(-value) : (unsigned long)value;
  char tmp[34]; int i = 0;
  while (v > 0) { int d = v % base; tmp[i++] = d < 10 ? ('0' + d) : ('a' + d - 10); v /= base; }
  int j = 0;
  if (neg) result[j++] = '-';
  while (i > 0) result[j++] = tmp[--i];
  result[j] = 0;
  return result;
}
inline char* itoa(int value, char* result, int base) { return ltoa((long)value, result, base); }

// Cooperative-multitasking yield point (ESP32/RP2040 cores feed this to their
// scheduler/watchdog between blocking waits; a plain native process has none
// of that, so it's a no-op). src/helpers/StreamUtils.h calls it directly.
inline void yield() { }

// Opaque incomplete type used only for PROGMEM-string pointer typing on real
// Arduino cores (F("...") normally returns a `const __FlashStringHelper*`).
// Since F(x) is defined as a plain passthrough above (no real PROGMEM on a
// native build), nothing ever actually constructs one of these -- it only
// needs to exist as a type so a `print(const __FlashStringHelper*)` overload
// (e.g. examples/companion_radio/ui-new/TrailScreen.h's BoundedSerialPrint)
// declares cleanly, exactly like on a real board.
class __FlashStringHelper;

// Adafruit_GFX.h declares a getTextBounds(const String&, ...) overload
// (variants/sim/thirdparty/gfx/) -- MeshCore's own app code never
// constructs or passes a real Arduino String anywhere (this file's own
// header comment), so this exists purely to satisfy that one declaration's
// compile, not to be a real String replacement.
#include <string>
class String {
  std::string _s;
public:
  String(const char* s = "") : _s(s ? s : "") {}
  const char* c_str() const { return _s.c_str(); }
  size_t length() const { return _s.length(); }
};

// Real Arduino cores define these; Adafruit_GFX.cpp's drawArc() uses
// radians(). Same DEG_TO_RAD/RAD_TO_DEG constants as the real macros.
#ifndef radians
#define radians(deg) ((deg) * 0.017453292519943295)
#endif
#ifndef degrees
#define degrees(rad) ((rad) * 57.29577951308232)
#endif

// --- Serial -------------------------------------------------------------
// Only ever used for Serial.begin() (main.cpp, ignored) and MyMesh.cpp's
// CLI-rescue debug command handler (Serial.print/println/printf as output,
// Serial.available()/read() as input). Output goes to real stdout; input
// always reports "nothing available" -- CLI-rescue isn't reachable through
// the sim's stdin-driven UI input path (see variants/sim/README) and isn't
// one of the Phase-1 exit criteria, but every call still needs to compile.
class SimSerialClass : public Stream {
public:
  void begin(unsigned long baud) { }
  // Real HardwareSerial's operator bool() reports "is a USB/BLE host
  // actually connected" (some real boards genuinely gate on this, e.g.
  // TrailScreen.h's GPX-export-over-serial feature). stdout is always
  // "there" for a native process, so just always report ready.
  explicit operator bool() const { return true; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t b) override { putchar(b); return 1; }
  size_t write(const uint8_t *buffer, size_t size) override {
    fwrite(buffer, 1, size, stdout);
    return size;
  }
  void flush() override { fflush(stdout); }
};

extern SimSerialClass Serial;
