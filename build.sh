#!/usr/bin/env bash
# The build: Config Live from this project's sources on the RXDK-SDK libraries.
# (The hybrid build of stages 0-1 has been deleted.)
#   our sources:   this project's flags (library entry points: lib_sdk.h, sdk_shim.h)
#   src/sdk/sdk_*: the SDK's own compile flags and headers (shims, kernel import slots)
#   link:          Rxdk.Cli link-exe (entry XapiTitleStartup -> main in src/sdk/x3_misc.c)
#   XBE:           imagebld, with the original's title id and init flags
#   BIOS:          bios/X3_3294_2MB.bin with its Config Live replaced -> X3_ConfigLive_2MB.bin
# usage: ./build.sh   (RX_OUT: output dir, default out; EXTRA_CFLAGS: extra defines)
set -euo pipefail
cd "$(dirname "$0")"
RXDK=${RXDK:-C:/ProgramData/RXDK}  # RXDK root (llvm/, sdk/, tools/), as scripts/stage-rxdk.ps1 lays it out
RXDK=${RXDK//\\//}
CL=$RXDK/llvm/xbox-windows-x64/bin/clang.exe
CLI=$RXDK/tools/Rxdk.Cli.exe
OUT=${RX_OUT:-out}
mkdir -p "$OUT/obj"
# no stale outputs: a failed step must not leave the previous build looking current
rm -f "$OUT/ConfigLive.exe" "$OUT/ConfigLive.xbe" "$OUT/X3_ConfigLive_2MB.bin"

T="-target i686-pc-windows-gnu -march=pentium3"
CFLAGS="${EXTRA_CFLAGS:-} -ffreestanding -fno-builtin -fno-stack-protector -O2 -Wall -Iinclude"
mapfile -t SDKFLAGS < <($CLI compile-args --lang c | tr -d '\r')

OBJS=()
while IFS= read -r src; do
    obj="$OUT/obj/${src%.c}.o"
    mkdir -p "$(dirname "$obj")"
    case "$src" in
    src/sdk/sdk_*) $CL "${SDKFLAGS[@]}" -O2 -Wno-pragma-pack -c "$src" -o "$obj" ;;
    *) $CL $T $CFLAGS -c "$src" -o "$obj" ;;
    esac
    OBJS+=("$obj")
done < <(find src -name '*.c' | sort)

MSYS_NO_PATHCONV=1 $CLI link-exe -o "$OUT/ConfigLive.exe" "${OBJS[@]}" \
    libxgraphics.lib libd3dx8.lib libd3d8.lib libxnet.lib libxapi.lib libcpp.lib libc.lib libcompat.lib libkernel.lib \
    | grep -v '^\$ ' || true
[ -f "$OUT/ConfigLive.exe" ] || { echo "link failed"; exit 1; }

# the original's certificate title id and init flags (x3control.xbe)
MSYS_NO_PATHCONV=1 $RXDK/tools/imagebld.exe /IN:"$OUT/ConfigLive.exe" /OUT:"$OUT/ConfigLive.xbe" \
    /STACK:65536 /INITFLAGS:0x18 /TESTID:0xF3EBA821 /TESTNAME:"X3 Config Live" /NOLIBWARN
[ -f "$OUT/ConfigLive.xbe" ] || { echo "imagebld failed"; exit 1; }
echo "built $OUT/ConfigLive.xbe"

# the X3 BIOS with this Config Live in its flash slot (bios/pack_bios.py; only the slot changes)
python -I bios/pack_bios.py bios/X3_3294_2MB.bin "$OUT/ConfigLive.xbe" "$OUT/X3_ConfigLive_2MB.bin"
