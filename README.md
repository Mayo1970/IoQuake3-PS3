# ioquake3-PS3

A port of [ioQuake3](https://github.com/ioquake/ioq3) to the PlayStation 3,
using [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and a custom GL-to-RSX
translation layer (OpenGL 1.1 fixed-function to GCM/RSX).

Three separate builds are produced from the same source tree:

| Variant | XMB title | TITLE_ID | Game dir on HDD |
|---|---|---|---|
| ioQuake3       | **ioQuake3**   | `IOQ3PS300` | `baseq3/` |
| Open Arena     | **Open Arena** | `IOQ3OA00`  | `baseoa/` |
| Team Arena     | **Team Arena** | `IOQ3TA00`  | `baseq3/` + `missionpack/` |

## Status

- Boots, loads maps, gameplay with bots, online multiplayer
- All textures render correctly (map, icons, models, particles)
- Networking: LAN discovery, internet server browser, master server, hosting
- DualShock 3 dual-stick analog input + **rumble (DS3 motor support)**
- On-screen keyboard for text input (console, chat, server address, name)
- Cinematic (intro video) playback with audio
- OGG Vorbis background music
- **Mod support** via `+set fs_game <mod>` (also how TA loads `missionpack`)
- **Standalone OA and TA** packages with their own EBOOT / SFO / icon
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

### Build all three variants

PKG (recommended — includes SFO + icon, installs via Package Manager):

```bash
make clean              && make pkg
make -f Makefile.oa clean && make -f Makefile.oa pkg
make -f Makefile.ta clean && make -f Makefile.ta pkg
```

ELF / SELF only:

```bash
make            # ELF
make self       # ELF + EBOOT.BIN (fake SELF)
make install    # FTP-ready EBOOT + PARAM.SFO + ICON0.PNG layout
make pkg        # full installable PKG
make clean
```

The `Makefile.oa` / `Makefile.ta` variants accept the same targets.

### Build flags

| Flag | Effect |
|---|---|
| `DEBUG=1` | Adds `-DPS3_DEBUG -g`; writes `log[_oa\|_ta].txt` to `/dev_hdd0/data/ioq3/`. Release builds emit no log file. |

### Outputs

| Variant | PKG path |
|---|---|
| ioQuake3   | `build/ioquake3_ps3.pkg` |
| Open Arena | `build_oa/ioquake3_oa_ps3.pkg` |
| Team Arena | `build_ta/ioquake3_ta_ps3.pkg` |

Always run the variant's `clean` before rebuilding after a flag change.

### Standalone macros (under the hood)

- `Makefile.oa` defines `-DSTANDALONEOA`. OA uses `gamename = "Quake3Arena"`
  (real OA 0.8.8 keeps the legacy Q3 gamename for master/auth compat).
- `Makefile.ta` defines `-DSTANDALONETA`. TA keeps `BASEGAME = "baseq3"` and
  forces `+set fs_game missionpack` on boot.
- Q3 (default) has no standalone define.

XMB titles are set via `make_sfo.py --title` in each Makefile's `pkg` /
`install` target. Icons are picked from `icons/<q3|oa|ta>/ICON0.PNG`.

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

TITLE_IDs: `IOQ3PS300` (Q3), `IOQ3OA00` (OA), `IOQ3TA00` (TA).

### Game data (you provide via FTP)

```
/dev_hdd0/data/ioq3/
├── log.txt        ← debug builds only (Q3), log_oa.txt / log_ta.txt for OA/TA
├── qkey           ← auto-created on first boot
├── q3config.cfg   ← per-variant cfg (Q3); oaconfig.cfg (OA); teamarenaconfig.cfg (TA)
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

Create `/dev_hdd0/data/ioq3/` via FTP before first boot. USB fallback
`/dev_usb000/quake3/<gamedir>/pak0.pk3` is also probed if the HDD path
is missing.

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
own weapon fire. Buttons are rebindable from the in-game menu.

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

### Text input (console, chat)

| Input | Action |
|---|---|
| **Triangle** | Open PS3 on-screen keyboard |
| **Cross** | Submit / confirm typed text |

When the console or chat is open, Triangle opens the system OSK. Type,
press Enter on the OSK, then Cross to submit.

### Menus

| Input | Action |
|---|---|
| Left / Right stick | Move cursor |
| D-pad | Arrow keys |
| **Cross / Triangle / Square** | Confirm (Enter) |
| **Circle** | Back (Escape) |
| **Start** | Escape |

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

## License

ioQuake3 is GPLv2. This port layer is also GPLv2. See `LICENSE`.
