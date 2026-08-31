#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after KeyboardWidget.h is defined.

#include "TabBar.h"

class BotScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  // Categories are a circular tab bar in the header (shared geometry with
  // NearbyScreen's filter tabs and AdminScreen's category tabs — see
  // TabBar.h). LEFT/RIGHT switches tabs; UP/DOWN moves within the active
  // tab's rows. Because LEFT/RIGHT is fully owned by tab-switching, every
  // row's value is edited via Enter only — no more inline LEFT/RIGHT cycling.
  enum Tab : uint8_t { TAB_CHANNEL, TAB_ROOM, TAB_DIRECT, TAB_OTHER, TAB_COUNT };
  static const char* TAB_LABELS[TAB_COUNT];

  enum Kind : uint8_t { ENABLE_DM, DM_SCOPE, COMMANDS_DM, ACTIONS_DM, TRIGGER_DM, REPLY_DM,
                        ENABLE_CH, CHANNEL, COMMANDS_CH, ACTIONS_CH, TRIGGER_CH, REPLY_CH,
                        ENABLE_ROOM, ROOM, COMMANDS_ROOM, ACTIONS_ROOM, TRIGGER_ROOM, REPLY_ROOM,
                        QUIET_FROM, QUIET_TO };
  struct Row { Kind kind; const char* label; };

  static const int ROWS_PER_TAB[TAB_COUNT];

  // Row labels drop the "DM"/"Ch"/"Room" suffix the old flat list needed —
  // the active tab already gives that context, freeing up value-column width.
  // Commands lives on each tab (not a shared "Other" toggle) so DM/channel/
  // room can each independently answer !ping etc. or stay quiet.
  static Row tabRow(int tab, int idx) {
    static const Row DIRECT[] = {
      { ENABLE_DM,   "Enable" },
      { DM_SCOPE,    "DM allow" },
      { COMMANDS_DM, "Commands" },
      { ACTIONS_DM,  "Actions" },
      { TRIGGER_DM,  "Trigger" },
      { REPLY_DM,    "Reply" },
    };
    static const Row CHANNEL_ROWS[] = {
      { ENABLE_CH,   "Enable" },
      { CHANNEL,     "Channel" },
      { COMMANDS_CH, "Commands" },
      { ACTIONS_CH,  "Actions" },
      { TRIGGER_CH,  "Trigger" },
      { REPLY_CH,    "Reply" },
    };
    static const Row ROOM_ROWS[] = {
      { ENABLE_ROOM,   "Enable" },
      { ROOM,          "Room" },
      { COMMANDS_ROOM, "Commands" },
      { ACTIONS_ROOM,  "Actions" },
      { TRIGGER_ROOM,  "Trigger" },
      { REPLY_ROOM,    "Reply" },
    };
    static const Row OTHER[] = {
      { QUIET_FROM, "Quiet from" },
      { QUIET_TO,   "Quiet to" },
    };
    switch (tab) {
      case TAB_CHANNEL: return CHANNEL_ROWS[idx];
      case TAB_ROOM:    return ROOM_ROWS[idx];
      case TAB_DIRECT:  return DIRECT[idx];
      default:          return OTHER[idx];
    }
  }

  uint8_t _tab;    // persists across visits, like NearbyScreen's _filter
  int  _sel;       // selected row index within the active tab
  int  _scroll;    // index of first visible row (managed by drawList in render)
  bool _dirty;

  // keyboard state (reused for trigger and reply fields)
  int             _kb_row;   // -1=off, else the row index (within the active tab) being edited
  KeyboardWidget* _kb;

  // Quiet-hours stepper: Enter on Quiet from/to enters this sub-mode (LEFT/
  // RIGHT is unavailable for +/- now that it switches tabs), where UP/DOWN
  // steps the hour and Enter/Cancel exits back to normal row browsing.
  int _stepper_row;  // -1=off, else the row index being stepped

  // channel/room counts (refreshed on enter) — just gate whether Enter opens
  // the (possibly empty) picker; the picker itself browses the real list, so
  // no local index cache is needed now that there's no LEFT/RIGHT quick-cycle.
  int _num_channels;
  int _num_rooms;

  void refreshChannels() {
    _num_channels = 0;
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') _num_channels++;
  }

  void refreshRooms() {
    _num_rooms = 0;
    ContactInfo ci;
    int total = the_mesh.getNumContacts();
    for (int i = 0; i < total; i++)
      if (the_mesh.getContactByIdx(i, ci) && ci.type == ADV_TYPE_ROOM) _num_rooms++;
  }

  // Header as a circular tab bar (shared geometry — see TabBar.h). `right_reserve`
  // keeps the reply counter (drawn after this call) clear of the rightmost tab.
  void drawTabBar(DisplayDriver& display, int right_reserve) {
    tabbar::draw(display, TAB_LABELS, TAB_COUNT, _tab, right_reserve);
  }

