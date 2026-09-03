#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// DisplayDriver implementation for the native sim build. Shaped like
// src/helpers/ui/NullDisplayDriver.h (same pure-virtual overrides -- start
// from that file, per the Phase-1 plan) but instead of no-ops, maintains an
// in-memory framebuffer and prints it to stdout as ASCII/block-art on
// endFrame(), so the real UITask/menu system's actual draw calls are
// visible in a terminal.
//
// Logical canvas is 128x64 (matches NullDisplayDriver / a typical SSD1306
// OLED, so layout math in the real screens behaves exactly as on that
// hardware). For terminal rendering it's downsampled onto a coarser
// CELL_W x CELL_H-pixel grid:
//   - fillRect()/drawRect()/drawXbm() mark the cells they cover as "filled"
//     (drawXbm -- icons -- has no real bitmap to rasterize in ASCII, so it's
//     approximated as a solid block, same as a fillRect over that area).
//   - print() does NOT rasterize a bitmap font -- it places the real
//     characters of the real string into the grid at the (approximate)
//     cursor cell, which is what actually makes the output legible. Real
//     text always wins over a "filled" block in the same cell.
class SimDisplayDriver : public DisplayDriver {
  static const int CELL_W = 2;   // pixels per terminal column
  static const int CELL_H = 2;   // pixels per terminal row
  static const int COLS = 128 / CELL_W;   // 64
  static const int ROWS = 64  / CELL_H;   // 32

  bool _on = false;
  int _cursor_x = 0, _cursor_y = 0;
  Color _color = LIGHT;
  bool _filled[ROWS][COLS];
  char _text[ROWS][COLS];     // 0 = no character placed
  bool _dirty = false;
  int _frame_no = 0;

  void cellOf(int px, int py, int& cx, int& cy) const {
    cx = px / CELL_W; cy = py / CELL_H;
  }

public:
  SimDisplayDriver() : DisplayDriver(128, 64) { clearBuffers(); }

  bool begin() { _on = true; return true; }

  void clearBuffers() {
    memset(_filled, 0, sizeof(_filled));
    memset(_text, 0, sizeof(_text));
  }

  bool isOn() override { return _on; }
  void turnOn() override { _on = true; }
  void turnOff() override { _on = false; }
  void clear() override { clearBuffers(); }

  void startFrame(Color bkg = DARK) override {
    clearBuffers();
    _color = LIGHT;
  }

  void setTextSize(int sz) override { /* one fixed size in ASCII output */ }

  void setColor(Color c) override { _color = c; }

  void setCursor(int x, int y) override { _cursor_x = x; _cursor_y = y; }

  void print(const char* str) override {
    if (!str) return;
    int cx, cy;
    cellOf(_cursor_x, _cursor_y, cx, cy);
    int col = cx;
    for (const char* p = str; *p; p++) {
      if (*p == '\n') { cy++; col = cx; continue; }
      if (col >= 0 && col < COLS && cy >= 0 && cy < ROWS) {
        _text[cy][col] = (*p >= 32 && *p < 127) ? *p : '?';
      }
      col++;
    }
    // advance cursor horizontally by the printed width, like a real display
    _cursor_x += getTextWidth(str);
  }

  void fillRect(int x, int y, int w, int h) override { markRect(x, y, w, h); }
  void drawRect(int x, int y, int w, int h) override {
    markRect(x, y, w, 1);
    markRect(x, y + h - 1, w, 1);
    markRect(x, y, 1, h);
    markRect(x + w - 1, y, 1, h);
  }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    markRect(x, y, w, h);   // icon placeholder: solid block (see class comment)
  }

  uint16_t getTextWidth(const char* str) override {
    return str ? (uint16_t)(strlen(str) * getCharWidth()) : 0;
  }

  void endFrame() {
    printf("\n===== SimDisplayDriver frame #%d =====\n", _frame_no++);
    printf("+");
    for (int c = 0; c < COLS; c++) printf("-");
    printf("+\n");
    for (int r = 0; r < ROWS; r++) {
      printf("|");
      for (int c = 0; c < COLS; c++) {
        char ch = _text[r][c];
        if (ch) putchar(ch);
        else if (_filled[r][c]) putchar('#');
        else putchar(' ');
      }
      printf("|\n");
    }
    printf("+");
    for (int c = 0; c < COLS; c++) printf("-");
    printf("+\n");
    fflush(stdout);
  }

private:
  void markRect(int x, int y, int w, int h) {
    bool lit = (_color != DARK);
    int cx0, cy0, cx1, cy1;
    cellOf(x, y, cx0, cy0);
    cellOf(x + w - 1, y + h - 1, cx1, cy1);
    for (int r = cy0; r <= cy1; r++) {
      if (r < 0 || r >= ROWS) continue;
      for (int c = cx0; c <= cx1; c++) {
        if (c < 0 || c >= COLS) continue;
        _filled[r][c] = lit;
      }
    }
  }
};

