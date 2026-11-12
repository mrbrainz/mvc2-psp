# Marvel vs. Capcom 2 — PSP

A native PSP port of Marvel vs. Capcom 2: New Age of Heroes, built on the
[MvC2 PS2 decompilation and port](https://github.com/g-guthrie/mvc2-port-and-decomp)
by G Guthrie. It runs on PSP hardware (custom firmware) and in PPSSPP.

**No game data is included.** You build your own EBOOT from your own copy of
the PS2 disc.

## Status

- Full arcade mode plays from start to credits in PPSSPP at full speed.
- Single player only (player 2 is not connected).
- Saves use the PSP's own savedata system.
- Real-hardware speed is still being tested.

Open work is tracked in [docs/backlog.js](docs/backlog.js) (board:
[docs/backlog.html](docs/backlog.html)).

## Requirements

- A PS2 disc image (`.iso`) of **Marvel vs. Capcom 2: New Age of Heroes (USA)**,
  SLUS-20486. Other regions are not supported. BIN/CUE, CSO and CHD images must
  be converted to `.iso` first.
- **PSP-2000 or later** with custom firmware (the PSP-1000 does not have enough
  memory), or [PPSSPP](https://www.ppsspp.org/).
- About 200 MB free on the memory stick.

## Making your EBOOT

Download the package for your computer from
[Releases](https://github.com/mrbrainz/mvc2-psp/releases). The builder checks
your disc, then writes a folder called `MVC2` (containing `EBOOT.PBP`) next to
your `.iso`. Your disc image is only read; nothing is uploaded anywhere.

### Windows

`mvc2-psp-<version>-windows.zip`

- **GUI:** unzip, then drag your `.iso` onto `MvC2 EBOOT Builder.exe`.
  If SmartScreen appears, choose *More info → Run anyway*.
- **Command line:**

  ```bat
  "MvC2 EBOOT Builder.exe" "C:\path\to\game.iso"
  ```

### macOS

`mvc2-psp-<version>-macos.zip` (Apple Silicon and Intel, macOS 11+)

- **GUI:** unzip, open `MvC2 EBOOT Builder.app`, choose your `.iso`. The first
  time, macOS may block it because it is unsigned: right-click the app →
  *Open → Open*, or allow it in *System Settings → Privacy & Security*.
- **Command line:**

  ```sh
  "MvC2 EBOOT Builder.app/Contents/MacOS/mvc2-eboot-builder" ~/path/to/game.iso
  ```

### Linux

`mvc2-psp-<version>-linux-x64.tar.gz`

- **GUI:** extract, then double-click `mvc2-eboot-builder` and choose your
  `.iso` (the file picker needs `zenity`; without it, run it from a terminal).
- **Command line:**

  ```sh
  tar -xzf mvc2-psp-<version>-linux-x64.tar.gz
  cd mvc2-psp-<version>
  ./mvc2-eboot-builder ~/path/to/game.iso
  ```

### Any OS with Python 3

`mvc2-psp-<version>-pypack.zip`

```sh
python3 port/psp/tools/xmb_art.py game.iso art
mkdir MVC2
python3 port/psp/pack/psp_pack.py --icon0 art/ICON0.PNG --pic1 art/PIC1.PNG game.iso base/EBOOT.PBP MVC2/EBOOT.PBP
```

### Builder options

```
mvc2-eboot-builder [-base EBOOT.PBP] [-o OUTDIR] [-q] [game.iso]
  -base  base EBOOT (default: the one shipped with the builder)
  -o     folder to create MVC2/ in (default: next to the .iso)
  -q     no dialogs and no "Press Enter" prompt
```

## Installing and playing

1. Copy the `MVC2` folder into `PSP/GAME/` on the memory stick, so you have
   `PSP/GAME/MVC2/EBOOT.PBP`.
2. Launch it from *Game → Memory Stick*. The `.iso` is not needed on the PSP.

PPSSPP: open `MVC2/EBOOT.PBP` directly.

**The built EBOOT contains game data from your disc. Do not share it.**

### Controls

| PSP | Default |
| --- | --- |
| D-pad / analog stick | Move |
| Square / Triangle | Light punch / Heavy punch |
| Cross / Circle | Light kick / Heavy kick |
| L / R | Assist 1 / Assist 2 |
| Start | Start / pause |

Buttons can be reassigned in the game's own *Button Config*.

### Settings

`PSP/GAME/MVC2/settings.txt` is created on first run:

| Setting | Values |
| --- | --- |
| `screen` | `fit` (4:3, default) or `stretch` (full 480x272). L+R+Select in game switches it. |
| `color` | `32` (default) or `16` (16-bit colour, less VRAM and bandwidth) |

## Building from source

The PSP build needs the [pspdev](https://github.com/pspdev/pspdev) toolchain,
LLVM (clang with the MIPS target), Emscripten, CMake, Ninja and Python 3.
Setup, build steps and PPSSPP testing are in
[port/docs/PSP.md](port/docs/PSP.md).

```sh
cmake -S port/psp -B build/port-psp -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/port/cmake/psp-clang.cmake
ninja -C build/port-psp mvc2_eboot
```

Add `-DMVC2_RELEASE=ON` for a release base EBOOT (no art, no game data). The
EBOOT builder is in [port/psp/builder](port/psp/builder) (Go), and release
packages come from [port/psp/release/package.sh](port/psp/release/package.sh).
Pushing a `v*` tag builds them in CI as a draft GitHub release.

## Repository layout

| Path | Holds |
| --- | --- |
| `port/psp/` | PSP build: host layer, glue, savedata, builder, release scripts |
| `port/platform/platform_psp.c`, `port/render/r_gu*.c` | PSP platform and GU renderer |
| `port/` | Upstream native/browser port the PSP build sits beside |
| `src/`, `config/`, `tools/` | Upstream PS2 decompilation |
| `naomi/` | Upstream NAOMI (arcade) decompilation |
| `docs/backlog.js` | PSP to-do list |

## Credits

This project would not exist without
**[G Guthrie's Marvel vs. Capcom 2 decompilation and port](https://github.com/g-guthrie/mvc2-port-and-decomp)**:
the byte-exact PS2 decompilation, the native port and the arcade decompilation
that this PSP build is based on. Thank you for the work and for sharing it.

Also thanks to the [pspdev](https://github.com/pspdev) community for the
PSP toolchain and SDK, and the [PPSSPP](https://www.ppsspp.org/) team.

## License

The PSP-specific code in this fork (`port/psp/`, `port/platform/*_psp.c`,
`port/render/r_gu*.c`, `.github/workflows/psp-release.yml`) is released under
the [MIT License](LICENSE).

The upstream project has no licence of its own; its code remains under its
authors' rights. Third-party code in the decompilation keeps the notices
listed in [LICENSES/](LICENSES).

## Legal

Disc images, ROMs, extracted assets, generated retail assembly, compilers and
SDK files are not in this repository and must be supplied locally. The port
contains no game data: it reads your own disc image at run time. This is a
research project and is not affiliated with Capcom, Marvel, Sony, Sega or
their licensors.