public:
  BotScreen(UITask* task, NodePrefs* prefs, KeyboardWidget* kb)
    : _task(task), _prefs(prefs), _tab(TAB_CHANNEL), _kb(kb) {}

  void onShow() override {
    // _tab persists across visits (like NearbyScreen's _filter) — only reset
    // the in-tab cursor and any open sub-mode.
    _sel        = 0;
    _scroll     = 0;
    _kb_row     = -1;
    _stepper_row = -1;
    _dirty      = false;
    refreshChannels();
    refreshRooms();
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_kb_row >= 0) {
      return _kb->render(display);
    }

    int val_x = display.valCol();

    // Reply counter, right-aligned in the header — reserve its width from the
    // tab bar before drawing so a right-side tab can't run under it.
    uint16_t sent = the_mesh.botReplyCount();
    char cbuf[12];
    int  cw = 0;
    if (sent > 0) {
      snprintf(cbuf, sizeof(cbuf), "%u", (unsigned)sent);
      cw = display.getTextWidth(cbuf);
    }
    drawTabBar(display, sent > 0 ? cw + 2 : 0);
    if (sent > 0) {
      display.setCursor(display.width() - cw - 1, 0);
      display.print(cbuf);
      display.setColor(DisplayDriver::LIGHT);
    }

    int n = ROWS_PER_TAB[_tab];
    int mq_delay = 0;
    drawList(display, n, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      Row r = tabRow(_tab, i);
      drawRowSelection(display, y, sel, reserve);
      display.setCursor(2, y);
      display.print(r.label);
      display.setCursor(val_x, y);

      switch (r.kind) {
        case ENABLE_DM:
          display.print(_prefs->bot_enabled ? "ON" : "OFF");
          break;
        case ENABLE_CH:
          display.print(_prefs->bot_channel_enabled ? "ON" : "OFF");
          break;
        case ENABLE_ROOM:
          display.print(_prefs->bot_room_enabled ? "ON" : "OFF");
          break;
        case DM_SCOPE:
          display.print(_prefs->bot_dm_scope ? "Fav" : "All");
          break;
        case CHANNEL: {
          ChannelDetails ch;
          if (_num_channels == 0)
            display.print("(none)");
          else if (the_mesh.getChannel(_prefs->bot_channel_idx, ch) && ch.name[0]) {
            int mqr = display.drawTextEllipsized(val_x, y, display.width() - val_x - 1 - reserve, ch.name, sel);
            if (sel && mqr > 0) mq_delay = mqr;
          } else
            display.print("?");
          break;
        }
        case ROOM: {
          if (_num_rooms == 0) {
            display.print("(none)");
          } else {
            ContactInfo* c = the_mesh.lookupContactByPubKey(_prefs->bot_room_prefix, NodePrefs::FAVOURITE_PREFIX_LEN);
            if (c && c->name[0]) {
              int mqr = display.drawTextEllipsized(val_x, y, display.width() - val_x - 1 - reserve, c->name, sel);
              if (sel && mqr > 0) mq_delay = mqr;
            } else
              display.print("?");
          }
          break;
        }
        case TRIGGER_DM:
        case TRIGGER_CH:
        case TRIGGER_ROOM: {
          const char* tr = (r.kind == TRIGGER_DM) ? _prefs->bot_trigger
                          : (r.kind == TRIGGER_CH) ? _prefs->bot_trigger_ch
                                                   : _prefs->bot_trigger_room;
          const char* shown = !tr[0] ? "(none)"
                            : (tr[0] == '*' && !tr[1]) ? "(any msg)"   // wildcard / away mode
                                                       : tr;
          int mqr = display.drawTextEllipsized(val_x, y, display.width() - val_x - 1 - reserve, shown, sel);
          if (sel && mqr > 0) mq_delay = mqr;
          break;
        }
        case REPLY_DM:
        case REPLY_CH:
        case REPLY_ROOM: {
          const char* rp = (r.kind == REPLY_DM) ? _prefs->bot_reply_dm
                          : (r.kind == REPLY_CH) ? _prefs->bot_reply_ch
                                                 : _prefs->bot_reply_room;
          int mqr = display.drawTextEllipsized(val_x, y, display.width() - val_x - 1 - reserve, rp[0] ? rp : "(none)", sel);
          if (sel && mqr > 0) mq_delay = mqr;
          break;
        }
        case COMMANDS_DM:
          display.print(_prefs->bot_commands_enabled ? "ON" : "OFF");
          break;
        case COMMANDS_CH:
          display.print(_prefs->bot_commands_ch ? "ON" : "OFF");
          break;
        case COMMANDS_ROOM:
          display.print(_prefs->bot_commands_room ? "ON" : "OFF");
          break;
        case ACTIONS_DM:
          display.print(_prefs->bot_actions_dm ? "ON" : "OFF");
          break;
        case ACTIONS_CH:
          display.print(_prefs->bot_actions_ch ? "ON" : "OFF");
          break;
        case ACTIONS_ROOM:
          display.print(_prefs->bot_actions_room ? "ON" : "OFF");
          break;
        case QUIET_FROM:
        case QUIET_TO: {
          bool off = (_prefs->bot_quiet_start == _prefs->bot_quiet_end);
          char hb[10];
          if (off) {
            strcpy(hb, "OFF");
          } else {
            uint8_t h = (r.kind == QUIET_FROM) ? _prefs->bot_quiet_start : _prefs->bot_quiet_end;
            snprintf(hb, sizeof(hb), "%02d:00", h);
          }
          // Bracket the value while the stepper sub-mode is open on this row,
          // as a visual cue that UP/DOWN now steps it instead of moving rows.
          if (_stepper_row == i) {
            char bracketed[14];
            snprintf(bracketed, sizeof(bracketed), "[%s]", hb);
            display.print(bracketed);
          } else {
            display.print(hb);
          }
          break;
        }
        default: break;
      }
      display.setColor(DisplayDriver::LIGHT);
    });
    return 2000;
  }

  bool handleInput(char c) override {
    bool up    = (c == KEY_UP);
    bool down  = (c == KEY_DOWN);
    bool enter = (c == KEY_ENTER);
    bool cancel = (c == KEY_CANCEL);

    if (_kb_row >= 0) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        Kind  k   = tabRow(_tab, _kb_row).kind;
        char* dst = fieldBuf(k);
        int   cap = fieldCap(k);
        if (dst) { strncpy(dst, _kb->buf, cap - 1); dst[cap - 1] = '\0'; }
        _dirty  = true;
        _kb_row = -1;
      } else if (res == KeyboardWidget::CANCELLED) {
        _kb_row = -1;
      }
      return true;
    }

    if (_stepper_row >= 0) {
      Kind k = tabRow(_tab, _stepper_row).kind;
      uint8_t& h = (k == QUIET_FROM) ? _prefs->bot_quiet_start : _prefs->bot_quiet_end;
      if (up)   { h = (h + 1) % 24;  _dirty = true; return true; }
      if (down) { h = (h + 23) % 24; _dirty = true; return true; }
      if (enter || cancel) { _stepper_row = -1; return true; }
      return true;  // swallow LEFT/RIGHT etc. while stepping
    }

    if (cancel) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoToolsScreen();
      return true;
    }
    if (keyIsPrev(c)) { _tab = (_tab + TAB_COUNT - 1) % TAB_COUNT; _sel = _scroll = 0; return true; }
    if (keyIsNext(c)) { _tab = (_tab + 1) % TAB_COUNT;             _sel = _scroll = 0; return true; }

    int n = ROWS_PER_TAB[_tab];
    // drawList() reclamps _scroll from _sel every render.
    if (up)   { _sel = (_sel > 0) ? _sel - 1 : n - 1; return true; }
    if (down) { _sel = (_sel < n - 1) ? _sel + 1 : 0; return true; }

    if (!enter) return false;
    Kind k = tabRow(_tab, _sel).kind;

    switch (k) {
      case ENABLE_DM:   _prefs->bot_enabled ^= 1;         _dirty = true; return true;
      case ENABLE_CH:   _prefs->bot_channel_enabled ^= 1; _dirty = true; return true;
      case ENABLE_ROOM: _prefs->bot_room_enabled ^= 1;    _dirty = true; return true;
      case DM_SCOPE:      _prefs->bot_dm_scope ^= 1;         _dirty = true; return true;
      case COMMANDS_DM:   _prefs->bot_commands_enabled ^= 1; _dirty = true; return true;
      case COMMANDS_CH:   _prefs->bot_commands_ch ^= 1;      _dirty = true; return true;
      case COMMANDS_ROOM: _prefs->bot_commands_room ^= 1;    _dirty = true; return true;
      case ACTIONS_DM:    _prefs->bot_actions_dm ^= 1;       _dirty = true; return true;
      case ACTIONS_CH:    _prefs->bot_actions_ch ^= 1;       _dirty = true; return true;
      case ACTIONS_ROOM:  _prefs->bot_actions_room ^= 1;     _dirty = true; return true;
      case CHANNEL:
        // Full browsable channel picker (same experience as Live Share's "To"
        // row); which channel is *active* is now the separate Enable row.
        if (_num_channels == 0) return false;
        _task->savePrefsIfDirty(_dirty);
        _task->pickBotChannelTarget();
        return true;
      case ROOM:
        if (_num_rooms == 0) return false;
        _task->savePrefsIfDirty(_dirty);
        _task->pickBotRoomTarget();
        return true;
      case QUIET_FROM:
      case QUIET_TO:
        _stepper_row = _sel;
        return true;
      case TRIGGER_DM:
      case TRIGGER_CH:
      case TRIGGER_ROOM:
      case REPLY_DM:
      case REPLY_CH:
      case REPLY_ROOM: {
        _kb_row = _sel;
        _kb->begin(fieldBuf(k), fieldCap(k) - 1);
        bool is_trigger = (k == TRIGGER_DM || k == TRIGGER_CH || k == TRIGGER_ROOM);
        if (is_trigger) {
          _kb->clearPlaceholders();  // trigger is literal — placeholders never match
        } else {
          kbAddSensorPlaceholders(*_kb, &sensors);
          // Only meaningful in a bot reply (there's an actual triggering
          // sender/hop-count to fill them with) — not offered on the general
          // compose keyboard's own placeholder list.
          _kb->addPlaceholder("{name}");
          _kb->addPlaceholder("{hops}");
        }
        return true;
      }
      default: return false;
    }
  }

