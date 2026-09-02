#pragma once

// Minimal native stand-in for Arduino's Stream class. src/Utils.h includes
// <Stream.h> directly (mesh::Utils::printHex(Stream&, ...)), and
// src/Identity.cpp's readFrom/writeTo/printTo take a Stream& -- both real
// hardware File objects (fs::File, Adafruit_LittleFS's File) and our own
// SimFile (variants/sim/SimFS.h) derive from this, exactly like on real
// boards.

#include "Print.h"

class Stream : public Print {
public:
  virtual ~Stream() = default;

  virtual int available() { return 0; }
  virtual int availableForWrite() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }

  virtual size_t readBytes(char *buffer, size_t length) {
    size_t i = 0;
    while (i < length) {
      int c = read();
      if (c < 0) break;
      buffer[i++] = (char)c;
    }
    return i;
  }
  size_t readBytes(uint8_t *buffer, size_t length) {
    return readBytes((char *)buffer, length);
  }
};
