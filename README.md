# ioquake3-PS3

A port of [ioQuake3](https://github.com/ioquake/ioq3) to the PlayStation 3,
using [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and a custom GL-to-RSX
translation layer (OpenGL 1.1 fixed-function to GCM/RSX).

Four builds are produced from the same source tree:

| Variant | XMB title | TITLE_ID | Game dir on HDD |
|---|---|---|---|
| ioQuake3       | **ioQuake3**   | `IOQ3PS300` | `baseq3/` |
| Open Arena     | **Open Arena** | `IOOAPS300`  | `baseoa/` |
| Team Arena     | **Team Arena** | `IOTAPS300`  | `baseq3/` + `missionpack/` |
| Quake 3 Classic | **Quake 3 Classic** | `IOQCPS301` | `baseq3/` (pak0–pak2 only) |

## Status

- Boots, loads maps, gameplay with bots, online multiplayer
- All textures render correctly (map, icons, models, particles)
- Networking: LAN discovery, internet server browser, master server, hosting
- DualShock 3 dual-stick analog input + **rumble (DS3 motor support)**
- **USB keyboard + mouse support** (type in console/chat/menus, mouse aim & menu cursor)
- On-screen keyboard for text input (console, chat, server address, name, menu fields)
- Cinematic (intro video) playback with audio
- OGG Vorbis background music
- **Mod support** via `+set fs_game <mod>` (also how TA loads `missionpack`)
- **Standalone OA and TA** packages with their own EBOOT / SFO / icon
- **Quake 3 Classic** package (protocol 43 — Dreamcast crossplay)
- 60 fps vsync-locked at 720p

## Prerequisites (Windows)

### 1. Build the PS3 toolchain

Build and install [ps3toolchain](https://github.com/ps3dev/ps3toolchain)
(all 9 steps) into `/opt/ps3dev` using devkitPro MSYS2:

```
C:\devkitPro\msys2\msys2_shell.cmd -msys
```

> Use devkitPro's MSYS2, **not** stock MSYS2 (`C:\msys64`).

Verify:

```bash
export PS3DEV=/opt/ps3dev
export PSL1GHT=$PS3DEV
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH
ppu-gcc --version    # powerpc64-ps3-elf-gcc 7.2.0
```

### 2. (Optional) Cg Toolkit

Only needed if you modify `.vcg` / `.fcg` shader sources in
`code/gl/shaders/`. Download [Cg Toolkit 3.1](https://developer.nvidia.com/cg-toolkit)
(32-bit `cgc.exe`).

### 3. Game data

You need to bring your own `.pk3` files — none are included in this repo:

- **Q3**: `pak0.pk3`–`pak8.pk3` from Quake III Arena (Steam: `steamapps/common/Quake 3 Arena/baseq3/`)
- **OA**: Open Arena 0.8.8 paks (https://openarena.ws/)
- **TA**: Q3 baseq3 paks **plus** Team Arena `missionpack/pak0.pk3`–`pak3.pk3`
- **Classic**: `pak0.pk3`–`pak2.pk3` from Quake III Arena (same `baseq3/` as Q3; plus `dc-mappack.pk3` for Dreamcast community servers)

These get FTP'd to the PS3 (see *PS3 directory layout* below).

---

## Building

All ioq3 sources are vendored under `code/` (already patched for PS3) —
no separate patch step, no external `../ioq3` checkout. Each variant has
its own Makefile and its own `build_*` directory so they never stomp each
other.

Open the **devkitPro MSYS2** shell (not stock MSYS2) and set:

```bash
export PS3DEV=/opt/ps3dev
export PSL1GHT=$PS3DEV
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH
cd /e/path/to/ioquake3-PS3
```

### Build all variants at once

```bash
make clean && make all-flavors
```

This builds Q3A, Open Arena, and Team Arena PKGs in sequence (Classic is not included — build it separately with `CLASSIC=1`):
- `build/ioquake3_ps3.pkg`
- `build_oa/ioquake3_oa_ps3.pkg`
- `build_ta/ioquake3_ta_ps3.pkg`

### Build individual variants

PKG (recommended):

```bash
make clean && make pkg              # ioQuake3 (Q3A)
make clean && make OA=1 pkg         # Open Arena
make clean && make TA=1 pkg         # Team Arena
make clean && make CLASSIC=1 pkg    # Quake 3 Classic (Dreamcast crossplay)
```

Or use the shorthand targets:

```bash
make clean && make oa && make OA=1 pkg          # Open Arena (two steps)
make clean && make ta && make TA=1 pkg          # Team Arena (two steps)
make clean && make classic && make CLASSIC=1 pkg # Classic (two steps)
```

Other targets (all variants):

```bash
make              # ELF binary
make self         # ELF + EBOOT.BIN (fake SELF)
make install      # FTP-ready EBOOT + PARAM.SFO + ICON0.PNG layout
make pkg          # full installable PKG
make clean        # wipe all build dirs (build/, build_oa/, build_ta/, build_qc/)
```

### Build flags

| Flag | Effect |
|---|---|
| `DEBUG=1` | Adds `-DPS3_DEBUG -g`; writes `log[_oa\|_ta].txt` to `/dev_hdd0/data/ioq3/`. Release builds emit no log file. |
| `CLASSIC=1` | Builds protocol-43 Dreamcast-crossplay variant. See the *Quake 3 Classic* section under Mods. |

### Outputs

| Variant | PKG path |
|---|---|
| ioQuake3        | `build/ioquake3_ps3.pkg` |
| Open Arena      | `build_oa/ioquake3_oa_ps3.pkg` |
| Team Arena      | `build_ta/ioquake3_ta_ps3.pkg` |
| Quake 3 Classic | `build_classic/ioquake3_classic_ps3.pkg` |

Always run `make clean` before rebuilding after a flag change.

### Compiler flags (under the hood)

The Makefile detects `OA=1` / `TA=1` / `CLASSIC=1` and sets the appropriate defines:

- `OA=1` → `-DSTANDALONEOA`. OA uses `gamename = "Quake3Arena"`
  (real OA 0.8.8 keeps the legacy Q3 gamename for master/auth compat).
- `TA=1` → `-DSTANDALONETA`. TA keeps `BASEGAME = "baseq3"` and
  forces `+set fs_game missionpack` on boot.
- `CLASSIC=1` → `-DCLASSIC -DLEGACY_PROTOCOL`. Protocol 43, pak0–pak2 only,
  master server at `dc.dreamcast-talk.com`.
- Neither → Q3A (default, no standalone define).

XMB titles and icons are set via `make_sfo.py --title` and picked from
`icons/<q3|oa|ta|qc>/ICON0.PNG` respectively.

---

## PS3 directory layout

Two roots: the EBOOT lives in the *game* container, but **game data + log
live under `/dev_hdd0/data/`** so reinstalling the PKG never wipes your
paks or settings.

### EBOOT (installed by PKG / FTP)

```
/dev_hdd0/game/IOQ3PS300/        ← ioQuake3 (TITLE_ID per variant)
├── PARAM.SFO
├── ICON0.PNG
└── USRDIR/
    └── EBOOT.BIN
```

TITLE_IDs: `IOQ3PS300` (Q3), `IOOAPS300` (OA), `IOTAPS300` (TA), `IOQCPS301` (Classic).

### Game data (you provide via FTP)

```
/dev_hdd0/data/ioq3/
├── log.txt        ← debug builds only (Q3/Classic), log_oa.txt / log_ta.txt for OA/TA
├── qkey           ← auto-created on first boot
├── q3config.cfg   ← per-variant cfg (Q3/Classic); oaconfig.cfg (OA); teamarenaconfig.cfg (TA)
├── baseq3/
│   ├── pak0.pk3 … pak8.pk3        ← required for Q3 and TA
├── missionpack/
│   ├── pak0.pk3 … pak3.pk3        ← required for TA only
└── baseoa/
    └── pak0.pk3 …                 ← required for OA only
```

Variant requirements:

- **ioQuake3** needs `baseq3/`.
- **Team Arena** needs `baseq3/` **and** `missionpack/`.
- **Open Arena** needs `baseoa/` only (no `baseq3/` required).
- **Quake 3 Classic** needs `baseq3/` (`pak0`–`pak2` only; higher paks are ignored).

Create `/dev_hdd0/data/ioq3/` via FTP before first boot. USB fallback
`/dev_usb000/quake3/<gamedir>/pak0.pk3` is also probed if the HDD path
is missing.

### Quake 3 Classic (Dreamcast crossplay)

`make CLASSIC=1` builds a fourth pkg that speaks **Quake III Arena protocol 43** — the protocol used by the original 1999 Dreamcast release. Modern ioQuake3 uses protocol 68 and is not compatible with Dreamcast servers, so this variant exists purely to enable crossplay between PS3 and the small community still running Dreamcast-era servers. The Internet server browser points at `dc.dreamcast-talk.com` out of the box.

Only `pak0`–`pak2` are loaded (byte-identical to the Dreamcast data files); higher paks and PS3-specific paks are excluded so their checksums do not interfere with the server authentication handshake.

To play on Dreamcast community servers you also need `dc-mappack.pk3`, which contains the maps in rotation on those servers. Download it from [lvlworld.com](https://lvlworld.com/download/id:999) and place it in `/dev_hdd0/data/ioq3/baseq3/`.

---

### Installing custom mods

The engine respects `fs_game`. Drop mods under their own dir alongside
`baseq3/`:

```
/dev_hdd0/data/ioq3/excessive/pak0.pk3
```

Then either bind a key to `set fs_game excessive; vid_restart`, edit the
boot cmdline (rebuild required), or use the console (`\fs_game excessive`,
then `\vid_restart`).

---

## Installing on PS3

### PKG (recommended)

1. Build with `make pkg` (and `-f Makefile.oa pkg` / `-f Makefile.ta pkg`).
2. Copy the `.pkg` to a USB drive.
3. Install via Package Manager on your CFW/HEN PS3.
4. FTP the game data into `/dev_hdd0/data/ioq3/<baseq3|baseoa|missionpack>/`.

### FTP / SELF

1. Build with `make install` — produces `build/pkg/USRDIR/EBOOT.BIN` etc.
2. FTP that whole layout to `/dev_hdd0/game/<TITLE_ID>/`.
3. FTP game data to `/dev_hdd0/data/ioq3/...`.
4. Refresh the XMB game list.

---

## Controls (DualShock 3)

Dual-stick FPS layout, analog movement + look, rumble on hits / own pain /
own weapon fire. Buttons are rebindable from the in-game menu. Button names
(CROSS, CIRCLE, SQUARE, TRIANGLE, L1–R2, L3, R3, SELECT) are displayed in 
menus and key-binding screens.

### In-game

| Input | Action |
|---|---|
| Left stick | Move (forward/back + strafe) |
| Right stick | Look (yaw + pitch) |
| **R2** | Fire |
| **L2** | Zoom |
| **Cross** | Jump |
| **Circle** | Crouch |
| **Square** | Previous weapon |
| **Triangle** | Next weapon |
| **L1** | Strafe left |
| **R1** | Strafe right |
| **L3** | Run / walk toggle |
| **R3** | Scoreboard |
| **Select** | Scoreboard |
| **Start** | Menu (Escape) |
| **Select + Cross** | Open chat |
| **Select + Triangle** | Toggle console |
| **Select + Start** | Quit to XMB |
| **L3 + R3** | Toggle rumble on/off |

### Rumble

Triggered automatically on:

- Taking damage (own pain)
- Firing your own weapon (rocket / shotgun / generic)
- Hit-feedback sounds

Cvars (both `CVAR_ARCHIVE`):

| Cvar | Default | Notes |
|---|---|---|
| `ps3_rumbleEnable` | `1` | 0 = off |
| `ps3_rumbleScale`  | `1.0` | clamps to `[0, 1]` |

### Text input (console, chat, menu fields)

| Input | Action |
|---|---|
| **Triangle** (console open) | Open on-screen keyboard; auto-submits as command |
| **Cross** (console open) | Confirm typed text and execute |
| **Select + Cross** | Open chat message editor |
| **Triangle** (chat open) | Open on-screen keyboard; auto-submits as message |
| **Cross** (chat open) | Confirm typed message and send |
| **Cross** (menu, on a focused text field) | Open on-screen keyboard; result is written into the field. On buttons/sliders it falls through to the normal confirm. |

The OSK is the PS3 system on-screen keyboard. When opened from the console
context, typed text is auto-submitted as a command (prepended with `/` to
prevent accidental chat broadcast). From chat context, text is the message body.
Menu text fields (player name, server address, etc.) accept OSK input directly
in all four variants (Q3, OA, TA, Classic).

---

## USB keyboard & mouse

A USB keyboard and/or mouse can be plugged into the PS3 and used alongside (or
instead of) the DualShock 3 — no configuration needed, they are detected and
polled automatically at boot, and hot-plugging is handled.

**Keyboard:**

- Full text entry in the console, chat, and menu text fields — no OSK needed.
- Standard key bindings work (the keyboard emits normal Quake3 key events), so
  any action can be bound to a key.
- Modifier keys (Ctrl / Shift / Alt) and key auto-repeat are supported.

**Mouse:**

- Mouse look in-game (free aim, same as the right stick).
- Left / right / middle buttons fire `MOUSE1` / `MOUSE2` / `MOUSE3` — bindable.
- Scroll wheel emits `MWHEELUP` / `MWHEELDOWN` (e.g. weapon cycling).
- Drives the menu cursor.

While the on-screen keyboard is open, both devices are held in sync-only mode so
their input does not bleed through into the game underneath.

### Menus

| Input | Action |
|---|---|
| Left / Right stick | Move cursor |
| D-pad | Arrow keys |
| **Cross / Triangle / Square** | Confirm (Enter) |
| **Circle** | Back (Escape) |
| **Start** | Escape |

---

## Custom music

OGG Vorbis background music playback is fully supported. Custom music tracks
can be loaded per-map via playlist configs:

**Search order:**

1. `playlist_<mapname>.cfg` — map-specific playlist
2. `autoexec_<mapname>.cfg` — map-specific autoexec
3. `playlist.cfg` — fallback generic playlist

**Format** (one path per line; `#` and `//` are comment lines):

```
random                 # optional: pick one song at random
music/track1.ogg       # bare path
music track2.ogg       # or "music <path>" syntax
```

Place the config in a pk3 under `baseq3/` (or `baseoa/`/`missionpack/`).
If no playlist config is found for the current map, the engine falls back
to the map's `CS_MUSIC` config string server-side (no regression).

---

## Recompiling shaders

Only needed if you modify `.vcg` / `.fcg` sources in `code/gl/shaders/`.

```bash
cd code/gl/shaders
./compile_shaders.sh

# If cgc.exe isn't in a standard location:
CGC_PATH=/path/to/cgc.exe ./compile_shaders.sh
```

Two-step pipeline: `cgc.exe` (Cg Toolkit 3.1, 32-bit) → ARB asm
(`-profile vp40` / `fp40`), then `cgcomp -a` → RSX `.vpo` / `.fpo`.
Output is embedded into `code/gl/ps3gl_shader_data.h` as C arrays.

---

## Memory budget

GameOS reserves ~88 MB of the 256 MB XDR, leaving ~145 MB free user
memory at `Sys_PlatformInit` (probed and logged on every boot in debug
builds).

| Region | Size | Notes |
|---|---|---|
| Hunk (`com_hunkMegs`) | 96 MB | Maps, shaders, models |
| Zone (`com_zoneMegs`) | 24 MB | Dynamic allocs, zlib inflate |
| Sound (`com_soundMegs`) | 8 MB | Audio buffers |
| Vorbis decoder + ring | a few MB | OGG music |
| RSX IO buffer | 32 MB | GCM command buffer + control |
| Framebuffers | ~7 MB (VRAM) | Two 1280x720 ARGB + Z24S8 |
| Vertex ring | 4 MB (VRAM) | Per-frame vertex data |
| Textures | ~40 MB (VRAM) | RSX texture heap |

~25 MB margin above hunk+zone for QVM loads / scratch malloc.
Don't raise `DEF_COMHUNKMEGS` above 112 without re-measuring.

---

## Important notes

- All `.sh` scripts must have LF line endings (CRLF breaks bash on the
  toolchain).
- `-mno-altivec` is required. PS3's Cell SPE is not AltiVec.
- `MAX_CLIENTS` stays at 64 (q_shared.h hardcodes it).
- IPv6 stubs compile but don't function (PSL1GHT has no IPv6).
- Library link order matters: `-lsysmodule` after `-lnet`.
- If switching variants seems to misbehave (wrong gamename, missing intro
  cinematic), delete the on-PS3 `*config.cfg` for that variant — stale
  cvar values can override defaults.

## Credits

- **[ioQuake3](https://github.com/ioquake/ioq3)** — the upstream engine this port is based on.
- **[PSL1GHT](https://github.com/ps3dev/PSL1GHT)** / **[ps3toolchain](https://github.com/ps3dev/ps3toolchain)** — PS3 homebrew toolchain, GCM/RSX headers, and runtime library.
- **[Lilium Arena Classic](https://github.com/clover-moe/lilium-arena-classic)** (clover-moe / clover-leaf) — reverse-engineered Quake III Arena protocol-43 / Dreamcast compatibility layer. The CLASSIC build's pure-checksum exchange, `cl_paks` format, server-message parse fixes, and `FS_ReferencedPakPureChecksums` compat mode are derived from this work.

---

## AI disclosure

Parts of this port were developed with the assistance of **Claude** (Anthropic). AI was used for code generation, debugging, porting guidance, and documentation. All AI-generated code was reviewed and tested on hardware before inclusion.

---

## License

ioQuake3 is GPLv2. This port layer is also GPLv2. See `LICENSE`.
