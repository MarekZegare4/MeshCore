// Minimal native stand-in for rweather/Crypto's own RNGClass (RNG.cpp /
// NoiseSource.cpp are Arduino/AVR/ESP-hardware-entropy-only -- EEPROM/NVS
// seed persistence, TRNG registers, etc. -- and were deliberately NOT vendored
// here). Ed25519::generatePrivateKey() and Curve25519::dh1() reference the
// global `RNG` object even though MeshCore itself never calls either
// (real keypair generation goes through lib/ed25519 + mesh::RNG /
// SimRNG, see variants/sim/SimRNG.h) -- the symbol still has to resolve
// because it's referenced inside Ed25519.cpp/Curve25519.cpp regardless of
// which functions actually get called at runtime.
#include <RNG.h>
#include <cstdlib>

RNGClass::RNGClass() { }
RNGClass::~RNGClass() { }

void RNGClass::begin(const char *tag) { }
void RNGClass::addNoiseSource(NoiseSource &source) { }
void RNGClass::setAutoSaveTime(uint16_t minutes) { }

void RNGClass::rand(uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) data[i] = (uint8_t)::rand();
}

bool RNGClass::available(size_t len) const { return true; }
void RNGClass::stir(const uint8_t *data, size_t len, unsigned int credit) { }
void RNGClass::save() { }
void RNGClass::loop() { }
void RNGClass::destroy() { }
void RNGClass::rekey() { }
void RNGClass::mixTRNG() { }

RNGClass RNG;
