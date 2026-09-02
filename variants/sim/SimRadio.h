#pragma once

#include <Dispatcher.h>
#include <ctime>
#include <cstdlib>

// mesh::Radio implementation for the native sim build (Phase 1). Mirrors
// the FakeRadio in test/test_kiss_modem/test_tx_backpressure.cpp in spirit
// (always-succeed send, no real RF) but is written directly against the
// REAL mesh::Radio interface in src/Dispatcher.h -- that test mock is for a
// different, out-of-date mocked Mesh.h (see the Phase-1 plan) and must not
// be copied.
//
// Phase 1 has exactly one logical device, so there is nothing to actually
// exchange packets with: recvRaw() always reports "nothing received",
// startSendRaw()/isSendComplete() always report success instantly. Phase 3
// of the sim plan (two simulated devices + a repeater) is where this class
// grows a real in-memory "ether" so two instances can actually talk.
class SimRadio : public mesh::Radio {
  uint32_t n_recv = 0, n_sent = 0, n_recv_errors = 0;
  bool _power_save = false;
  bool _rx_boosted_gain = false;
  int8_t _tx_dbm = 0;

public:
  void begin() override { }

  int recvRaw(uint8_t* bytes, int sz) override {
    return 0;   // never any incoming data yet (Phase 3: real ether)
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    // Rough LoRa-ish estimate so anything that logs/uses airtime for
    // scheduling doesn't see nonsense; not calibrated to any real profile.
    return (uint32_t)(len_bytes * 3 + 50);
  }

  float packetScore(float snr, int packet_len) override {
    return 100.0f;  // pretend every packet we'd send is a clean, high-quality one
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    n_sent++;
    return true;    // instantly "succeeds" -- nothing is actually transmitted yet
  }

  bool isSendComplete() override { return true; }
  void onSendFinished() override { }

  bool isInRecvMode() const override { return true; }

  // --- Extra methods below (not part of mesh::Radio) -------------------
  // MyMesh.cpp/DataStore.cpp/the Settings/Diagnostics UI screens call these
  // directly on the concrete radio_driver object on every real board, the
  // same way they'd call them on a RadioLibWrapper subclass (see
  // src/helpers/radiolib/RadioLibWrappers.h, which every one of these
  // mirrors). No real chip underneath, so these just report plausible
  // static/no-op values.

  uint32_t getRngSeed() {
    return (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)this ^ (uint32_t)rand();
  }

  void getFreqBounds(float& min_mhz, float& max_mhz) const {
    min_mhz = 150.0f;
    max_mhz = 2500.0f;
  }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) { }
  void powerOff() { }

  void setPowerSaving(bool en) { _power_save = en; }
  bool getPowerSaving() const { return _power_save; }

  void setTxPower(int8_t dbm) { _tx_dbm = dbm; }
  int8_t getTxPower() const { return _tx_dbm; }

  bool setRxBoostedGainMode(bool en) { _rx_boosted_gain = en; return true; }
  bool getRxBoostedGainMode() const { return _rx_boosted_gain; }

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  uint32_t getRxPsWatchdogSoftCount() const { return 0; }
  uint32_t getRxPsWatchdogHardCount() const { return 0; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }

  static float snrFloorForSF(uint8_t sf) {
    if (sf < 7) sf = 7; else if (sf > 12) sf = 12;
    return -7.5f - 2.5f * (float)(sf - 7);
  }
};
