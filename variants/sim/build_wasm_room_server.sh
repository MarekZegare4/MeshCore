#!/usr/bin/env bash
# Emscripten build script for the examples/simple_room_server sim -- sibling
# of build_wasm.sh (companion_radio) and build_wasm_repeater.sh
# (simple_repeater), NOT a modification of either.
#
# Differences from build_wasm_repeater.sh, all deliberate:
#   - examples/simple_room_server/{main,MyMesh}.cpp instead of
#     examples/simple_repeater/{main,MyMesh}.cpp -- UITask.cpp is likewise
#     excluded (not added to SRCS at all) since no -DDISPLAY_CLASS is
#     defined here, matching every real headless room-server env.
#   - -DSIM_FS_ROOT=\"/sim_data_room\" -- must match the "./sim_data_room"
#     SimFS root examples/simple_room_server/main.cpp's own SIM_PLATFORM
#     branch constructs (see variants/sim/sim_main.cpp), kept DIFFERENT from
#     both companion_radio's "/sim_data" and simple_repeater's
#     "/sim_data_repeater" so identities can never collide on one page.
#   - -sEXPORT_NAME=MeshCoreSimRoomServer -- its own MODULARIZE factory
#     name, distinct from MeshCoreSim/MeshCoreSimRepeater.
#   - Output lands under web/build/room_server/.
#
# Usage:
#   variants/sim/build_wasm_room_server.sh            # release-ish build (-O2)
#   variants/sim/build_wasm_room_server.sh debug       # -O0 -g
#
# Same emsdk 6.0.9 requirement as build_wasm.sh -- see that script's header
# comment for the install command if variants/sim/tools/emsdk/ is missing.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
EMSDK_DIR="$SCRIPT_DIR/tools/emsdk"
EMXX="$EMSDK_DIR/upstream/emscripten/em++"
OUT_DIR="$SCRIPT_DIR/web/build/room_server"

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

# Same list as variants/sim/platformio.ini's [env:sim_simple_room_server]
# build_src_filter, spelled as real paths.
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
  examples/simple_room_server/main.cpp
  examples/simple_room_server/MyMesh.cpp
)

INCLUDES=(
  -Ivariants/sim/arduino
  -Ivariants/sim
  -Ivariants/sim/thirdparty/crypto
  -Ivariants/sim/thirdparty/cayennelpp
  -Ivariants/sim/thirdparty/arduinojson
  -Ilib/ed25519
  -Isrc
  -Iexamples/simple_room_server
)

DEFINES=(
  -DSIM_PLATFORM
  -DMESH_DEBUG=0
  -DENABLE_ADVERT_ON_BOOT=1
  -DADVERT_NAME="\"Sim Room\""
  -DSIM_FS_ROOT="\"/sim_data_room\""
)

# -funsigned-char: same reasoning as build_wasm.sh/build_wasm_repeater.sh --
# keep it uniform across every sim TU even though this app has no UI either.
COMMON_FLAGS=(-std=c++17 -funsigned-char "${OPT_FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}")

# Mirror each source's own directory under obj/ -- same macOS APFS
# case-insensitive sha512.o/SHA512.o collision reason as the other two
# build scripts (same source tree).
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
  -sEXPORT_NAME=MeshCoreSimRoomServer \
  -sENVIRONMENT=web \
  -sEXIT_RUNTIME=0 \
  -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,HEAPU8 \
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
  -o "$OUT_DIR/meshcore_sim_room_server.js"

echo ""
echo "Built: $OUT_DIR/meshcore_sim_room_server.js (+ .wasm alongside it)"
