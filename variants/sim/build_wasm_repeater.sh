#!/usr/bin/env bash
# Phase 3 (Emscripten) build script for the examples/simple_repeater sim --
# sibling of build_wasm.sh (the companion_radio one), NOT a modification of
# it, so the existing companion_radio build stays byte-for-byte untouched
# (see the "hard constraints" in the Phase 3 plan: build_wasm.sh must keep
# working exactly as before).
#
# Differences from build_wasm.sh, all deliberate:
#   - examples/simple_repeater/{main,MyMesh}.cpp instead of
#     examples/companion_radio/{main,MyMesh,DataStore}.cpp +
#     ui-new/UITask.cpp -- no UI/display/BLE in this app at all, and
#     UITask.cpp specifically does NOT compile cleanly with no display
#     defined (real Arduino GPIO constants used unconditionally -- see
#     variants/sim/platformio.ini's [env:sim_simple_repeater] comment for
#     the same issue hit there), so it's excluded, matching that env.
#   - No -DDISPLAY_CLASS -- headless, matching every real hardware
#     repeater env's convention when no display is fitted.
#   - -DSIM_FS_ROOT=\"/sim_data_repeater\" -- must match the
#     "./sim_data_repeater" SimFS root examples/simple_repeater/main.cpp's
#     own SIM_PLATFORM branch constructs (see variants/sim/sim_main.cpp),
#     kept DIFFERENT from companion_radio's "/sim_data" so a repeater
#     instance's IDBFS-backed identity storage can never collide with a
#     companion instance's, even on the same page/origin.
#   - -sEXPORT_NAME=MeshCoreSimRepeater (not MeshCoreSim) -- the host page
#     loads both build/meshcore_sim.js (build/) and
#     build/repeater/meshcore_sim_repeater.js side by side; each is its own
#     MODULARIZE factory function under a distinct global name so loading
#     both on one page can't collide.
#   - Output lands under web/build/repeater/, not web/build/, so the two
#     builds' .wasm/.js/obj/ trees never share a directory.
#
# Usage:
#   variants/sim/build_wasm_repeater.sh            # release-ish build (-O2)
#   variants/sim/build_wasm_repeater.sh debug       # -O0 -g
#
# Same emsdk 6.0.9 requirement as build_wasm.sh -- see that script's header
# comment for the install command if variants/sim/tools/emsdk/ is missing.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
EMSDK_DIR="$SCRIPT_DIR/tools/emsdk"
EMXX="$EMSDK_DIR/upstream/emscripten/em++"
OUT_DIR="$SCRIPT_DIR/web/build/repeater"

if [ ! -x "$EMXX" ]; then
  echo "error: em++ not found at $EMXX" >&2
  echo "Install it first:" >&2
  echo "  cd $EMSDK_DIR && python3 ./emsdk.py install 6.0.9 && python3 ./emsdk.py activate 6.0.9" >&2
  exit 1
fi

BUILD_MODE="${1:-release}"
if [ "$BUILD_MODE" = "debug" ]; then
  OPT_FLAGS=(-O0 -g)
else
  OPT_FLAGS=(-O2)
fi

mkdir -p "$OUT_DIR"
cd "$REPO_ROOT"

# Same list as variants/sim/platformio.ini's [env:sim_simple_repeater]
# build_src_filter, spelled as real paths. Keep in sync with that file by
# hand (same convention build_wasm.sh already established for the
# companion_radio side).
SRCS=(
  src/Dispatcher.cpp
  src/Identity.cpp
  src/Mesh.cpp
  src/Packet.cpp
  src/Utils.cpp
  src/helpers/AdvertDataHelpers.cpp
  src/helpers/BaseChatMesh.cpp
  src/helpers/ClientACL.cpp
  src/helpers/CommonCLI.cpp
  src/helpers/ConfigSerializer.cpp
  src/helpers/DeviceDiag.cpp
  src/helpers/IdentityStore.cpp
  src/helpers/RegionMap.cpp
  src/helpers/StaticPoolPacketManager.cpp
  src/helpers/TransportKeyStore.cpp
  src/helpers/TxtDataHelpers.cpp
  lib/ed25519/add_scalar.c
  lib/ed25519/fe.c
  lib/ed25519/ge.c
  lib/ed25519/key_exchange.c
  lib/ed25519/keypair.c
  lib/ed25519/sc.c
  lib/ed25519/seed.c
  lib/ed25519/sha512.c
  lib/ed25519/sign.c
  lib/ed25519/verify.c
  variants/sim/sim_main.cpp
  variants/sim/target.cpp
  variants/sim/thirdparty/crypto/AES128.cpp
  variants/sim/thirdparty/crypto/AESCommon.cpp
  variants/sim/thirdparty/crypto/BigNumberUtil.cpp
  variants/sim/thirdparty/crypto/BlockCipher.cpp
  variants/sim/thirdparty/crypto/Crypto.cpp
  variants/sim/thirdparty/crypto/Curve25519.cpp
  variants/sim/thirdparty/crypto/Ed25519.cpp
  variants/sim/thirdparty/crypto/Hash.cpp
  variants/sim/thirdparty/crypto/rng_stub.cpp
  variants/sim/thirdparty/crypto/SHA256.cpp
  variants/sim/thirdparty/crypto/SHA512.cpp
  variants/sim/thirdparty/cayennelpp/CayenneLPP.cpp
  variants/sim/thirdparty/cayennelpp/CayenneLPPPolyline.cpp
  examples/simple_repeater/main.cpp
  examples/simple_repeater/MyMesh.cpp
)

INCLUDES=(
  -Ivariants/sim/arduino
  -Ivariants/sim
  -Ivariants/sim/thirdparty/crypto
  -Ivariants/sim/thirdparty/cayennelpp
  -Ivariants/sim/thirdparty/arduinojson
  -Ilib/ed25519
  -Isrc
  -Iexamples/simple_repeater
)

DEFINES=(
  -DSIM_PLATFORM
  -DMESH_DEBUG=0
  -DENABLE_ADVERT_ON_BOOT=1
  -DSIM_FS_ROOT="\"/sim_data_repeater\""
)

# -funsigned-char: same reasoning as build_wasm.sh (KEY_* codes aren't
# actually used by this app at all -- no UI -- but every other sim TU is
# compiled with this flag and mixing char signedness across TUs that share
# struct layouts/bitfields is asking for trouble, so keep it uniform).
COMMON_FLAGS=(-std=c++17 -funsigned-char "${OPT_FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}")

# Mirror each source's own directory under obj/ (see build_wasm.sh's own
# comment on this -- the sha512.o/SHA512.o macOS case-collision reason
# applies identically here since it's the same source tree).
OBJ_DIR="$OUT_DIR/obj"
rm -rf "$OBJ_DIR"
OBJS=()
for src in "${SRCS[@]}"; do
  obj="$OBJ_DIR/${src%.*}.o"
  mkdir -p "$(dirname "$obj")"
  "$EMXX" -c "${COMMON_FLAGS[@]}" "$src" -o "$obj"
  OBJS+=("$obj")
done

"$EMXX" \
  "${OBJS[@]}" \
  -lidbfs.js \
  -sALLOW_MEMORY_GROWTH=1 \
  -sFORCE_FILESYSTEM=1 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=MeshCoreSimRepeater \
  -sENVIRONMENT=web \
  -sEXIT_RUNTIME=0 \
  -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,HEAPU8 \
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
  -o "$OUT_DIR/meshcore_sim_repeater.js"

echo ""
echo "Built: $OUT_DIR/meshcore_sim_repeater.js (+ .wasm alongside it)"
