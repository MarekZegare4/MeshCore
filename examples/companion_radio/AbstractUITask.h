#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/BaseChatMesh.h>   // MAX_TEXT_LEN, for addOwnChannelMsg() below
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    advertReceivedFlood,
    advertReceivedZeroHop,
    ack
};

class AbstractUITask {
protected:
  mesh::MainBoard* _board;
  BaseSerialInterface* _serial;
  bool _connected;

  AbstractUITask(mesh::MainBoard* board, BaseSerialInterface* serial) : _board(board), _serial(serial) {
    _connected = false;
  }

public:
  void setHasConnection(bool connected) {
    bool prev = _connected;
    _connected = connected;
    if (prev && !connected) onBLEDisconnected();
  }
  bool hasConnection() const { return _connected; }
  virtual void onBLEDisconnected() {}
  // An end-to-end ACK (CRC) arrived for one of our sent messages — drives the
  // DM delivery-status marker. Default no-op for UIs that don't track it.
  virtual void onMsgAck(uint32_t ack_crc) { (void)ack_crc; }
  // A repeater rebroadcast of one of our channel sends was heard (seq from
  // lastChannelRelaySeq()) — drives the channel "relayed into mesh" marker.
  // May fire once per distinct repeater within earshot for the same seq;
  // repeater_hash/hash_size (when given) is that repeater's path hash, so the
  // UI can list every repeater that confirmed, not just "was it heard at all".
  virtual void onChannelRelayed(uint32_t seq, const uint8_t* repeater_hash = nullptr, uint8_t hash_size = 0) {
    (void)seq; (void)repeater_hash; (void)hash_size;
  }
  // Result of an on-device-UI-triggered MyMesh::sendRoomLogin() arrived.
  // pub_key is the contact's key prefix (>=4 bytes valid); permissions is the
  // room/repeater ACL byte (only meaningful when success is true).
  virtual void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) { (void)pub_key; (void)success; (void)permissions; }
  // Text reply to an on-device-UI-triggered MyMesh::sendAdminCommand() arrived
  // (see AdminScreen). pub_key is the contact's key prefix (>=4 bytes valid).
  virtual void onAdminReply(const uint8_t* pub_key, const char* text) { (void)pub_key; (void)text; }
  // Bot action commands (!gps/!buzz, see MyMesh::botCommandReply) -- device
  // state changes triggered remotely, gated by the bot_actions_* prefs.
  // Default no-op so UI variants that don't wire these up just ignore them.
  virtual void botSetGPS(bool on) { (void)on; }
  virtual void botBuzz(int seconds) { (void)seconds; }
  // !gpio1..!gpio4 (idx 1-4). botSetGPIO returns false if the pin isn't
  // currently configured as an Output (or the board has none) -- lets the
  // bot reply distinguish "set" from "ignored". botGetGPIO returns false if
  // the pin is Off/unsupported; on true, fills is_output (current direction)
  // and value (live level).
  virtual bool botSetGPIO(int idx, bool on) { (void)idx; (void)on; return false; }
  virtual bool botGetGPIO(int idx, bool& is_output, bool& value) { (void)idx; (void)is_output; (void)value; return false; }
  // Analog read for pins that support it (GPIO1/GPIO2 on Wio Tracker L1 --
  // the nRF52840's AIN0/AIN5). Returns false if the pin isn't in Analog mode
  // or doesn't support it; on true, fills millivolts with the reading.
  virtual bool botGetGPIOAnalog(int idx, int& millivolts) { (void)idx; (void)millivolts; return false; }
  // True only when a BLE central is actually bonded/connected. On a dual
  // (BLE+USB) interface hasConnection() is always true (USB counts), so use
  // this for BLE-specific UI like the pairing-PIN prompt.
  bool isBLEConnected() const { return _serial->isBLEConnected(); }
  // True when a companion app is connected over any transport (BLE bonded or an
  // open USB-CDC port). For app-connected behaviour like Auto buzzer mute.
  bool isClientConnected() const { return _serial->isClientConnected(); }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isSerialEnabled() const { return _serial->isEnabled(); }
  void enableSerial() { _serial->enable(); }
  void disableSerial() { _serial->disable(); }
  virtual void msgRead(int msgcount) = 0;
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t contact_type = 0, const uint8_t* pub_key = nullptr) = 0;
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  // Returns the new entry's ring position (see MessageHistory::addChannelMsg),
  // or -1 on a UI variant that doesn't track history (default no-op below) --
  // callers that need it (to then arm a relay-echo tracker) should check for
  // that instead of assuming a valid position. path/path_len (packed
  // (hash_size-1)<<6|hop_count, same as mesh::Packet::path_len) is the hop
  // route this incoming post actually took -- nullptr/0 when not known (e.g.
  // this is our own outgoing post). own_message: our own post (e.g. mirrored
  // from an app-originated send) -- never counted unread, unlike a genuinely
  // incoming post the device's own UI doesn't happen to be showing right now.
  virtual int addChannelMsg(uint8_t channel_idx, const char* text, uint32_t timestamp = 0,
                            const uint8_t* path = nullptr, uint8_t path_len = 0,
                            bool own_message = false) { return -1; }
  // Convenience wrapper around addChannelMsg() for mirroring a channel post
  // this device just sent itself (bot trigger/command reply, a !gps fix
  // result, an app-originated send) into the on-device history. Always frames
  // it with the literal "Me: " prefix -- the convention MessagesScreen uses
  // (see its bubble-side check) to tell an outgoing post from an incoming
  // one -- and always passes own_message=true, so a caller can't reintroduce
  // the bug this replaced: three separate MyMeshBot.h call sites used to
  // build "<node_name>: " instead, which rendered the reply as an incoming
  // bubble from a stranger who happened to share the device's own name.
  // text_len < 0 (default) means text is null-terminated; otherwise only the
  // first text_len bytes are used (a source buffer isn't always guaranteed
  // to be null-terminated, e.g. the app-originated mirror).
  int addOwnChannelMsg(uint8_t channel_idx, const char* text, int text_len = -1, uint32_t timestamp = 0) {
    char buf[MAX_TEXT_LEN + 8];   // "Me: "(4) + text(MAX_TEXT_LEN) + margin
    if (text_len < 0) snprintf(buf, sizeof(buf), "Me: %s", text);
    else              snprintf(buf, sizeof(buf), "Me: %.*s", text_len, text);
    return addChannelMsg(channel_idx, buf, timestamp, nullptr, 0, true);
  }
  // Arms the "relayed into mesh" tracker (a heard repeater rebroadcast) on the
  // entry at ring position pos, e.g. right after addChannelMsg for a channel
  // send this device just originated. seq: MyMesh::lastChannelRelaySeq().
  virtual void armChannelRelay(int pos, uint32_t seq) {}
  // ack_tag/ack_deadline_ms/resends: pending-ACK tracking for an outgoing DM
  // (0 = none, e.g. incoming or "no ack expected") -- see MessageHistory::addDMMsg.
  // path/path_len: the hop route an incoming DM actually took (nullptr/0 for
  // outgoing -- a DM's delivery confirmation is the ack_tag above, not a path).
  virtual void addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text, uint32_t sender_timestamp = 0,
                        uint32_t ack_tag = 0, uint32_t ack_deadline_ms = 0, uint8_t resends = 0,
                        const uint8_t* path = nullptr, uint8_t path_len = 0) {}
  // A node shared its current position via a [LOC] message. pub_key is the
  // sender's key prefix for a verified DM share, or null for a channel share
  // (keyed by name, best-effort). Default no-op so UI variants opt in.
  virtual void onSharedLocation(const uint8_t* pub_key, const char* name,
                                int32_t lat_1e6, int32_t lon_1e6,
                                uint32_t ts, bool verified) {}
  // A contact is gone — removed explicitly (companion app / CLI command) or
  // silently auto-evicted to make room when the contact table is full. Lets
  // UI state that references contacts by pubkey (favourite slots, the
  // Locator/Live Share target) drop a reference that would otherwise dangle.
  // Default no-op.
  virtual void onContactRemoved(const uint8_t* pub_key) {}
  // A channel slot was cleared (companion app set it to an empty secret).
  // Drop any setting that referenced it by index — otherwise a new channel
  // added later at the same slot would silently inherit the old one's bot/
  // share target or notification melody. Default no-op.
  virtual void onChannelRemoved(uint8_t channel_idx) {}
  // Single choke point for every controlled power-down (low-battery auto-off,
  // long-press power-off, and now every board.reboot() caller too): flush
  // prefs/RTC/contacts/trail before the board actually goes down or restarts,
  // so no exit path can silently skip a pending write. restart=true reboots,
  // false powers off. Every UI variant (ui-new/ui-tiny/ui-orig) implements
  // this the same way -- see each's UITask::shutdown().
  virtual void shutdown(bool restart = false) = 0;
  virtual void loop() = 0;
};
