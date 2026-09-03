#pragma once

#include <cstdint>

// Phase 3: per-instance "salt" for the sim build.
//
// The two-companion-radio + one-repeater browser demo loads the SAME
// compiled companion_radio.wasm/js twice (see build_wasm.sh -- there is no
// separate "instance A" vs "instance B" build, by design: MODULARIZE gives
// each MeshCoreSim() call its own independent globals/linear memory, so one
// binary genuinely serves both roles). That means nothing can be baked in
// at compile time to tell the two apart -- any per-instance difference has
// to come from the host page, passed in as a plain JS value on the Module
// config object *before* that instance's factory promise resolves (e.g.
// MeshCoreSim({ simInstanceTag: 'A' })), and read back from C++ only at
// *runtime* (never at static-init time -- global C++ constructors run
// before any JS-supplied Module config is reachable from generated code,
// confirmed by a standalone getenv()-via-Module.ENV experiment during this
// phase that came back null; Module.arguments/argv has the same timing
// problem for the same reason). Every call site below only ever runs from
// setup()-time code (never a global/static initializer), so this is safe.
//
// Used for two unrelated purposes that both care about the two instances
// NOT looking identical to each other:
//   1. SimRNG::begin() / SimRadio::getRngSeed() -- without this, two
//      instances started in the same browser tick could produce the exact
//      same seed (same wall-clock second, same `rand()` state, and often
//      the same `this` pointer value across independent-but-identically-
//      laid-out linear memories), which would hand both simulated devices
//      the same Ed25519 identity. Mixing in a per-instance tag makes that
//      collision a non-issue regardless of whether the timing/address
//      coincidence happens.
//   2. SimFS.h's sim_fs_mount_idbfs() namespacing the real IndexedDB
//      database per instance so instance A/B don't silently share
//      persisted storage on a page reload (see the long comment there).
//
// Native (and any wasm build that never sets simInstanceTag) gets salt 0,
// i.e. exactly the pre-Phase-3 behavior -- single-instance builds are
// unaffected.
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

inline uint32_t sim_instance_salt() {
  return (uint32_t)EM_ASM_INT({
    var t = (typeof Module !== 'undefined' && Module['simInstanceTag']) ? Module['simInstanceTag'] : '';
    var h = 2166136261;   // FNV-1a, plain JS numbers (>>> 0 keeps it unsigned 32-bit)
    for (var i = 0; i < t.length; i++) {
      h = h ^ t.charCodeAt(i);
      h = (h * 16777619) >>> 0;
    }
    return h >>> 0;
  });
}
#else
inline uint32_t sim_instance_salt() { return 0; }
#endif
