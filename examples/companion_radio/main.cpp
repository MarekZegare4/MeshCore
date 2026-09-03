#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
// Phase 3: true only once setup() has fully finished (set at the very end
// of setup(), below) -- lets a JS test harness poll "has this instance
// actually booted" instead of guessing a fixed delay after the MODULARIZE
// factory promise resolves, which resolves once the wasm module is
// instantiated, well before sim_idbfs_ready()'s async IDBFS callback ever
// invokes setup() (see variants/sim/sim_main.cpp). Calling any of the other
// sim_test_*() hooks below before this is true would run against a
// the_mesh that exists (global C++ construction already ran) but hasn't
// had begin()/an identity loaded yet.
static bool g_sim_ready = false;
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

// platform file system
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
    #if defined(EXTRAFS)
      #include <CustomLFS.h>
      CustomLFS ExtraFS(0xD4000, 0x19000, 128);
      DataStore store(InternalFS, ExtraFS, rtc_clock);
    #else
      DataStore store(InternalFS, rtc_clock);
    #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#elif defined(SIM_PLATFORM)
  #include <SimFS.h>
  // Real files under ./sim_data/ (relative to the process's cwd) so
  // NodePrefs/contacts/identity genuinely round-trip across process
  // restarts -- see SimFS.h.
  SimFS sim_fs("./sim_data");
  DataStore store(sim_fs, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(DUAL_SERIAL)
    #include <helpers/esp32/DualSerialInterface.h>
    DualSerialInterface serial_interface;
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef DUAL_SERIAL
    #include <helpers/nrf52/DualSerialInterface.h>
    DualSerialInterface serial_interface;
  #elif defined(BLE_PIN_CODE)
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#elif defined(SIM_PLATFORM)
  // No real BLE/USB companion-app transport in Phase 1 -- always reports
  // "not connected". See variants/sim/SimSerialInterface.h.
  #include <SimSerialInterface.h>
  SimSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

/* WIFI RECONNECT TRACKERS */
#if defined(ESP32) && defined(WIFI_SSID)
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

void setup() {
  Serial.begin(115200);
  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef DUAL_SERIAL
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin(), Serial);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          WIFI_DEBUG_PRINTLN("WiFi disconnected. Flagging for reconnect...");
          wifi_needs_reconnect = true;
      } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
          wifi_needs_reconnect = false;
      }
  });

  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(DUAL_SERIAL)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin(), Serial);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(SIM_PLATFORM)
  // sim_fs already exists/mkdir'd itself in its constructor above.
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  serial_interface.begin();
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  store.restoreRTCTime();
  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  // Apply saved brightness as soon as prefs are available so the tail of
  // the loading screen is not stuck at full brightness.
  if (disp && the_mesh.getNodePrefs())
    disp->setBrightness(the_mesh.getNodePrefs()->display_brightness);
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif

#ifdef NRF52_PLATFORM
  NRF_WDT->CONFIG      = 0x01;        // run during sleep; pause during debug halt
  NRF_WDT->CRV         = 32768*30-1;  // 30 second timeout
  NRF_WDT->RREN        = 0x01;        // enable reload register 0
  NRF_WDT->TASKS_START = 1;
#endif
  board.onBootComplete();
#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  g_sim_ready = true;
#endif
}

void loop() {
#ifdef NRF52_PLATFORM
  NRF_WDT->RR[0] = 0x6E524635UL;  // pet watchdog
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

  // CPU sleeps until next interrupt (radio, timer, BLE) — but only when the
  // mesh has no pending work, so queued TX/processing isn't delayed.
  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#endif
  }

#if defined(ESP32) && defined(WIFI_SSID)
  // Safely attempt to reconnect every 10 seconds if flagged
  if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
    WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    last_wifi_reconnect_attempt = millis();
  }
