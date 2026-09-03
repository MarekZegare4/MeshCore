#include <Arduino.h>
#include <cstring>
#include "target.h"
#include "SimRNG.h"

// Global stdout-backed Serial object, declared extern in Arduino.h.
SimSerialClass Serial;

SimMainBoard board;
SimRadio radio_driver;
SimRTCClock rtc_clock;
SensorManager sensors;   // base class: no real sensors in Phase 1

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
#endif

bool radio_init() {
  // No real radio hardware to initialise -- always succeeds (see
  // variants/sim/SimRadio.h; Phase 3 of the sim plan is where two SimRadio
  // instances actually exchange bytes through a shared in-memory "ether").
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  static SimRNG rng;
  rng.begin();
  return mesh::LocalIdentity(&rng);
}

#ifdef __EMSCRIPTEN__
// Real bitmap-font text rendering for SimDisplayDriverCanvas::print()
// (declared in SimDisplayDriver.h, defined here -- the one TU allowed to
// include MiscFixedRenderer.h; see that header's own "include only from a
// .cpp" comment). Uses the exact same font tables + glyph-plotting math a
// real MeshCore-Solo board renders with OLED_MISC_FIXED_FONT=1 (see
// solo/heltec_v3/platformio.ini), instead of the browser's own system font.
// Adafruit_GFX.h/.cpp branch on `#if ARDUINO >= 100` in exactly two spots
// (which Arduino.h to #include, and whether write(uint8_t) returns size_t
// or void) -- our own Arduino.h shim deliberately never defines ARDUINO
// globally (other vendored libs branch on #ifdef ARDUINO to pick their
// portable std:: path instead of Arduino String/Stream), so define it here,
// scoped to this one translation unit, purely so Adafruit_GFX picks the
// modern branch that actually matches our Print.h shim's
// `size_t write(uint8_t)` signature.
#define ARDUINO 100
#include <Adafruit_GFX.h>
#include <helpers/ui/MiscFixedRenderer.h>

// Adafruit_GFX subclass that plots into a plain 128x64 byte buffer (one
// pixel per byte, 0/1) instead of real display hardware -- drawPixel() is
// the only thing MiscFixedRenderer.h's glyph-plotting code and
// Adafruit_GFX's own fillRect()/writeFillRect() (used for the "unmapped
// codepoint" substitution box) ultimately call down to.
class SimGfxCanvas : public Adafruit_GFX {
public:
  uint8_t px[128 * 64];
  SimGfxCanvas() : Adafruit_GFX(128, 64) { memset(px, 0, sizeof(px)); }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if ((unsigned)x >= 128 || (unsigned)y >= 64) return;
    px[y * 128 + x] = (color != 0) ? 1 : 0;
  }
};

void SimDisplayDriverCanvas::print(const char* str) {
  if (!str) return;
  static SimGfxCanvas gfx;
  memset(gfx.px, 0, sizeof(gfx.px));
  gfx.setCursor(_cursor_x, _cursor_y);
  // color arg is just our own internal "lit" marker (1) -- the real on-screen
  // amber/black choice is applied once at blit time below, from _color, same
  // as every other primitive in this class.
  miscFixedPrint(gfx, str, 1, 1);

  // startFrame() already blanks the whole canvas to black every frame, so
  // only the lit pixels need drawing here -- unlit buffer cells are already
  // correct background. One EM_ASM call blits the whole 128x64 buffer
  // (reading it directly out of wasm memory, same pattern as drawXbm()
  // below) rather than one call per glyph pixel.
  EM_ASM({
    if (!Module.__simCtx) return;
    var ctx = Module.__simCtx;
    var buf = $0;
    ctx.fillStyle = UTF8ToString($1) === 'L' ? '#ffb000' : '#000';
    for (var y = 0; y < 64; y++) {
      for (var x = 0; x < 128; x++) {
        if (HEAPU8[buf + y * 128 + x]) ctx.fillRect(x, y, 1, 1);
      }
    }
  }, gfx.px, (_color != DARK) ? "L" : "D");

  // Same external contract as every other DisplayDriver backend here (see
  // SimDisplayDriver's own ASCII print()): only _cursor_x advances by the
  // printed width; _cursor_y is left for the caller to manage via
  // setCursor() between lines, even though miscFixedPrint() itself tracks a
  // real y position across embedded '\n's internally.
  _cursor_x += getTextWidth(str);
}
#endif // __EMSCRIPTEN__
