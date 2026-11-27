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

`mvc2-psp-<version>-linux-x64.tar.gz` (Intel/AMD) or `-linux-arm64.tar.gz`
(ARM: Raspberry Pi, ARM laptops, VMs on Apple Silicon). Run `uname -m` if
unsure: `x86_64` or `aarch64`.

- **GUI:** extract, then double-click `mvc2-eboot-builder` (or drop your `.iso`
  on it). It opens in a terminal window; the file picker needs `zenity`.
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

The builder apps also give the game the character select music in the PSP menu.
With this package, first build the encoder (needs CMake, git and a C++
compiler), then add `--snd0-tool snd0/bin/mvc2-snd0` before `game.iso`:

```sh
cmake -S port/psp/snd0 -B snd0 -DCMAKE_BUILD_TYPE=Release
cmake --build snd0 --config Release
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

`PSP/GAME/MVC2/settings.txt` is created with the defaults the first time you run
the game:

```
screen=stretch
color=32
unlock=stock
log=off
```

| Setting | Values |
| --- | --- |
| `screen` | `stretch` (full 480x272, default) or `fit` (the original 4:3). L+R+Select in game switches it and saves the choice. |
| `color` | `32` (default) or `16` (dithered 16-bit colour: faster drawing, gradients may band) |
| `unlock` | `stock` (default: characters and extras are unlocked by playing, as on the original) or `all` (everything unlocked) |
| `log` | `off` (default) or `on` (write `mvc2_psp.log` to the game folder, for bug reports) |

To change a setting, quit the game, connect the PSP over USB (or put the memory
stick in your computer), edit the file in a text editor, save it and start the
game again. Keep one setting per line, with no spaces around `=`.

#### Unlocking all characters

1. Run the game once, so that `settings.txt` exists.
2. In `PSP/GAME/MVC2/settings.txt`, change `unlock=stock` to `unlock=all`.
3. Start the game. All characters, extra colours and shop items are unlocked,
   and the same character can be picked more than once in a team.

If the game saves while `unlock=all` is set, the unlocks are stored in your
save and stay after you change back to `stock`. To keep your earned progress
separate, first copy `PSP/SAVEDATA/MVC2PSP000000/` somewhere safe.

### Saves

The game saves to the PSP's own savedata (`PSP/SAVEDATA/MVC2PSP000000/`), shown
in the XMB's *Saved Data Utility*. *Save* / *Load* in the main menu open the
system save dialogs; the game's autosaves need no dialog.

### Troubleshooting

| Problem | What to do |
| --- | --- |
| The builder says the disc is another region, or not MvC2 | Only the USA PS2 disc (SLUS-20486) works. |
| "does not match the USA retail release" | The dump is bad or modified: make a new `.iso` from your disc. |
| "convert ... to an .iso first" | BIN/CUE, CSO/ZSO and CHD images must be converted to a plain `.iso` (for CHD: `chdman extractdvd`). |
| macOS or Windows will not open the builder | It is not signed: see *Making your EBOOT* above for the first-launch steps. |
| The game is not listed on the PSP | The path must be `PSP/GAME/MVC2/EBOOT.PBP`, not `PSP/GAME/MVC2/MVC2/...`. Custom firmware is needed. |
| Something goes wrong and you want to report it | Set `log=on` in `settings.txt`, reproduce it, and attach `PSP/GAME/MVC2/mvc2_psp.log` (`mvc2_psp.prev.log` is the run before). |
| It stops at boot on a PSP-1000 | Not supported: it needs the memory of a PSP-2000 or later. |

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
Pushing a `v*` tag builds them in CI as a draft GitHub release. To test the
builder before tagging, [port/psp/release/draft.sh](port/psp/release/draft.sh)
builds the same packages into `private/release-drafts/` (replaced on each
run) and unpacks the one for your computer there.

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

The XMB music is encoded with
[atracdenc](https://github.com/dcherednik/atracdenc) (LGPL-2.1).


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
