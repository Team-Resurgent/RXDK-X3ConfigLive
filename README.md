# RXDK X3 Config Live

<p align="center"><b>The Xecuter 3 Config Live app rebuilt as C source on the open RXDK SDK, packed straight into an X3 BIOS image</b></p>

<p align="center">
  <a href="https://github.com/Team-Resurgent/RXDK-X3ConfigLive/actions/workflows/build.yml"><img src="https://github.com/Team-Resurgent/RXDK-X3ConfigLive/actions/workflows/build.yml/badge.svg?branch=main" alt="Build"></a>
  <a href="https://discord.gg/VcdSfajQGK"><img src="https://img.shields.io/badge/chat-on%20discord-7289da.svg?logo=discord" alt="Discord"></a>
</p>

<p align="center">
  <a href="https://ko-fi.com/J3J7L5UMN"><img src="https://img.shields.io/badge/ko--fi-Support-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white" alt="ko-fi"></a>
  <a href="https://www.patreon.com/teamresurgent"><img src="https://img.shields.io/badge/Patreon-F96854?style=for-the-badge&logo=patreon&logoColor=white" alt="Patreon"></a>
</p>

<p align="center">
  <a href="https://github.com/Team-Resurgent/RXDK-X3ConfigLive/releases/latest"><img src="https://img.shields.io/badge/download-latest-brightgreen.svg?style=for-the-badge&logo=github" alt="Download"></a>
</p>

## About

Config Live is the settings dashboard built into the Xecuter 3 BIOS: BIOS banks and flashing, X3
settings, LCD, LED and fan, EEPROM and HDD tools, the on-screen keyboard, the FTP server and the
web flash page. This project reconstructs it function by function from the 4026 build as C,
and builds it on the RXDK SDK libraries (D3D8, XFONT, XNet, XAPI) instead of the original XDK
ones. No original code or data is linked in.

## Changes from the original

- **Build number:** shown as 5838 instead of 4026 (`X3_APP_BUILD` in `include/types.h`), so the
  rebuild can be told apart at a glance.
- **LCD contrast for R3mux boards:** when port 0xF509 reads 0x69, the LCD Settings menu gets
  "LCD Contrast" (0..127, live). B saves it, and it is applied at start-up.

## Requirements

Windows, Linux or macOS, with:

- **RXDK** (clang, the SDK, `Rxdk.Cli` and `imagebld`) where RXDK installs itself:

  | OS | RXDK root |
  |---|---|
  | Windows | `%ProgramData%\RXDK` |
  | macOS | `~/Library/Application Support/RXDK` |
  | Linux | `$XDG_DATA_HOME/rxdk` (`~/.local/share/rxdk`) |

  Set `RXDK` to use another root. The RXDK VS Code / Visual Studio setup installs it, or run
  `pwsh scripts/stage-rxdk.ps1`, which downloads and lays it out as CI does.
- **Bash** (Git Bash on Windows) and **Python 3**
- **[7-Zip](https://www.7-zip.org/)** for the BIOS image: `7z`, `7zz` or `7za` on `PATH`, the Windows default
  install, or `SEVENZIP`

## Build

```bash
./build.sh
```

| Output | What |
|---|---|
| `out/ConfigLive.xbe` | Config Live as a normal XBE |
| `out/X3_ConfigLive_2MB.bin` | `bios/X3_3294_2MB.bin` with this Config Live in its flash slot |

Every push to `main` builds on GitHub Actions (Windows, Linux and macOS) and publishes both files
as the rolling `latest` release; a `v*` tag makes a fixed release. `RX_OUT=<dir>` builds
somewhere else; `EXTRA_CFLAGS` adds defines. Debug switches:
`-DRX_TRACE_MAIN` (start-up markers on the COM1 UART), `-DRX_DEBUG_NONET` (no network thread),
`-DRX_DEBUG_NOSAVECFG` (no XNet config writes).

## The BIOS image

`bios/pack_bios.py` replaces only Config Live inside the X3 image. The kernel finds it at 0x51F0
in each 1 MB half: a header `{0xCDE, compressed size, size, 'l33t'}`, then RC4 (the kernel's key)
of a zlib stream of the XBE. The boot front-end, 2BL and kernel stay byte-identical, so the image
still boots on every Xbox revision the X3 supports; the script checks this, and that the image
decodes back to the exact XBE. Repacking the BIOS's own Config Live reproduces the original image
byte for byte.

The slot holds 0x6ECEE bytes. zlib -9 is about 11 KB too big for this build, so the script uses
7-Zip's deflate, which fits with about 6 KB to spare. The build stops with an error rather than
write an image that doesn't fit.

```bash
python -I bios/pack_bios.py <x3 bios.bin> <ConfigLive.xbe> <out.bin>   # pack
python -I bios/pack_bios.py --extract <x3 bios.bin> <out.xbe>          # unpack
```

## Button combos

| Combo | Does |
|---|---|
| White + B (or Back) | screenshot to `E:\X3ScreenShots\X3ScreenShot_NNNN.bmp` |
| Black + B (or Back) | reboot |
| LT + X | set the video region to NTSC-M in the EEPROM, then reboot |
| LT + Y | set the video region to PAL-I in the EEPROM, then reboot |
| hold RT | fast repeat |

## Source layout

| Folder | What |
|---|---|
| `src/app` | `AppMain`, launching XBEs, the menu objects |
| `src/menus` | bios, datetime, disk, display, eeprom, info, password, quicklaunch, skins, video |
| `src/ui` | the menu engine, message boxes, keyboard, dialogs, flash UI |
| `src/hw` | SMC, X3 port, LCD, pad, clock, X3 settings, EEPROM, flash, HDD |
| `src/net` | FTP server, HTTP (download, web flash page), network config |
| `src/gfx` | D3D setup, drawing, the 3D logo, textures and skins |
| `src/fs` | FAT memory units, ISO 9660, drives, files |
| `src/lib` | buffer, strings, ini, lists, SHA-1, RC4, unzip |
| `src/data` | the app's globals (with the original initial values), fonts, the logo mesh |
| `src/sdk` | glue to the SDK: startup, kernel import slots, D3D shims, `std::string` and minizip replacements |
| `include` | one header per module; every module includes `app.h` |

The comment on each function and global gives its address in the original 4026 image.

## Credits

This rebuild is based on **[Sick-Git](https://github.com/Sick-Git)'s Config Live 4026**
([Xecuter-3-Config-Live-XBOX-App](https://github.com/Sick-Git/Xecuter-3-Config-Live-XBOX-App)),
which took the X3's 3294 Config Live, reverse engineered it and turned it into an HDD app.
Its patches are all carried over into this source:

- flash chip ID patches for AMD / Fujitsu (main flash IDs 01 and 04), and the 256 KB backup flash
  ID check cleared so any suitably structured backup chip works (the original EON X3 backup chip
  is long gone)
- the CPLD X3 chip detection removed, so it boots from any BIOS
- the config read / write dispatch (the D3D code cave) and the write wrapper with auto-load
- config persistence: settings are saved to and loaded from a file
- an HDD access fix
- the X3 3294 clock bug in the year 2020 fixed; the default date is now January 2026 (your own
  settings stay once saved)