#ifdef __EMSCRIPTEN__
// ---------------------------------------------------------------------
// Phase 2 (Emscripten): DisplayDriver backend that draws to a real
// HTML5 <canvas> instead of dumping ASCII art to stdout. SimDisplayDriver
// above is left completely untouched -- the native build still links that
// class (see variants/sim/platformio.ini's DISPLAY_CLASS=SimDisplayDriver);
// this class is only selected when DISPLAY_CLASS=SimDisplayDriverCanvas is
// set by the Emscripten build (variants/sim/build_wasm.sh).
//
// Design choice: draw straight through the browser's canvas 2D API on every
// draw call (fillRect/strokeRect/fillText), rather than building an offscreen
// RGBA framebuffer in linear memory and blitting it with putImageData(). The
// canvas 2D approach was simpler and more robust for this app's actual draw
// call shape:
//   - print() needs real text rendering (variable glyphs, marquee/ellipsis
//     logic in DisplayDriver.h measures via getTextWidth()) -- letting the
//     browser's own font rasterizer draw it is both less code and crisper
//     than hand-rolling a bitmap font + blit.
//   - Every draw happens synchronously within one call to startFrame()..
//     endFrame() inside a single JS "tick" (called from the Emscripten main
//     loop -- see sim_main.cpp) -- the browser never paints a partial canvas
//     mid-tick, so there's no tearing/flicker risk from not double-buffering
///    in C++ first.
//   - putImageData() would still need *something* to rasterize text and
//     icons into an RGBA buffer first -- it doesn't remove that work, it
///    only relocates it into C++ for no real benefit here.
//
// Each call reaches into the DOM via EM_ASM (synchronous, main-thread JS --
// fine since this build has no pthreads/proxying). The canvas is looked up
// by id once in begin() and cached on a per-instance JS property (see below)
// so every later call is one property read, not a fresh getElementById().
//
// Phase 3 addendum: cached on Module.__simCtx, NOT window.__simCtx as this
// class originally did in Phase 2. Phase 2 only ever ran one instance on a
// page, so a plain `window` global was invisible/harmless as a design smell;
// Phase 3 loads multiple MeshCoreSim()/MeshCoreSimRepeater() instances on
// ONE page, and `window` is the single real browser global shared by every
// one of them (MODULARIZE isolates each instance's own Module/wasm linear
// memory, but NOT the DOM/window) -- two instances' begin() calls would
// stomp the same window.__simCtx in turn, and both would end up drawing
// through whichever one won. `Module` itself, by contrast, IS a distinct
// object per instance (that's the whole point of MODULARIZE) and is already
// reachable from inside EM_ASM here as the current instance's own Module
// (same access pattern SimFS.h's sim_fs_mount_idbfs()/SimInstance.h's
// sim_instance_salt() already rely on for Module['simInstanceTag']), so
// storing it there instead scopes it correctly per instance for free.
//
// The canvas element id is ALSO made per-instance the same way: an untagged
// instance (no Module['simInstanceTag'], e.g. Phase 2's original
// single-instance web/index.html harness) still looks for plain
// "sim-canvas", byte-for-byte the pre-Phase-3 behavior; a tagged instance
// (Module['simInstanceTag'] = 'A', from a Phase 3 multi-instance host page
// like web/mesh.html) looks for "sim-canvas-A" instead, so two instances on
// one page never fight over the same <canvas> element either.
class SimDisplayDriverCanvas : public DisplayDriver {
  bool _on = false;
  int _cursor_x = 0, _cursor_y = 0;
  Color _color = LIGHT;

public:
  SimDisplayDriverCanvas() : DisplayDriver(128, 64) { }

  bool begin() {
    _on = true;
    EM_ASM({
      var tag = (typeof Module !== 'undefined' && Module['simInstanceTag']) ? Module['simInstanceTag'] : '';
      var id = tag ? ('sim-canvas-' + tag) : 'sim-canvas';
      var c = document.getElementById(id);
      if (!c) { console.error('[sim] #' + id + ' not found in the host page'); return; }
      Module.__simCtx = c.getContext('2d');
      Module.__simCtx.imageSmoothingEnabled = false;
    });
    return true;
  }

