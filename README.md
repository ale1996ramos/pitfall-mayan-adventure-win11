# Pitfall: The Mayan Adventure — Windows 10/11 fix

[Português (Brasil)](README.pt-BR.md)

Makes the 1995 Windows 95 version of *Pitfall: The Mayan Adventure* (Activision / Kinesoft) run on Windows 10 and 11 with **sound effects, CD music and saved settings**, without administrator rights, compatibility modes, DxWnd or a CD in the drive.

| Problem on modern Windows | Fixed |
|---|---|
| No sound at all, sound options greyed out | ✅ |
| Sound effects cut each other off / only one sound at a time | ✅ |
| Original Miles `WAIL32.DLL` freezes or stutters | ✅ replaced with a DirectSound implementation |
| CD music needs the original disc in a CD drive | ✅ virtual CD that plays MP3/WAV files |
| Needs "Run as administrator" to save settings | ✅ settings moved to the current user |

How it was done, patch by patch: [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Requirements

* The Windows 95 release of the game, installed or copied to a folder (`PITFALL.EXE` of 710,144 bytes, `WAIL32.DLL`, `INIT.PH`, `LEVEL00.PH` … `LEVEL24.PH`).
* For music: the audio tracks of the game CD as `.mp3` (or `.wav`) files.

## Install

1. Download `PitfallFix-<version>.zip` from [Releases](../../releases) and extract it.
2. Copy `PitfallFix.exe` into the game folder (the one with `PITFALL.EXE`) and run it.
   It checks the game version, backs up the originals as `*.orig`, patches `PITFALL.EXE`, and writes `WAIL32.DLL`, `PITMM.DLL` and `PITMUSIC.INI`.
3. Music: create a `music` folder next to `PITFALL.EXE` with the CD audio tracks named `Track02.mp3` … `Track24.mp3` (track 1 is the data track).
   Using the 19-track soundtrack release instead? Use `PITMUSIC-soundtrack-example.ini` as your `PITMUSIC.INI`.
4. Run `PITFALL.EXE` normally (no compatibility mode). In the game **Properties**, enable *Sound Effects* and *CD Music*.

To undo everything: `PitfallFix.exe --uninstall`.

## Music configuration (`PITMUSIC.INI`)

```ini
[options]
log=0      ; 1 = write PITMM.LOG (troubleshooting)
realcd=0   ; 1 = use a physical CD drive with the original disc

[tracks]
5=music\Track05.mp3   ; CD track = file, relative to the game folder
```

Tracks without a file play silence for the length of the original track. Which track each level uses is listed in [docs/TECHNICAL.md](docs/TECHNICAL.md#track-usage).

## What is in this repository

| Path | What |
|---|---|
| `src/wail32/` | replacement `WAIL32.DLL`: the 13 Miles AIL 3.03 functions the game uses, on DirectSound |
| `src/pitmm/` | `PITMM.DLL`: virtual CD (`cdaudio`) + CD aux volume; forwards everything else to `winmm.dll` |
| `src/installer/` | `PitfallFix.exe`: verifies, backs up, patches and installs (DLLs embedded) |
| `config/` | default `PITMUSIC.INI` and an example for the 19-track soundtrack |
| `docs/TECHNICAL.md` | reverse-engineering notes and the full patch list |

## Build

Cross-compiled with MinGW-w64 (32-bit), e.g. on Ubuntu or WSL:

```sh
sudo apt install gcc-mingw-w64-i686 python3 zip
make          # build/WAIL32.DLL, build/PITMM.DLL, build/PitfallFix.exe
make dist VERSION=v1.0.0
```

GitHub Actions builds every push and attaches the zip to releases for `v*` tags.

## Roadmap

* Borderless full screen with integer / aspect-correct scaling (the game renders at 320×224)
* Optional scaling filters
* Palette animation fixes on 32-bit desktops
* XInput gamepad support

## Credits

* Reverse engineering and fixes: Alessandro Ramos, with Claude (Anthropic).
* CD track lengths: [Redump](http://redump.org/) dump of the PC disc.

*Pitfall: The Mayan Adventure* © 1995 Activision. This is an unofficial fan project, not affiliated with Activision. The code in this repository is released under the [MIT License](LICENSE).