#endif
}

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
#include <emscripten.h>
// Phase 3 sim test hooks -- a browser test harness (no real phone app, no
// on-device keyboard-driven compose flow in this sim yet) needs SOME way
// to trigger "send a flood advert" / "send a DM" from JS. Every one of
// these calls straight into the exact same real BaseChatMesh/MyMesh
// functions the real phone-app serial protocol (CMD_SEND_SELF_ADVERT,
// CMD_SEND_TXT_MSG in this same file) or the on-device UI compose flow
// (MessagesScreen::afterSend) already use -- real crypto, real routing,
// real contact table, nothing about the mesh/message logic is faked here,
// only "what UI gesture triggers it" is short-circuited. See the Phase 3
// report for why: scripting the on-device virtual keyboard widget
// key-by-key to compose free text was judged not worth the fragility for
// an automated test, versus this ~20-line, obviously-inert-on-real-hardware
// addition.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_is_ready() {
  return g_sim_ready ? 1 : 0;
}

// A host page's "Reset" control (meshcore-solo-site's RESET button,
// mesh.html's own Reset buttons) re-invokes the MODULARIZE factory
// function for the same simInstanceTag to simulate a real device restart
// -- board.reboot() is inert here (exit()s the whole wasm process, which
// under -sEXIT_RUNTIME=0 just freezes the tab). That leaves the OLD
// Module instance's own emscripten_set_main_loop() callback (see
// sim_main.cpp's sim_idbfs_ready()) still registered and still ticking
// forever afterwards -- nothing ever tore it down. Two real, user-visible
// consequences: it keeps re-drawing onto the same simInstanceTag-keyed
// <canvas> element the NEW instance is also drawing onto (visible as
// flicker/reversion once more than one reset has piled up orphaned
// instances), and if the old instance had been sitting on the Shutdown
// screen, HomeScreen::poll() keeps re-firing shutdown() -> turnOff() on
// every tick, permanently blacking that canvas out from under the new
// instance. A host page should call this on the OLD Module reference
// right before discarding it (i.e. right before re-invoking the
// MODULARIZE factory for that same tag) to actually stop it.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_stop_main_loop() {
  emscripten_cancel_main_loop();
}

extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_advert_flood() {
  if (!g_sim_ready) return 0;
  return the_mesh.advertFlood() ? 1 : 0;
}

// getContactByIdx(idx, ...) indexes DIRECTLY into the raw contacts[] array
// (src/helpers/BaseChatMesh.cpp) with NO offset applied -- getNumContacts()
// only SUBTRACTS MAX_ANON_CONTACTS from the count to hide the reserved
// anon-request slots at the front of that array, it doesn't shift where
// index 0 points. The real UI/CLI code never hits this because it always
// walks contacts via startContactsIterator() (BaseChatMesh.cpp), which
// already begins at MAX_ANON_CONTACTS -- this small helper mirrors that
// same offset for these test-only hooks instead of duplicating an iterator.
static bool findFirstChatContact(ContactInfo& out) {
  int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) && ci.type == ADV_TYPE_CHAT) {
      out = ci;
      return true;
    }
  }
  return false;
}

// Finds the first known contact of type ADV_TYPE_CHAT (i.e. another
// companion_radio instance, not a repeater/room) and sends it a real DM via
// BaseChatMesh::sendMessage() -- the exact function CMD_SEND_TXT_MSG calls.
// Returns MSG_SEND_SENT_FLOOD/MSG_SEND_SENT_DIRECT/MSG_SEND_FAILED (see
// src/helpers/BaseChatMesh.h), or -1 if no chat contact is known yet.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_send_msg_to_first_contact(const char* text) {
  if (!g_sim_ready) return -1;
  ContactInfo ci;
  if (!findFirstChatContact(ci)) return -1;
  uint32_t expected_ack, est_timeout;
  uint32_t ts = rtc_clock.getCurrentTimeUnique();
  return the_mesh.sendMessage(ci, ts, 0, text, expected_ack, est_timeout);
}

