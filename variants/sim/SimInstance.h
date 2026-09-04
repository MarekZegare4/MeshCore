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

// meshcore-solo-site's WebSocket relay bridge (Phase 3) exposed a real gap
// in sim_instance_salt() alone: it's a pure function of the simInstanceTag
// STRING ('hero'/'B'/'R'), so it's only ever useful for telling apart
// same-tab instances that use different tags -- it's the SAME value every
// time for two genuinely different browser tabs/machines that both boot a
// 'hero' instance, which is exactly the new cross-visitor case. Combined
// with SimRNG::begin()'s other two seed ingredients -- time(NULL) (1-second
// resolution: two real visitors loading the page in the same second collide
// outright) and `(uintptr_t)this` (a WASM linear-memory address, which is
// fully deterministic across independent boots of the same binary doing the
// same allocation sequence -- there's no ASLR inside a wasm sandbox, so this
// contributes zero actual entropy, not "usually" different) -- two distinct
// real visitors landing on the exact same wall-clock second reliably
// produced byte-identical generated Ed25519 identities (confirmed while
// testing the relay bridge: two independent, freshly-IDBFS browser contexts
// launched together produced provably identical advert packets end to end).
// A host page now passes one genuinely random value from the one place that
// actually has real entropy per browser session -- crypto.getRandomValues()
// -- as `simEntropy` on the Module config object (see meshcore-solo-site's
// bootInstance()/bootRepeater()), read back here the same way
// simInstanceTag already is. Defaults to 0 (this function's old, sole
// behavior) if a host page doesn't set it, so nothing else changes.
inline uint32_t sim_instance_entropy() {
  return (uint32_t)EM_ASM_INT({
    return (typeof Module !== 'undefined' && Module['simEntropy']) ? (Module['simEntropy'] >>> 0) : 0;
  });
}
#else
inline uint32_t sim_instance_salt() { return 0; }
inline uint32_t sim_instance_entropy() { return 0; }
#endif
