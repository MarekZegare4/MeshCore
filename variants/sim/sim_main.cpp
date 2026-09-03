// Process/runtime entry point for the sim build. Real Arduino cores provide
// their own main() (call setup() once, then loop() forever); platform=native
// has no such core, so this is that main() for both Phase 1's native target
// and Phase 2's Emscripten target -- the two are different enough (a native
// process owns its own loop and a real stdin tty; a wasm module in a browser
// tab must hand control back to the browser's event loop between ticks, and
// has no stdin at all) that they get fully separate #ifdef __EMSCRIPTEN__
// branches below rather than one branch trying to cover both.
#include <Arduino.h>
#include <cstdio>
#include <cstdlib>

// Defined by examples/companion_radio/main.cpp (Arduino sketch convention:
// no header declares these, every board's own entry point just forward-
// declares and calls them).
void setup();
void loop();

#ifdef __EMSCRIPTEN__
// ---------------------------------------------------------------------
// Phase 2: Emscripten/browser entry point.
#include <emscripten.h>
#include "SimFS.h"   // sim_fs_mount_idbfs()

// One tick of the real app's cooperative loop -- identical body to
// examples/companion_radio/main.cpp's own loop() (the_mesh.loop();
// sensors.loop(); ui_task.loop(); rtc_clock.tick(); ...), so this wrapper
// adds nothing of its own; it exists only because emscripten_set_main_loop
// wants a void(void) function pointer, and passing `loop` directly would
// also work here, but naming it makes the call below self-documenting.
static void sim_main_loop_tick() {
  loop();
}

// Called back from JS (see SimFS.h's sim_fs_mount_idbfs()) once the async
// IndexedDB -> MEMFS pull finishes. This is where the app's boot sequence
// actually starts -- deliberately NOT in main() itself, since main() must
// return (or hand off via emscripten_exit_with_live_runtime()) long before
// this callback ever fires.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_idbfs_ready() {
  setup();
  // fps=0, simulate_infinite_loop=1: let the browser's own
  // requestAnimationFrame cadence drive ticks (Emscripten's documented
  // recommendation for anything drawing to a <canvas>) rather than a fixed
  // interval -- ties the sim's tick rate to the actual display refresh, and
  // it's throttled/paused for free by the browser when the tab is hidden or
  // backgrounded, which a manual setInterval(..., fixed_ms) wouldn't get.
  emscripten_set_main_loop(sim_main_loop_tick, 0, 1);
}

// The app-level SimFS root to mount IDBFS at -- must match whatever
// relative "./sim_data..." path that app's own main.cpp constructs its
// SimFS with (see the long comment on sim_fs_mount_idbfs() in SimFS.h for
// why those are the same filesystem node under Emscripten's default cwd,
// "/"). Defaults to companion_radio's root, unchanged from Phase 2 --
// Phase 3's simple_repeater build (variants/sim/build_wasm_repeater.sh)
// overrides this via -DSIM_FS_ROOT so its identity storage lands under a
// DIFFERENT IDBFS-backed root than a companion instance's, same reasoning
// as examples/simple_repeater/main.cpp's SIM_PLATFORM branch using
// "./sim_data_repeater" instead of "./sim_data" for its SimFS.
#ifndef SIM_FS_ROOT
#define SIM_FS_ROOT "/sim_data"
#endif

int main() {
  printf("MeshCore sim (wasm) starting -- mounting IDBFS at " SIM_FS_ROOT "...\n");
  sim_fs_mount_idbfs(SIM_FS_ROOT);
  // Keep the runtime alive after main() returns instead of tearing it down
  // (the default for a `main()` that returns under Emscripten) -- the real
  // boot sequence hasn't happened yet, it's waiting on sim_idbfs_ready()
  // above, an async JS callback that fires well after main() itself is done.
  emscripten_exit_with_live_runtime();
  return 0;
}

#else
// ---------------------------------------------------------------------
// Phase 1: native (host process) entry point -- unchanged from the
// original Phase 1 implementation. Puts the terminal into raw/
// non-canonical mode so UITask.cpp's SIM_PLATFORM stdin-poll branch (see
// UITask::loop()) receives individual keystrokes immediately instead of
// once per Enter-terminated line.
#include <unistd.h>
#include <termios.h>

static struct termios g_orig_termios;
static bool g_termios_saved = false;

static void restoreTerminal() {
  if (g_termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

static void setupTerminal() {
  if (!isatty(STDIN_FILENO)) return;   // piped/redirected stdin -- leave alone
  if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) return;
  g_termios_saved = true;
  atexit(restoreTerminal);

  struct termios raw = g_orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);   // no line buffering, no local echo
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

int main() {
  setupTerminal();
  printf("MeshCore sim (native) -- Ctrl-C to quit.\n");
  printf("Keys: arrows/WASD move, Enter/Space select, Esc/Backspace cancel, n/p next/prev.\n\n");

  setup();
  for (;;) {
    loop();
    // Real boards spin their loop() as fast as the hardware allows too;
    // this just keeps a native process from pegging a CPU core at 100%
    // for no benefit -- 1ms is well under any UI timing this app cares
    // about (refresh/animation intervals are tens-to-hundreds of ms).
    usleep(1000);
  }
  return 0;
}
#endif // __EMSCRIPTEN__