// How many contacts this instance has discovered so far (any type) -- lets
// the JS ether-tick loop poll "has advert propagation finished yet" without
// guessing a fixed timeout.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_get_num_contacts() {
  // Same g_sim_ready gate as every other hook here: before setup() runs,
  // BaseChatMesh::num_contacts hasn't been seeded to MAX_ANON_CONTACTS yet,
  // so getNumContacts() would report a negative count.
  if (!g_sim_ready) return 0;
  return the_mesh.getNumContacts();
}

// Real hardware ships with NodePrefs::HP_DEFAULT -- a curated 5-page Home
// carousel (Clock/Tools/Shutdown/Favourites/Map) -- so a first-time user
// isn't handed 13 pages to joystick through; the rest (Recent/Radio/
// Bluetooth/Advert/GPS/Sensors) are opt-in via Settings > Home Pages. The
// demo site exists specifically to show off the whole feature set, so it
// calls this once right after boot to opt every instance into all of them
// instead -- 0 means "all visible" (see the home_pages_mask comment in
// NodePrefs.h), same as an as-yet-unset field on a factory-fresh device.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_show_all_home_pages() {
  if (!g_sim_ready) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  prefs->home_pages_mask = 0;
  return 1;
}

#ifdef DISPLAY_CLASS
// Jumps the on-device UI straight to the DM thread with the first known
// ADV_TYPE_CHAT contact (UITask::openContactDM() -- the exact same real
// function NearbyScreen's contact-list "select" action calls) so a test
// harness can screenshot the canvas and see the actual received message
// text, rendered by the real MessagesScreen/DisplayDriver code, without
// having to script the on-device contact-list navigation key-by-key.
// Returns 1 if a chat contact was found and the screen switched, 0 if not
// (e.g. advert propagation hasn't reached this instance yet).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_open_dm_with_first_contact() {
  if (!g_sim_ready) return 0;
  ContactInfo ci;
  if (!findFirstChatContact(ci)) return 0;
  ui_task.openContactDM(ci);
  return 1;
}
#endif

// Same idea as findFirstChatContact() above, but for the first known
// admin-loginable contact (a repeater or room server) instead of another
// chat instance.
static bool findFirstAdminContact(ContactInfo& out) {
  int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) &&
        (ci.type == ADV_TYPE_REPEATER || ci.type == ADV_TYPE_ROOM)) {
      out = ci;
      return true;
    }
  }
  return false;
}

#ifdef DISPLAY_CLASS
// Jumps the on-device UI straight to AdminScreen for the first known
// repeater/room contact -- UITask::openAdminFor(ci, false), the exact same
// function NearbyScreen's "Nodes" Hold-Enter admin action calls (see
// NearbyScreen.h:709), so a test harness can screenshot the real Admin
// screen instead of scripting contact-list navigation key-by-key. Returns
// 1 if a repeater/room contact was found and the screen switched, 0 if not.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_open_admin_with_first_repeater() {
  if (!g_sim_ready) return 0;
  ContactInfo ci;
  if (!findFirstAdminContact(ci)) return 0;
  ui_task.openAdminFor(ci, false);
  return 1;
}
#endif

// Submits a login against the first known repeater/room contact via
// MyMesh::sendRoomLogin() -- the exact same function AdminScreen's own
// submit button calls (examples/companion_radio/ui-new/AdminScreen.h) --
// so a test harness can verify the admin/password flow without scripting
// the on-device virtual keyboard. Only reports whether the login *request*
// was sent (matching sim_test_send_msg_to_first_contact()'s same
// synchronous-only contract) -- the actual accept/reject arrives async via
// AbstractUITask::onRoomLoginResult() and is visible on AdminScreen once
// sim_test_open_admin_with_first_repeater() has switched to it. Returns 1
// if sent, 0 if send failed, -1 if no repeater/room contact known yet.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_login_first_repeater(const char* password) {
  if (!g_sim_ready) return -1;
  ContactInfo ci;
  if (!findFirstAdminContact(ci)) return -1;
  uint32_t est_timeout;
  return the_mesh.sendRoomLogin(ci, password, est_timeout) ? 1 : 0;
}
#endif
