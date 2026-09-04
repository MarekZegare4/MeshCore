#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Repeater CLI"
  #include <helpers/nrf52/EthernetCLI.h>
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
#include <emscripten.h>
// Phase 3 sim ether: a JS-visible "did this repeater just relay a packet"
// hook. MyMesh (src/Mesh.h's Mesh base class) already keeps an exact count
// of packets actually re-transmitted in the repeater/transport role --
// n_forwarded, incremented at the real ACTION_RETRANSMIT decision points in
// src/Mesh.cpp (routeRecvPacket()) and decremented in onRetransmitCancelled()
// if an overhear cancels a queued retransmit before it goes out -- and
// already exposes it publicly as getNumForwarded() (the same number
// DiagnosticsScreen.h prints on a real companion_radio's screen). Rather
// than re-deriving "was this a relay" from TX/RX byte timing (fragile,
// and duplicates logic the real Mesh class already gets right, including
// the overhear-cancellation edge case), this just exports that exact
// counter for JS to poll and diff on its own ether tick -- zero changes
// to any shared/platform-agnostic src/ file.
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t sim_repeater_get_relay_count() {
  return the_mesh.getNumForwarded();
}

// Same "has setup() actually finished" readiness flag as
// examples/companion_radio/main.cpp -- see that file's comment for why
// this is needed instead of a fixed post-ready delay.
static bool g_sim_ready = false;
extern "C" EMSCRIPTEN_KEEPALIVE int sim_is_ready() {
  return g_sim_ready ? 1 : 0;
}

// Same idea as companion_radio's sim_test_advert_flood() -- MyMesh's own
// updateAdvertTimer() (called once from begin()) doesn't fire the repeater's
// first self-advert for a full 6 minutes (advert_interval defaults to 3,
// scaled *2*60*1000ms -- see MyMesh::updateAdvertTimer()/begin() in this
// same MyMesh.cpp), so a host page that wants hero/B to discover the
// repeater as a contact immediately on boot (rather than waiting out that
// timer) needs some way to trigger it early. sendSelfAdvertisement(0, true)
// is the exact same call updateAdvertTimer()'s scheduled path eventually
// makes, just invoked now instead of on a timer -- real crypto, real
// packet, nothing faked.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_advert_flood() {
  if (!g_sim_ready) return 0;
  the_mesh.sendSelfAdvertisement(0, true);
  return 1;
}
#endif

static char command[160];
#ifdef ETHERNET_ENABLED
static char ethernet_command[160];
#endif

// For power saving
unsigned long POWERSAVING_FIRSTSLEEP_SECS = 120; // The first sleep (if enabled) from boot

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
static unsigned long userBtnDownAt = 0;
#define USER_BTN_HOLD_OFF_MILLIS 1500
#endif

void setup() {
  Serial.begin(115200);
#ifndef SIM_PLATFORM
  // Skip this one-shot boot pause in the sim: under Emscripten it would
  // synchronously block the browser's single JS thread for a full second
  // (Arduino.h's sim delay() is a real std::this_thread::sleep_for(), and
  // this runs before emscripten_set_main_loop ever hands control back) --
  // harmless on a real board's own thread, unnecessary UX friction here.
  delay(1000);
#endif

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // give some extra time for serial to settle so
  // boot debug messages can be seen on terminal
  delay(5000);
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(SIM_PLATFORM)
  // Real files under ./sim_data_repeater/ (relative to the process's cwd,
  // native; or the Emscripten virtual FS, wasm) -- deliberately a DIFFERENT
  // root than examples/companion_radio/main.cpp's "./sim_data" so a
  // repeater instance's identity can never collide with a companion
  // instance's, even if both happened to run from the same cwd (native) or
  // the same page (wasm, see variants/sim/sim_main.cpp's SIM_FS_ROOT).
  // "static" (not a plain local) so sim_fs outlives setup() -- IdentityStore
  // only stores a pointer to it, same reasoning as the ui_task static above.
  static SimFS sim_fs("./sim_data_repeater");
  fs = &sim_fs;
  IdentityStore store(sim_fs, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  g_sim_ready = true;
#endif
}

void loop() {
  // Handle Serial CLI
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;
#ifdef ETHERNET_ENABLED
    if (!ethernet_handle_command(command, reply)) {
      the_mesh.handleCommand(0, command, reply);
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    if (!ethernet_handle_command(ethernet_command, reply)) {
      the_mesh.handleCommand(0, ethernet_command, reply);
    }
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_) && !defined(DISPLAY_CLASS)
  // Hold the user button to power off the SenseCAP Solar repeater.
  int btnState = digitalRead(PIN_USER_BTN);
  if (btnState == LOW) {
    if (userBtnDownAt == 0) {
      userBtnDownAt = millis();
    } else if ((unsigned long)(millis() - userBtnDownAt) >= USER_BTN_HOLD_OFF_MILLIS) {
      Serial.println("Powering off...");
      board.powerOff();  // does not return
    }
  } else {
    userBtnDownAt = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#else
    if (the_mesh.millisHasNowPassed(POWERSAVING_FIRSTSLEEP_SECS * 1000)) { // To check if it is time to sleep
      board.sleep(30); // Sleep. Wake up after a while or when receiving a LoRa packet
    }
#endif
  }
}
