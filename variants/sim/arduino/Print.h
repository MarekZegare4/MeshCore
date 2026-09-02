#pragma once

// Minimal native stand-in for Arduino's Print class -- just enough of the
// real API surface for MeshCore's app logic (which never uses Arduino
// String) to link: byte/buffer write(), the numeric print() overloads, and
// printf() (a genuine Arduino Print extension on ESP32/etc, used sparingly
// by MyMesh.cpp's CLI-rescue debug path).
//
// Modelled on test/mocks/Stream.h's Print (same shape, same DEC/HEX/OCT/BIN
// constants) but adds printf() since the real app needs it.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
public:
  virtual ~Print() = default;

  virtual size_t write(uint8_t b) { return 1; }

  size_t write(const char *str) {
    if (str == NULL) return 0;
    return write((const uint8_t *)str, strlen(str));
  }

  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t t = 0;
    for (size_t i = 0; i < size; i++) t += write(buffer[i]);
    return t;
  }

  size_t write(const char *buffer, size_t size) {
    return write((const uint8_t *)buffer, size);
  }

  virtual size_t print(unsigned char b, int base = DEC) { return printInt((unsigned long)b, base); }
  virtual size_t print(int v, int base = DEC)            { return base == DEC ? printSigned((long)v) : printInt((unsigned long)v, base); }
  virtual size_t print(unsigned int v, int base = DEC)   { return printInt((unsigned long)v, base); }
  virtual size_t print(long v, int base = DEC)           { return base == DEC ? printSigned(v) : printInt((unsigned long)v, base); }
  virtual size_t print(unsigned long v, int base = DEC)  { return printInt(v, base); }
  virtual size_t print(long long v, int base = DEC)      { char buf[32]; snprintf(buf, sizeof(buf), "%lld", v); return write(buf); }
  virtual size_t print(unsigned long long v, int base = DEC) { char buf[32]; snprintf(buf, sizeof(buf), "%llu", v); return write(buf); }
  virtual size_t print(double v, int digits = 2)         { char buf[64]; snprintf(buf, sizeof(buf), "%.*f", digits, v); return write(buf); }

  size_t print(char c)        { return write((uint8_t)c); }
  size_t print(const char* s) { return write(s); }

  size_t println()              { return write("\r\n"); }
  size_t println(const char* s) { size_t n = print(s); n += println(); return n; }
  size_t println(char c)        { size_t n = print(c); n += println(); return n; }
  size_t println(int v, int base = DEC)          { size_t n = print(v, base); n += println(); return n; }
  size_t println(unsigned int v, int base = DEC) { size_t n = print(v, base); n += println(); return n; }
  size_t println(long v, int base = DEC)         { size_t n = print(v, base); n += println(); return n; }
  size_t println(unsigned long v, int base = DEC){ size_t n = print(v, base); n += println(); return n; }
  size_t println(double v, int digits = 2)       { size_t n = print(v, digits); n += println(); return n; }

  // Real Arduino cores (ESP32/NRF52) expose Print::printf(); used by
  // MyMesh.cpp's CLI-rescue debug command handler.
  size_t printf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(buf)) return write(buf);
    // truncated -- still write what fit
    return write(buf);
  }

  virtual void flush() { }

private:
  size_t printInt(unsigned long v, int base) {
    char buf[34];
    if (base == DEC) { snprintf(buf, sizeof(buf), "%lu", v); }
    else if (base == HEX) { snprintf(buf, sizeof(buf), "%lx", v); }
    else if (base == OCT) { snprintf(buf, sizeof(buf), "%lo", v); }
    else {
      // generic base conversion (BIN etc.)
      char tmp[34]; int i = 0;
      unsigned long n = v;
      if (n == 0) tmp[i++] = '0';
      while (n > 0) { tmp[i++] = "0123456789abcdefghijklmnopqrstuvwxyz"[n % base]; n /= base; }
      int j = 0;
      while (i > 0) buf[j++] = tmp[--i];
      buf[j] = 0;
    }
    return write(buf);
  }
  size_t printSigned(long v) {
    char buf[34];
    snprintf(buf, sizeof(buf), "%ld", v);
    return write(buf);
  }
};
