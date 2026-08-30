#pragma once
#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>
#include "icons.h"   // scalable scroll indicator (track + thumb), matches the rest of the UI

// Generic scrollable popup menu overlay.
// Caller owns item string lifetimes — addItem() stores const char* pointers.
// Navigation wraps around: up from first item goes to last, and vice versa.
struct PopupMenu {
  static const int PM_MAX_ITEMS = 24;

  const char* _items[PM_MAX_ITEMS];
  int         _count;
  int         _sel;
  int         _scroll;
  int         _visible;   // caller hint (min visible items)
  int         _cap;       // actual visible cap, updated each render()
  bool        active;
  const char* _title;

  enum Result { NONE, SELECTED, CANCELLED };

  PopupMenu() : _count(0), _sel(0), _scroll(0), _visible(3), _cap(3), active(false), _title(nullptr) {}

  void begin(const char* title, int visible = 3) {
    _count = 0; _sel = 0; _scroll = 0;
    _visible = visible; _cap = visible; active = true; _title = title;
  }

  void addItem(const char* item) {
    if (_count < PM_MAX_ITEMS) _items[_count++] = item;
  }

  int render(DisplayDriver& display) {
    // Everything is derived from the live font metrics so the box fits its
    // content on every display — including landscape e-ink, where the font (and
    // line height) is ~2x the OLED. Fixed pixel constants used to clip the text
    // and leave the box the wrong size there.
    const int lh      = display.getLineHeight();
    const int cw      = display.getCharWidth();
    const int sh      = display.sepH();      // separator thickness (2px landscape e-ink, else 1)
    const int item_h  = lh + 2;             // row pitch
    const int pad     = 4;                  // inner left/right padding
    const int margin  = 4;                  // gap to the screen edge
    // Compact menu: rows tile straight under the separator (no header gap here —
    // unlike full screens) so the selection bar fills its cell edge to edge.
    const int title_h = lh + 2 + sh;        // title text + scaled separator

    // Hard ceiling: never show more rows than physically fit on screen.
    int avail_h = display.height() - margin * 2 - title_h - 2;
    int max_by_height = avail_h / item_h;
    if (max_by_height < 1) max_by_height = 1;
    _cap = max_by_height;
    int vis = (_count < _cap) ? _count : _cap;
    // render() is the single source of truth for _scroll: using the cap just
    // computed from the live display height, keep the selection inside the
    // visible window. handleInput() therefore never has to touch _scroll (and
    // can't act on a stale _cap from before the first render).
    if (_sel < _scroll)             _scroll = _sel;
    else if (_sel >= _scroll + vis) _scroll = _sel - vis + 1;
    int max_scroll = _count - vis;
    if (max_scroll < 0)       max_scroll = 0;
    if (_scroll > max_scroll) _scroll = max_scroll;
    if (_scroll < 0)          _scroll = 0;

    bool can_scroll = (_count > vis);
    // Right gutter for the scroll indicator column, plus a couple px of
    // clearance from the box border (full-screen lists sit flush against the
    // screen edge instead, so they don't need this extra margin).
    int  arrow_w    = can_scroll ? (scrollIndicatorColWidth(display) + 2) : 0;

    // Box width: widest of the title / all items, plus padding and the arrow
    // gutter; clamped to the screen with a sane minimum.
    int content_w = _title ? display.getTextWidth(_title) : 0;
    for (int i = 0; i < _count; i++) {
      int w = display.getTextWidth(_items[i]);
      if (w > content_w) content_w = w;
    }
    int bw = content_w + pad * 2 + arrow_w;
    int max_bw = display.width() - margin * 2;
    if (bw > max_bw) bw = max_bw;
    int min_bw = cw * 6 + pad * 2;
    if (bw < min_bw) bw = min_bw;
    if (bw > max_bw) bw = max_bw;

    int bh = title_h + vis * item_h + 1;   // +1 keeps the last row off the bottom border

    // Centre on screen (clamped to the margins on small displays).
    int bx = (display.width()  - bw) / 2;
    int by = (display.height() - bh) / 2;
    if (bx < margin) bx = margin;
    if (by < margin) by = margin;

    display.setColor(DisplayDriver::DARK);
    display.fillRect(bx, by, bw, bh);
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(bx, by, bw, bh);
    if (_title) display.drawTextEllipsized(bx + pad, by + 1, bw - pad * 2, _title);
    display.fillRect(bx, by + lh + 2, bw, sh);   // separator just under the title; gap follows

    int list_y = by + title_h;
    int text_w = bw - pad * 2 - arrow_w;
    if (text_w < cw) text_w = cw;
    for (int i = 0; i < vis && (_scroll + i) < _count; i++) {
      int idx = _scroll + i;
      int py  = list_y + i * item_h + 1;
      if (idx == _sel) {
        display.setColor(DisplayDriver::LIGHT);
        // Stops short of the scroll-indicator gutter, like every other list's
        // selection bar (e.g. drawList's row width - reserve) — otherwise the
        // bar paints over the indicator's column instead of framing it.
        display.fillRect(bx + 1, py - 1, bw - 2 - arrow_w, item_h);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      // Return value not needed here: this popup already redraws every 50ms
      // (below), faster than any marquee step, so the animation is already smooth.
      display.drawTextEllipsized(bx + pad, py, text_w, _items[idx], idx == _sel);
      display.setColor(DisplayDriver::LIGHT);
    }

    // Same proportional track + thumb indicator the rest of the UI uses,
    // anchored to the box's own right edge (a couple px clear of the border)
    // instead of the screen edge since this box floats centred on screen.
    drawScrollIndicator(display, bx + bw - 2, list_y, vis * item_h, _count, vis, _scroll);
    display.setColor(DisplayDriver::LIGHT);
    return 50;
  }

  Result handleInput(char c) {
    if (_count == 0) { active = false; return CANCELLED; }
    // Selection only moves here; render() keeps it scrolled into view.
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : _count - 1; return NONE; }
    if (c == KEY_DOWN) { _sel = (_sel < _count - 1) ? _sel + 1 : 0; return NONE; }
    if (c == KEY_ENTER)                           { active = false; return SELECTED;  }
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) { active = false; return CANCELLED; }
    return NONE;
  }

  int selectedIndex() const { return _sel; }
  int count() const { return _count; }
  void setSelected(int i) { if (i >= 0 && i < _count) _sel = i; }
};
