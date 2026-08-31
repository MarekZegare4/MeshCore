#pragma once
// Configures which data fields appear on the clock home page.
// Included by UITask.cpp after BotScreen.h.

// Field type constants — used here and in UITask.cpp HP_CLOCK render.
static const uint8_t DASH_NONE    = 0;
static const uint8_t DASH_BATT_V  = 1;
static const uint8_t DASH_TEMP    = 2;
static const uint8_t DASH_HUM     = 3;
static const uint8_t DASH_PRES    = 4;
static const uint8_t DASH_GPS     = 5;
static const uint8_t DASH_ALT     = 6;
static const uint8_t DASH_LUX     = 7;
static const uint8_t DASH_CO2     = 8;
static const uint8_t DASH_NODES   = 9;
static const uint8_t DASH_MSGS    = 10;
static const uint8_t DASH_BATT_PCT = 11;
static const uint8_t DASH_COUNT   = 12;

class DashboardConfigScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  static const int FIELD_SLOTS = 3;

  static const char* OPTION_NAMES[DASH_COUNT];

  int  _sel;
  bool _dirty;

  void cycle(int slot, int dir) {
    uint8_t& f = _prefs->dashboard_fields[slot];
    f = (uint8_t)((f + DASH_COUNT + dir) % DASH_COUNT);
    _dirty = true;
  }

public:
  DashboardConfigScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override { _sel = 0; _dirty = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    int item_h  = display.lineStep();
    int start_y = display.listStart();
    int val_x   = display.valCol();

    display.drawCenteredHeader("CLOCK FIELDS");

    static const char* labels[] = { "Field 1", "Field 2", "Field 3" };
    for (int i = 0; i < FIELD_SLOTS; i++) {
      int y = start_y + i * item_h;
      bool sel = (i == _sel);
      display.drawSelectionRow(0, y - 1, display.width(), item_h, sel);
      display.setCursor(2, y);
      display.print(labels[i]);
      display.setCursor(val_x, y);
      uint8_t f = _prefs->dashboard_fields[i];
      display.print(OPTION_NAMES[f < DASH_COUNT ? f : DASH_NONE]);
      display.setColor(DisplayDriver::LIGHT);
    }
    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoHomeScreen();
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : FIELD_SLOTS - 1; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < FIELD_SLOTS - 1) ? _sel + 1 : 0; return true; }
    if (keyIsPrev(c))  { cycle(_sel, -1); return true; }
    if (keyIsNext(c))  { cycle(_sel,  1); return true; }
    if (c == KEY_ENTER)                   { cycle(_sel,  1); return true; }
    return false;
  }
};

const char* DashboardConfigScreen::OPTION_NAMES[DASH_COUNT] = {
  "None", "Batt V", "Temp", "Humidity", "Pressure",
  "GPS", "Altitude", "Lux", "CO2", "Contacts", "Messages", "Batt %"
};
