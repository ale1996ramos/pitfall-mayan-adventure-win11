# Technical notes

How *Pitfall: The Mayan Adventure* (Activision / Kinesoft, Windows 95, 1995) was made to run with full sound and CD music on Windows 10/11.

Target files:

| File | Size | SHA-256 |
|---|---|---|
| `PITFALL.EXE` | 710,144 | `c7dadc1b511d8ae16dd713f826fd49d706b4286b6cc6d5b5ee9283aa344ca2fd` |
| `WAIL32.DLL` (Miles AIL 3.03) | 116,736 | `432d51744010e25ec82e550b4bc6422134625451f6a33fe72228528aa28c241a` |

The patched `PITFALL.EXE` has SHA-256 `0aa8f6d5995d5156db0cf525c0438f64a3fc60c14eb72b89f3fa1ed59eb9bc4c`.

## 1. Why there was no sound at all

The game was written before Windows NT was a consumer platform. At startup it calls `GetVersion()` and sets two globals:

```
0x452be0  "Win95 features" = (platform is Win9x && major >= 4)
0x452c20  "NT"             = (platform is NT)
```

When `0x452be0` is 0 the game never calls `LoadLibrary("WAIL32.DLL")`, skips the joystick and disables full screen. On top of that, after reading its settings from the registry it **forces `Option_SoundEnabled = 0`** when running on NT, and greys out the sound control in the Properties dialog. This matches the original `TECH.WRI`: *"The Sound Effects option is not available when running the program in Windows NT."* Windows 10/11 are NT, so sound was switched off on purpose, not because of a driver problem.

Patches (file offsets):

| Offset | Bytes | Effect |
|---|---|---|
| `0x3b6d2` | `76 10` → `90 90` | do not clear the Win95 flag on NT |
| `0x3b6e2` | `73` → `EB` | ignore the minor-version check |
| `0x3f860` | `74` → `EB` | do not force `Option_SoundEnabled = 0` |
| `0x40e95` | `74` → `EB` | keep the sound control enabled in the dialog |

## 2. Why sound effects were cut off

The sound routine (`0x43cc10`) has two paths. On Win9x it uses the Miles Audio Interface Library with **two channels** and volume control. On NT it falls back to `sndPlaySound(SND_MEMORY | SND_ASYNC)`, which plays **one sound at a time**: every new sound cuts the previous one. A single byte makes the routine always take the AIL path:

| Offset | Bytes | Effect |
|---|---|---|
| `0x3c01a` | `74` → `EB` | always use the AIL (two channels) path |

## 3. Why the original Miles DLL cannot be used

With the AIL path enabled, the original `WAIL32.DLL` (AIL 3.03) **freezes the game** on modern Windows. The library:

1. opens `waveOut` with `CALLBACK_FUNCTION`; the callback (WOM_DONE) enters an internal critical section and mixes the next block of audio;
2. runs its service routine from a `SetTimer` timer; that routine holds the same critical section while calling `waveOutWrite` / `waveOutReset`.

Since Windows Vista, winmm holds its own device lock while it runs the callback, so both threads wait on each other forever (a classic deadlock). Patching the callback out removed the freeze but exposed the next problem: the timer is a `WM_TIMER` (~54 ms, only fired when the game pumps messages), so audio became choppy. Moving the service to a multimedia timer did not help either, most likely because the library mixes into buffers that are **already queued** (its WOM_DONE handler fills the buffer four positions ahead). Win9x drivers read the buffer at play time; modern Windows copies it when `waveOutWrite` is called, so those late writes never reach the speakers.

Instead of patching more of a 1995 mixer designed for Win9x drivers, `WAIL32.DLL` was **rewritten** (`src/wail32`). The game resolves only 13 exports through `GetProcAddress`:

```
AIL_startup  AIL_shutdown  AIL_waveOutOpen  AIL_serve  AIL_last_error
AIL_allocate_sample_handle  AIL_init_sample  AIL_end_sample
AIL_set_sample_file  AIL_set_sample_volume  AIL_start_sample
AIL_sample_status  FILE_read
```

The sound effects are complete in-memory WAV images (`SOUNDxx.WAV` loaded from the `.PH` archives). The new DLL parses each WAV once into a static **DirectSound** secondary buffer and reuses it, so start, stop and volume changes are immediate, and Windows does the mixing. It creates no threads, timers or callbacks of its own.

A first version used one `waveOut` stream per sample. It worked, but opening and resetting a `waveOut` stream on Windows 11 takes tens of milliseconds, and the game restarts sounds constantly (27 call sites, almost all on channel 0), so effects were late or dropped. DirectSound fixed that.