  bool isOn() override { return _on; }
  void turnOn() override { _on = true; }
  void turnOff() override {
    _on = false;
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simCtx.fillStyle = '#000';
      Module.__simCtx.fillRect(0, 0, 128, 64);
    });
  }
  void clear() override { turnOff(); _on = true; }

  void startFrame(Color bkg = DARK) override {
    _color = LIGHT;
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simCtx.fillStyle = '#000';
      Module.__simCtx.fillRect(0, 0, 128, 64);
    });
  }

  void setTextSize(int sz) override { /* one fixed size, like the native ASCII backend */ }
  void setColor(Color c) override { _color = c; }
  void setCursor(int x, int y) override { _cursor_x = x; _cursor_y = y; }

  // Amber-on-black palette (a common OLED look) for LIGHT/DARK; the other
  // Color enumerators (RED/GREEN/BLUE/YELLOW/ORANGE) aren't used on the real
  // monochrome OLED boards this sim mirrors either (DisplayDriver.h's own
  // comment: "on b/w screen, colors will be !=0 synonym of light").
  static const char* jsColor(Color c) { return c == DARK ? "#000" : "#ffb000"; }

  void print(const char* str) override {
    if (!str) return;
    EM_ASM({
      if (!Module.__simCtx) return;
      var ctx = Module.__simCtx;
      ctx.fillStyle = UTF8ToString($3) === 'L' ? '#ffb000' : '#000';
      ctx.font = '8px monospace';
      ctx.textBaseline = 'top';
      // Advance width is fixed (getCharWidth()==6, DisplayDriver.h default) --
      // draw one character per cell so glyph spacing matches the layout math
      // every screen already does off getTextWidth()'s strlen()*6 estimate,
      // instead of leaving it to the font's own (proportional) metrics.
      // NB: EM_ASM's argument-splitting only understands parens, not
      // braces -- an unparenthesized top-level comma (e.g. a multi-name
      // `var a, b;`) gets misread as separating this macro's own C++
      // arguments and breaks the whole block. Every declaration below is
      // therefore its own separate `var` statement.
      var s = UTF8ToString($0);
      var x = $1;
      var y = $2;
      for (var i = 0; i < s.length; i++) {
        ctx.fillText(s[i], x + i * 6, y);
      }
    }, str, _cursor_x, _cursor_y, (_color != DARK) ? "L" : "D");
    _cursor_x += getTextWidth(str);
  }

  void fillRect(int x, int y, int w, int h) override {
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simCtx.fillStyle = UTF8ToString($4) === 'L' ? '#ffb000' : '#000';
      Module.__simCtx.fillRect($0, $1, $2, $3);
    }, x, y, w, h, (_color != DARK) ? "L" : "D");
  }

  void drawRect(int x, int y, int w, int h) override {
    EM_ASM({
      if (!Module.__simCtx) return;
      var ctx = Module.__simCtx;
      ctx.strokeStyle = UTF8ToString($4) === 'L' ? '#ffb000' : '#000';
      ctx.lineWidth = 1;
      ctx.strokeRect($0 + 0.5, $1 + 0.5, $2 - 1, $3 - 1);
    }, x, y, w, h, (_color != DARK) ? "L" : "D");
  }

  // Real XBM bit-unpacking (row-major, MSB-first, rows padded to whole
  // bytes) -- same convention every real DisplayDriver's drawXbm() already
  // assumes (see e.g. src/helpers/ui/ST7789Display.cpp's own drawXbm(),
  // `0x80 >> (bx & 7)` against `widthInBytes = (w+7)/8`). A canvas can
  // afford to rasterize the real icon pixels cheaply, unlike the native
  // ASCII backend's solid-block placeholder (no ASCII resolution for that).
  // `bits` is a pointer into wasm linear memory; EM_ASM passes it through as
  // a plain integer and the JS side indexes HEAPU8 with it directly.
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    EM_ASM({
      if (!Module.__simCtx) return;
      var ctx = Module.__simCtx;
      var x0 = $0;
      var y0 = $1;
      var w = $2;
      var h = $3;
      var bits = $4;
      var lit = UTF8ToString($5) === 'L';
      var widthInBytes = (w + 7) >> 3;
      ctx.fillStyle = lit ? '#ffb000' : '#000';
      for (var ry = 0; ry < h; ry++) {
        for (var rx = 0; rx < w; rx++) {
          var byteOff = bits + ry * widthInBytes + (rx >> 3);
          var mask = 0x80 >> (rx & 7);
          if (HEAPU8[byteOff] & mask) ctx.fillRect(x0 + rx, y0 + ry, 1, 1);
        }
      }
    }, x, y, w, h, bits, (_color != DARK) ? "L" : "D");
  }

  uint16_t getTextWidth(const char* str) override {
    return str ? (uint16_t)(strlen(str) * getCharWidth()) : 0;
  }

  // Every draw call above already lands directly on the visible canvas
  // (see the class comment) -- nothing left to flush.
  void endFrame() override { }
};
#endif // __EMSCRIPTEN__