private:
  // Maps a keyboard-editable row kind to its backing string + capacity.
  char* fieldBuf(Kind k) {
    switch (k) {
      case TRIGGER_DM:   return _prefs->bot_trigger;
      case REPLY_DM:     return _prefs->bot_reply_dm;
      case TRIGGER_CH:   return _prefs->bot_trigger_ch;
      case REPLY_CH:     return _prefs->bot_reply_ch;
      case TRIGGER_ROOM: return _prefs->bot_trigger_room;
      case REPLY_ROOM:   return _prefs->bot_reply_room;
      default: return nullptr;
    }
  }
  int fieldCap(Kind k) {
    switch (k) {
      case TRIGGER_DM:   return sizeof(_prefs->bot_trigger);
      case REPLY_DM:     return sizeof(_prefs->bot_reply_dm);
      case TRIGGER_CH:   return sizeof(_prefs->bot_trigger_ch);
      case REPLY_CH:     return sizeof(_prefs->bot_reply_ch);
      case TRIGGER_ROOM: return sizeof(_prefs->bot_trigger_room);
      case REPLY_ROOM:   return sizeof(_prefs->bot_reply_room);
      default: return 0;
    }
  }
};

const char* BotScreen::TAB_LABELS[BotScreen::TAB_COUNT] = { "Channel", "Room", "Direct", "Other" };
const int   BotScreen::ROWS_PER_TAB[BotScreen::TAB_COUNT] = { 6, 6, 6, 2 };