## 4. CD music (virtual CD)

The soundtrack is Red Book audio on the game CD (data track 1 + audio tracks 2–24). The game drives it with `mciSendCommandA` on the `cdaudio` device:

* `MCI_OPEN` `cdaudio`, `MCI_SET` time format MSF;
* **disc check:** `MCI_STATUS LENGTH` of track 8 must be `4:32.12` and track 7 must be `4:07.33`, otherwise CD music is disabled;
* time format TMSF, then `MCI_PLAY` with `MCI_FROM = track`, `MCI_TO = track + 1` and `MCI_NOTIFY`; when the track ends the game receives `MM_MCINOTIFY` and starts it again (loop);
* volume fades go through an `aux` device whose technology is `AUXCAPS_CDAUDIO`.

`PITFALL.EXE` imports these from `WINMM.dll`. Since `winmm.dll` is a KnownDLL and cannot be replaced from the game folder, the import name was changed to `PITMM.dll` (same length):

| Offset | Bytes | Effect |
|---|---|---|
| `0x6c7f4` | `57` → `50` | `WINMM.dll` → `PINMM.dll` |
| `0x6c7f6` | `4E` → `54` | → `PITMM.dll` |

`PITMM.DLL` (`src/pitmm`) forwards every other import to the real `winmm` (export forwarding in the `.def`) and implements:

* `mciSendCommandA`: `cdaudio` becomes a virtual CD that answers the disc check with the real track lengths (taken from the Redump dump of the disc), plays the file mapped in `PITMUSIC.INI` through MCI `mpegvideo`, and posts `MM_MCINOTIFY` (`SUCCESSFUL`, `SUPERSEDED`, `ABORTED`) exactly like a CD drive. Tracks without a file play silence for their original length. Any other device is passed to winmm unchanged;
* `auxGetNumDevs` / `auxGetDevCapsA` / `auxGetVolume` / `auxSetVolume`: a virtual "CD Audio" aux device, so the in-game music volume and fades work.

`[options] realcd=1` turns the emulation off and uses a physical CD drive.

### Track usage

Two tables in the EXE (`0x470470` music, `0x4704d8` ambience) give the track for each level index. Level names were recovered from the background table at `0x46f0d8` and the "next level" title table.

| Level | Name | Music track | Ambient track |
|---|---|---|---|
| 0 | Ceiba Jungle | 5 | 4 |
| 1 | Xibalba Falls | 7 | 20 |
| 2 | Tazamul Mines | 8 | 21 |
| 3 | Lost City of Copan | 9 | 21 |
| 4 | Copan Temple | 11 | 21 |
| 5 | Lakamul Rainforest | 12 | 4 |
| 6 | Yaxchilan Lagoon | 13 | 20 |
| 7 | Runaway Minecar | 16 | 21 |
| 8 | Tikal Ruins | 14 | 21 |
| 9 | Temple of Tikal | 15 | 21 |
| 10 | Warrior Spirit | 19 | 21 |
| 11–14 | bonus rooms | 21 | 21 |
| 15 | logo screen | 3 | 3 |
| 16 | Palenque Ruins (PC only) | 14 | 21 |
| 17 | Tomb of Palenque (PC only) | 15 | 21 |
| 18 | Jaina Island Falls (PC only) | 17 | 20 |
| 19 | map | 4 | 4 |
| 20 | Atari 2600 Pitfall! | – | – |
| 21 | opening cutscene | 2 | 2 |
| 22 | ending scene | 22 | 22 |
| 23 | credits | 5 | 5 |
| 24 | ending | 23 | 23 |

Tracks 4, 20 and 21 are jungle, water and cave ambience (used when "CD Ambient" is selected instead of music).

## 5. Settings without administrator rights

Settings are stored in the registry under `HKEY_LOCAL_MACHINE`, which needs elevation on modern Windows. Both accesses were moved to `HKEY_CURRENT_USER`:

| Offset | Bytes | Effect |
|---|---|---|
| `0x3f527` | `02` → `01` | `RegOpenKeyA(HKLM…)` → `HKCU` |
| `0x3f8b1` | `02` → `01` | `RegCreateKeyA(HKLM…)` → `HKCU` |

## 6. Rendering (for future work)

The game draws each frame into a 320×224, 8-bit `CreateDIBSection` and scales it to the window with `StretchBlt`. The original full-screen mode used `DISPDIB.DLL` (16-bit), which does not exist on 64-bit Windows; the game falls back to windowed mode gracefully. Palette effects use `AnimatePalette`.
