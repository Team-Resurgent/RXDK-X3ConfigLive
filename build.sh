#!/usr/bin/env bash
# The build: Config Live from this project's sources on the RXDK-SDK libraries.
#   our sources:   this project's flags (library entry points: lib_sdk.h, sdk_shim.h)
#   src/sdk/sdk_*: the SDK's own compile flags and headers (shims, kernel import slots)
#   link:          Rxdk.Cli link-exe (entry XapiTitleStartup -> main in src/sdk/x3_misc.c)
#   XBE:           imagebld, with the original's title id and init flags
#   BIOS:          bios/X3_3294_2MB.bin with its Config Live replaced -> X3_ConfigLive_2MB.bin
# Two variants: ConfigLive.xbe keeps the settings in E:/x3config.bin (4026); the BIOS gets a
# second link with X3_BIOS_BUILD (settings in the X3 kernel / EEPROM, autostart as 3294). Only
# the sources that test X3_BIOS_BUILD are compiled twice.
# usage: ./build.sh   (RXDK: RXDK root; RX_OUT: output dir, default out; EXTRA_CFLAGS: extra
#                     defines; PYTHON: the Python 3 to use)
# Runs on Windows (Git Bash / MSYS2), Linux and macOS, with RXDK where RXDK itself puts it.
set -euo pipefail
cd "$(dirname "$0")"

case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*) os=windows EXE=.exe DEFAULT_ROOT="${ProgramData:-C:/ProgramData}/RXDK" ;;
Darwin) os=macos EXE= DEFAULT_ROOT="$HOME/Library/Application Support/RXDK" ;;
*) os=linux EXE= DEFAULT_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/rxdk" ;;
esac
case "$(uname -m)" in
arm64 | aarch64) arch=arm64 ;;
*) arch=x64 ;;
esac
RXDK=${RXDK:-$DEFAULT_ROOT} # llvm/, sdk/, tools/ (scripts/stage-rxdk.ps1 lays them out)
RXDK=${RXDK//\\//}
CL="$RXDK/llvm/xbox-$os-$arch/bin/clang$EXE"
CLI="$RXDK/tools/Rxdk.Cli$EXE"
IMAGEBLD="$RXDK/tools/imagebld$EXE"
for t in "$CL" "$CLI" "$IMAGEBLD"; do
    [ -x "$t" ] || { echo "missing $t (RXDK root: $RXDK; set RXDK or run scripts/stage-rxdk.ps1)"; exit 1; }
done
# (on Windows python3 may be the Microsoft Store stub, so try python first there)
if [ $os = windows ]; then PY=${PYTHON:-$(command -v python || command -v python3)}; else PY=${PYTHON:-$(command -v python3 || command -v python)}; fi

OUT=${RX_OUT:-out}
mkdir -p "$OUT/obj"
# no stale outputs: a failed step must not leave the previous build looking current
rm -f "$OUT/ConfigLive.exe" "$OUT/ConfigLive.xbe" "$OUT/X3_ConfigLive_2MB.bin" "$OUT/obj/ConfigLive_bios."*

T="-target i686-pc-windows-gnu -march=pentium3"
CFLAGS="${EXTRA_CFLAGS:-} -ffreestanding -fno-builtin -fno-stack-protector -O2 -Wall -Iinclude"
SDKFLAGS=() # (a read loop, not mapfile: macOS ships bash 3.2)
while IFS= read -r f; do SDKFLAGS+=("$f"); done < <("$CLI" compile-args --lang c | tr -d '\r')

OBJS=() BIOS_OBJS=()
while IFS= read -r src; do
    obj="$OUT/obj/${src%.c}.o"
    mkdir -p "$(dirname "$obj")"
    case "$src" in
    src/sdk/sdk_*) "$CL" "${SDKFLAGS[@]}" -O2 -Wno-pragma-pack -c "$src" -o "$obj" ;;
    *) "$CL" $T $CFLAGS -c "$src" -o "$obj" ;;
    esac
    OBJS+=("$obj")
    if grep -q X3_BIOS_BUILD "$src"; then
        "$CL" $T $CFLAGS -DX3_BIOS_BUILD -c "$src" -o "${obj%.o}.bios.o"
        BIOS_OBJS+=("${obj%.o}.bios.o")
    else
        BIOS_OBJS+=("$obj")
    fi
done < <(find src -name '*.c' | LC_ALL=C sort)

# link-exe, then imagebld with the original's certificate title id and init flags:
# make_xbe <exe> <xbe> <objects...>
make_xbe() {
    local exe=$1 xbe=$2
    shift 2
    MSYS_NO_PATHCONV=1 "$CLI" link-exe -o "$exe" "$@" \
        libxgraphics.lib libd3dx8.lib libd3d8.lib libxnet.lib libxapi.lib libcpp.lib libc.lib libcompat.lib libkernel.lib \
        | grep -v '^\$ ' || true
    [ -f "$exe" ] || { echo "link failed: $exe"; exit 1; }
    MSYS_NO_PATHCONV=1 "$IMAGEBLD" /IN:"$exe" /OUT:"$xbe" \
        /STACK:65536 /INITFLAGS:0x18 /TESTID:0xF3EBA821 /TESTNAME:"X3 Config Live" /NOLIBWARN
    [ -f "$xbe" ] || { echo "imagebld failed: $xbe"; exit 1; }
}
make_xbe "$OUT/ConfigLive.exe" "$OUT/ConfigLive.xbe" "${OBJS[@]}"
echo "built $OUT/ConfigLive.xbe"
make_xbe "$OUT/obj/ConfigLive_bios.exe" "$OUT/obj/ConfigLive_bios.xbe" "${BIOS_OBJS[@]}"

# the X3 BIOS with the BIOS build in its flash slot (bios/pack_bios.py; only the slot changes)
"$PY" -I bios/pack_bios.py bios/X3_3294_2MB.bin "$OUT/obj/ConfigLive_bios.xbe" "$OUT/X3_ConfigLive_2MB.bin"
