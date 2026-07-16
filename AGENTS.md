# CLAUDE.md — ioquake3-PS3 port (PSL1GHT / devkitPro)

> Agent guidance for this repo. Dense by design; read it fully before editing.
> End users want [README.md](README.md).
>
> **This file is the master. [AGENTS.md](AGENTS.md) is a verbatim mirror of it**
> (Codex and other tools read AGENTS.md). Edit CLAUDE.md, then copy it over
> AGENTS.md so the two never drift. If they ever disagree, CLAUDE.md wins.

---

## Maintaining this file (read first)

This doc rotted into an 82 KB dated changelog once; keep it from happening again.

**Add an entry here only if ALL of these are true:**
1. It is a durable invariant or constraint, not a one-time event.
2. Violating it breaks the build, boot, or gameplay.
3. It is **not** obvious from reading the code.

The test: *"Would an agent re-introduce this bug if this line were deleted?"*
If no, it does not belong here.

**Do NOT add:** dated post-mortems ("fixed 2026-05-30…"), session logs, "Session N
DONE" status, or narration of what changed. **That is what git history is for.**
Once a fix lands and the code enforces it, delete the war story — keep only the
one-line rule that stops the next person undoing it.

**CLAUDE.md vs. memory vs. git:**
- **CLAUDE.md / AGENTS.md** — repo invariants and constraints (this file).
- **Agent memory** — facts about *Matt* and how he wants work done, and project
  context not derivable from the repo. Never duplicate repo facts into memory.
- **git history / commit messages** — the record of what changed and when.

When in doubt, prefer fewer words. A 300-line doc that is true beats a 1300-line
doc that is half stale.

---

## What this project is

A working PS3 port of [ioquake3](https://github.com/ioquake/ioq3) on the PSL1GHT
homebrew SDK. PS3 has no GPU OpenGL driver, so the project ships a custom
GL-1.1 → RSX/GCM translation layer (`code/gl/`). All upstream ioq3 sources are
**vendored under `code/` and patched in place** — there is no external `../ioq3`
checkout and no patch-script step. One source tree builds **five** PKGs (see
below). It boots, renders, plays online/LAN/bots, has DS3 input + rumble, OSK,
USB keyboard/mouse, cinematics, and OGG music.

---

## Build

- Requires `PS3DEV` set and the **devkitPro MSYS2** shell (`ppu-gcc`, GCC 7.2,
  LP64). You probably cannot build on the dev host — reason from source when the
  toolchain is unavailable. Matt builds and tests on hardware himself.
- Run all `make` from MSYS2 bash with `PS3DEV` set.

| Goal | Variant | `-D` flags | Build dir | TITLE_ID |
|---|---|---|---|---|
| `make pkg` | ioQuake3 (Q3A) | *(none)* | `build/` | `IOQ3PS300` |
| `make oa` / `make OA=1 pkg` | Open Arena | `STANDALONEOA` | `build_oa/` | `IOOAPS300` |
| `make ta` / `make TA=1 pkg` | Team Arena | `STANDALONETA` | `build_ta/` | `IOTAPS300` |
| `make classic` / `make CLASSIC=1 pkg` | Quake 3 Classic | `CLASSIC LEGACY_PROTOCOL` | `build_qc/` | `IOQCPS301` |
| `make ef` / `make EF=1 pkg` | Elite Force | `ELITEFORCE LEGACY_PROTOCOL` | `build_ef/` | `IOEFPS300` |

- All TITLE_IDs are **9 characters** — `CONTENT_ID`/PARAM.SFO APP_ID parsing on
  PS3 firmware expects exactly that length; a shorter/longer ID breaks PKG install.
- `make all-flavors` builds all five PKGs (Q3/TA/OA/Classic/EF) in sequence.
- `make clean` wipes all build dirs. Append `pkg` (installable PKG), `self`
  (raw SELF), or `install` (FTP-ready dir).
- `make DEBUG=1` adds `-DPS3_DEBUG -g`, enables the `ps3_log()` file and
  `com_logfile 2`. Release writes no log file.
- Icons come from `icons/<q3|oa|ta|qc|ef>/ICON0.PNG`.

---

## Editing rules

- Edit `code/<subdir>/<file>` directly — vendored sources are the source of truth.
- `ps3_*` files and everything in `code/gl/ audio/ input/ renderer/ spu/` are
  PS3-original — edit freely.
- Files with upstream ioq3 names are vendored — keep changes minimal, guard with
  `#ifdef __PS3__` where practical.
- All CLASSIC-only changes must be inside `#ifdef CLASSIC` and must not affect
  Q3/OA/TA builds.
- Stay at **`-O2`** globally — `-O3` breaks boot on this toolchain.
- Keep **`-mno-altivec`** globally — AltiVec is enabled per-file only for
  `ps3_snd.c`.
- All `.sh` scripts must be **LF** — CRLF breaks bash on the toolchain.
- Don't add a CD-Key entry UI — there's no input device flow for it, and
  `cl_cdkey` is pre-seeded.
- No hack-ish workarounds. This is a console port with strict platform rules;
  anything the platform doesn't expect crashes or regresses. When stuck, read the
  sibling PS4 port (see References) before guessing.

---

## Memory budget — hard constraint

- **~145 MB free user RAM** at boot (GameOS reserves ~88 MB of the 256 MB XDR).
  Logged at boot as `[mem] free user memory at Sys_PlatformInit: ~N MB`.
- Hunk = **96 MB**, Zone = **24 MB** → 120 MB, ~25 MB margin for QVM/sound/caches.
- **Do not raise hunk above 112 MB** without re-measuring. **zone = 32 MB hangs at
  boot** — keep `DEF_COMZONEMEGS = 24`.
- Constants live in [code/sys/ps3_platform.h](code/sys/ps3_platform.h) as
  **integers, not strings** (`common.c` does arithmetic on them).
- `common.c`'s `DEF_COMHUNKMEGS` / `DEF_COMZONEMEGS` are `#ifndef`-guarded so the
  PS3 overrides win — do not remove those guards.
- `MAX_CLIENTS` is effectively **64** regardless of `-DMAX_CLIENTS=8`
  (`q_shared.h` hardcodes 64). Size arrays for 64.
- `MAX_RAW_STREAMS` must be ≥ `2*MAX_CLIENTS+1` = **129** (`= 2*64+1` in
  ps3_platform.h) or `cl_parse.c` voice-chat writes OOB into `s_rawsamples[]`.
- `MAX_RAW_SAMPLES` = **8192** (`s_rawsamples[MAX_RAW_STREAMS][MAX_RAW_SAMPLES]`,
  set in **both** `ps3_platform.h` and the Makefile's `CFLAGS` — the Makefile
  `-D` wins over the header's `#ifndef` fallback on every compile, clean or
  not, so both must be edited together or a header-only change silently does
  nothing). RoQ cinematic audio (stream 0) legitimately runs ~13500-16200
  samples ahead of `s_soundtime`, more than 8192 holds — **do not bump this
  constant to fix that**, confirmed on real hardware that raising it for all
  129 streams (+~8 MB static memory) hangs at boot despite the ~25 MB margin
  looking sufficient on paper.
- Cinematic/music crackle is instead fixed via a **stream-0-only** deeper
  buffer: `s_rawsamples0[MAX_RAW_SAMPLES_STREAM0]` (16384, `snd_dma.c`,
  declared in `snd_local.h`, sized in `ps3_platform.h`) is a separate array
  used only when `stream == 0` (cinematics + background music — see
  `S_Base_RawSamples`, `S_PaintChannels` in `snd_mix.c`, and
  `S_UpdateBackgroundTrack`'s `S_RAW_STREAM0_CAP`); the other 128 voice-chat
  streams keep using the small shared `s_rawsamples[][MAX_RAW_SAMPLES]` array.
  Cost is +128 KB, not +8 MB. If you touch raw-stream masking logic, every
  site must pick its mask from the same stream-0-vs-other branch — a mismatch
  between the write-side mask (`S_Base_RawSamples`) and read-side mask
  (`S_PaintChannels`) reads/writes the wrong ring slot.

---

## Source layout

```
code/
├── qcommon/ client/ server/ botlib/ game/ cgame/ q3_ui/ ui/
│   renderercommon/ renderergl1/ renderergl2/ null/ asm/ sdl/ thirdparty/
│        ← VENDORED upstream ioq3, patched in place.
├── gl/       ← PS3-ORIGINAL. GL-1.1 → RSX/GCM translation layer (hot path).
├── audio/    ← PS3-ORIGINAL. ps3_snd.c is the sole sound dispatcher.
├── input/    ← PS3-ORIGINAL. DS3 pad, rumble, OSK, USB kb/mouse.
├── renderer/ ← PS3-ORIGINAL. Stub-glue between upstream tr_* and the GL layer.
├── spu/      ← PRESENT BUT DEAD. SPU vertex-offload experiment, NOT in the
│              Makefile (see §Performance). Do not assume it runs.
└── sys/
    ├── ps3_main.c ps3_sys.c ps3_glimp.c ps3_platform.h ps3_net.h ps3_setjmp.S
    │        ← compiled. Entry point is ps3_main.c::main().
    ├── include/ ← PS3 shims for headers PSL1GHT lacks (SDL_*.h, endian.h, …).
    └── sys_unix.c sys_main.c …  ← NOT compiled; reference only.
```

`code/gl/ps3gl_spu.{c,h}` also exist but are unused/unbuilt.

---

## Boot order & cvar timing

```
ps3_main.c::main()
  ├─ PS3_Input_Init()      ← BEFORE Com_Init. Must NOT call Cvar_Get here.
  │                          Input cvars register in IN_Init (after Com_Init).
  ├─ Sys_GetCurrentUser()  ← XMB nickname (see below)
  ├─ PS3_SetupFilesystem() ← probes /dev_hdd0/data/<gamedir>/pak0.pk3
  ├─ Sys_PlatformInit()    ← free-mem probe + FPU setup (NOT auto-called by
  │                          upstream sys_main.c; ps3_main.c calls it explicitly)
  └─ Com_Init(cmdline)     ← engine start; cmdline beats Cvar_Get defaults
        └─ … GLimp_Init → IN_Init (register input cvars here) …
```

- **Never call `Cvar_Get` before `Com_Init`** — it was a real boot crash.
- All PS3 tuning rides the boot cmdline in `ps3_main.c`. Do **not** set `name`
  via `+set name` or `Cvar_Set` in `Sys_Init` — both overwrite the saved config.
- **XMB nickname**: `Sys_GetCurrentUser` tries `CURRENT_USERNAME` (0x0131, 64 B,
  offline) then `NICKNAME` (0x0113, 128 B, needs sign-in) then `"player"`. Buffer
  sizes matter. `ps3_main.c` applies it after `Com_Init` only if `name` is still
  `"UnnamedPlayer"`, so in-game renames persist.

---

## Boot cmdline — key injected cvars

| Cvar | Value | Reason |
|---|---|---|
| `g_doWarmup` | `0` | Kills Q3A warmup `map_restart` loop (slow cgame reload → serverId race) |
| `com_maxfps` | `60` | Uncapped overfills the GCM buffer → `rsxFlushBuffer` mid-frame stall |
| `max_routingcache` | `6144` KB | Botlib routing-cache headroom (default 4096) |
| `r_customwidth/height` | `1280` / `720` | **720p is current** (see §Performance) |
| `cl_allowDownload` | `1` | UDP pk3 downloads from servers |
| `fs_game missionpack` | TA only | TA loads via fs_game; BASEGAME stays `baseq3` |
| `com_logfile` | `2` debug / `0` release | ioq3's own qconsole.log writer |

---

## QVM execution — INTERPRETER (no JIT)

**All QVMs run on the bytecode interpreter.** The Makefile builds `vm_none.c`;
the PS3 block in `q_platform.h` does **not** define `HAVE_VM_COMPILED`.

- A PPC JIT was investigated and is a **hard NO-GO**: lv2 enforces per-page NX
  and exposes no exec flag in the user API, so instruction fetch from a heap/JIT
  page faults — and the failure is a **console freeze, not an error**, so it
  can't even be runtime-probed. No PS3 homebrew ships a dynarec for this reason.
- `code/qcommon/vm_powerpc.c` is kept in-tree **unbuilt** (with PS3 patches) only
  in case lv2-level executable memory ever appears. Do not add it to the Makefile.
- `vm_none.c`'s `VM_Compile`/`VM_CallCompiled` call `exit(99)` — they are
  unreachable because `HAVE_VM_COMPILED` is undefined and stale `vm_* 2` configs
  are downgraded to the interpreter by `vm.c`.

---

## Rendering pipeline

- Hot path: upstream `tr_*` (renderergl1) → `code/renderer/` qgl glue →
  `code/gl/ps3gl_*.c` → RSX. `ps3gl_draw.c::ps3gl_DrawElements` is the critical loop.
- **Vertex data is zero-copy from a tess arena.** The tess vertex arrays live in
  the top 16 MB of the 32 MB `rsxInit` IO buffer (XDR, RSX-snooped); the GPU
  fetches them directly via `rsxAddressToOffset` + `GCM_LOCATION_CELL`. Streams
  not in the arena (dlights, showtris) fall back to a copy into the vring. There
  is no per-vertex interleave repack anymore. (`rsxMemalign` would be VRAM and
  starve CPU reads — keep the arena in XDR.)
- **Do not split the vertex interleaving loop** in the copy fallback — the RSX
  ring is write-combined; multiple passes per attribute thrash the WC hardware.
- **Every carve from a ring/arena must self-align its own start** (streams 16 B,
  indices 128 B). A misaligned F32 attribute offset wedges the RSX vertex fetch =
  whole-console freeze. Never rely on the previous carve's size keeping alignment.
- **Ring/arena GPU fences**: use `rsxSetWriteBackendLabel` (drains the pipeline),
  **not** a command label (races ahead of vertex fetch). Label indices < 64 are
  system-reserved. The vring is **double-buffered per frame** (`ps3gl_vring_t`,
  `code/gl/ps3gl.h`): segment 0 uses label 64, segment 1 uses label 66 (65 stays
  reserved), `ps3gl_begin_frame` swaps segments and waits only on the one it's
  about to reuse. A same-frame overflow refuses to wrap (drops that batch with a
  warning) rather than clobbering earlier draws still in flight — never make it
  silently wrap again. The tess-arena zero-copy path (`ps3gl_draw.c`'s direct
  `rsxAddressToOffset` bind onto Q3's own `tess.xyz`) has **no fence at all** —
  do not assume one exists; this is a known, deferred gap, not yet hardware-
  validated. `gcmGetLabelAddress` returns `u32*` — cast to `volatile u32*`.
- **Dual-texture lightmap**: `RB_StageIteratorLightmappedMultitexture` needs the
  `PS3GL_TENV_MODULATE2` key (tex0×tex1). If lightmaps render flat-lit,
  `ps3gl_shader_key()` is only looking at TMU0.
- **Mirror/portal**: RSX has no fixed-function clip plane. Software triangle
  culling lives in `ps3gl_draw.c` and must use the **world-space** `portalPlane`
  via `ps3gl_SetWorldClipPlane` (`#ifdef __PS3__` in `tr_backend.c`). Using
  eye-space `plane2` gets the wrong sign and blacks the mirror.
- Textures are swizzled with full mip chains. `rsxTextureControl` min/max LOD are
  4.8 fixed-point — `maxlod = (num_levels-1)<<8`. A texture that "binds but does
  nothing" → check this register first. `gcmTexture.mipmap` is a **level count**,
  not a boolean (PSL1GHT header comment is wrong).

---

## Sound pipeline

- `snd_main.c` is **not compiled**. `code/audio/ps3_snd.c` is the sole dispatcher
  and calls `S_Base_*` from `snd_dma.c` directly.
- `S_Update` must call **`S_Base_Update()`**, not `S_Update_()` — the raw call
  skips `S_UpdateBackgroundTrack` and music never plays.
- Any console command `snd_main.c` would register (`play`, `music`, `stopmusic`,
  `soundlist`, `soundinfo`) **must** be registered in `ps3_snd.c::S_Init`, or the
  client forwards it to the server as an unknown command (shows up in chat).
- `s_musicVolume` is defined in `ps3_snd.c`, resolved via `extern` in `snd_local.h`.
- OGG music uses vendored libvorbis 1.3.7 — no decoder-only split exists, so
  `block.c` pulls in `lpc/lsp/psy/envelope/bitrate/analysis.c`; all are compiled.
- **Custom playlists**: `CG_LoadCustomMusic()` (`cg_main.c`, from `CG_StartMusic`)
  tries `playlist_<map>.cfg`, then `autoexec_<map>.cfg`, then `playlist.cfg`. One
  path per line, `#`/`//` comments, optional `random` keyword. Returns `qfalse` →
  falls back to the map's `CS_MUSIC`.

---

## Five-variant invariants (Q3 / OA / TA / Classic / EF)

Getting these wrong causes networking or boot failures:

- `STANDALONEOA` must **NOT** define `STANDALONE` (OA uses the legacy networking
  path). `STANDALONETA` **does** define `STANDALONE`. `CLASSIC` does not.
- OA `GAMENAME_FOR_MASTER` must be **`"Quake3Arena"`**, not `"openarena"` — real
  OA 0.8.8 uses the legacy Q3 gamename. Wrong value → "Game mismatch" rejecting
  every real OA server/client.
- OA `PROTOCOL_LEGACY_VERSION 71` + `AUTHORIZE_SERVER_NAME "dpmaster.deathmask.net"`
  live in `#ifdef STANDALONEOA` nested inside `qcommon.h`'s `#ifndef STANDALONE`.
- OA `CINEMATICS_LOGO` must be `"idlogo.roq"` (`q_shared.h`) — OA paks ship
  `video/idlogo.roq`.
- TA `BASEGAME` stays `"baseq3"`; it loads `missionpack` via `+set fs_game`.
  Setting `BASEGAME "missionpack"` crashes at `FS_CheckPak0`.
- `FS_CheckPak0` is guarded off for OA/TA/Classic/EF (their pak checksums don't
  match Q3's; Classic intentionally has only pak0–pak2).
- `md5.c` is compiled into all variants — `cl_guid` must be valid 32-char hex or
  OA's qagame rejects connect with "Invalid GUID".
- If cvars misbehave after a gamename/protocol change, delete the on-PS3
  `*config.cfg` — stale configs override defaults.
- **Classic = Dreamcast crossplay** (PROTOCOL_VERSION 43). It speaks proto-43,
  which differs from ioq3's network protocol in non-obvious ways — packet
  scramble instead of XOR encode, no Huffman on connect, no-key usercmds,
  `c == -1` → `clc_EOF`, plain (not pure) pak checksums. All of this is inside
  `#ifdef CLASSIC` across `net_chan.c`, `cl_net_chan.c`, `sv_net_chan.c`,
  `cl_parse.c`, `sv_client.c`, `sv_snapshot.c`, `msg.c`, `files.c`. Reference for
  the proto-43 logic is the PS4 port (`#ifdef CLASSIC`) and lilium-arena-classic
  (`#ifdef ELITEFORCE`). Full per-file change list is in git (commit `620038a`).
- **EF = Star Trek Voyager: Elite Force multiplayer** (`BASEGAME "baseEF"`,
  `PROTOCOL_VERSION 26`, `PROTOCOL_LEGACY_VERSION 24` — 24 is the real retail
  wire protocol id that actually gets negotiated via `com_legacyprotocol`, 26 is
  only ioEF's own project id, do not set them equal). Trap ABI (`gameImport_t`/
  `cgameImport_t`/`uiImport_t` ordinals) and wire structs (`playerState_t`/
  `entityState_t`/`usercmd_t`) both differ from vanilla Q3 — `sv_game.c`,
  `cl_cgame.c`, `cl_ui.c` route EF's trap numbers separately, and `msg.c` has a
  dedicated EF delta-field table, never shared with Classic's. **No fix-pak,
  no bot support** — EF loads Matt's retail `qagame`/`cgame`/`ui` QVMs as-is;
  `code/game`/`cgame`/`q3_ui`/`ui` sources are never touched or compiled for any
  flavor. Reference for the compat wire-framing mechanism (shared with Classic's
  proto-43 implementation) is ioEF (`E:\…\Voyager\ioef`, `#ifdef ELITEFORCE`).

---

## On-disk layout on the PS3

EBOOT in the game container; **game data + log under `/dev_hdd0/data/`** so they
survive a PKG reinstall.

```
/dev_hdd0/game/<TITLE_ID>/USRDIR/EBOOT.BIN   (+ PARAM.SFO, ICON0.PNG one level up)
/dev_hdd0/data/ioq3/
  ├── baseq3/pak0.pk3 …          ← Q3 + TA (bring your own paks; Classic = pak0–2)
  ├── missionpack/pak0–3.pk3     ← TA only
  ├── baseoa/pak0.pk3 …          ← OA only
  ├── qkey                       ← auto-created at first boot (hashed → cl_guid)
  ├── q3config.cfg / oaconfig.cfg / …
  └── log*.txt                   ← debug builds only
```

USB fallback: `/dev_usb000/quake3/<gamedir>/pak0.pk3`.
Hardcoded path call sites to keep in sync: `ps3_log_path`, `ps3_base_path`,
`PS3_SetupFilesystem` (`ps3_main.c`); `ps3_basepath` (`ps3_sys.c`).

---

## Bundled QVM fix-paks

`fixes/baseq3/pak9-ps3.pk3` (Q3/TA), `fixes/missionpack/pak4-ps3.pk3` (TA), and
`fixes/baseq3/zpack-classic.pk3` (Classic) are **prebuilt** `.pk3`s embedded into
the EBOOT at build time (the Makefile's inline python `bin2c` step generates
`*_embedded.h`) and auto-extracted to the game dir on boot via checksum compare.
OA embeds nothing. No manual FTP needed after install.

- pak9/pak4 carry the menu-field OSK fix (CROSS on a focused text field opens the
  OSK; Q3/OA deliver via `SE_CHAR` injection in `q3_ui/ui_mfield.c`, TA via direct
  cvar write in `ui/ui_shared.c` because its `g_editingField` path never fires).
- **The `.sh` scripts that compile the q3_ui/ui QVMs into these paks are NOT
  tracked in this repo** (`tools/` is empty). If you change `code/q3_ui/` or
  `code/ui/`, you must rebuild the QVM and repack externally, then rebuild the PKG.
- `code/q3_ui/` is the PS3 repo's own clean copy (formerly OA-contaminated); it no
  longer needs the PS4 repo to rebuild.

---

## DS3 controls & rumble

- Button aliases in `cl_keys.c` (`#ifdef __PS3__`, before generic `JOY*`):
  CROSS=K_JOY1, CIRCLE=K_JOY2, SQUARE=K_JOY3, TRIANGLE=K_JOY4, L1=K_JOY5 …
  R3=K_JOY10, SELECT=K_JOY11.
- `SELECT+TRIANGLE` toggles console; `CROSS` (console open) opens OSK + auto-submits
  as a command; `CIRCLE` closes console; `SELECT+CROSS` opens chat.
- In menus, Cross emits **only** `K_ENTER` and Circle **only** `K_ESCAPE` (the
  `K_JOY*` is suppressed) — otherwise every confirm/back double-fires.
- Rumble: `PS3_SetRumble(large, small, ms)` in `ps3_input.c`. DS3 small motor is
  **binary** — clamp small to `(sm>0)?1:0`. Triggered from `snd_dma.c`
  (`#ifdef __PS3__`). `L3+R3` toggles `ps3_rumbleEnable` (CVAR_ARCHIVE, default 1).
- USB keyboard uses INPUTCHAR + `KB_CODETYPE_RAW`; `ioKbRead` always returns 0 and
  `nb_keycode==0` is ambiguous, so held keys use a 2000 ms emergency timeout
  refreshed by auto-repeat. **Call `ioKbSetCodeType(RAW)` first; do NOT call
  `ioKbSetReadMode`** — order-swapping silently kills all input.

---

## Performance — current state

**Resolution is 720p** (`ps3_glimp.c` → `VIDEO_RESOLUTION_720`, falls back to TV
default; cmdline `r_customwidth/height 1280/720`). Triple-buffered
(`RSX_FB_COUNT 3`) with a flip-handler counter so slightly-over-budget frames
degrade gracefully instead of snapping to 30 fps. 480p was used historically for
bots + picmip 0 headroom but is **not** the current setting.

Two historical bottlenecks, both fixed:
1. **CPU/bots** — botlib routing-cache LRU thrash. Proactive eviction in
   `AAS_AllocRoutingCache` (`be_aas_route.c`) + `max_routingcache 6144`.
2. **GPU/fill rate** — addressed by the renderer work below (and 480p was the
   blunt fallback).

Wins that are live: zero-copy tess arena (§Rendering), swizzled textures + mips,
`glIndex_t`→`uint16`, indices in an RSX index ring (`rsxDrawIndexArray`), VMX
int16→float32 audio in `ps3_snd.c` (8/iter, `aligned(16)`, `-maltivec` per-file,
`vec_madd` not `vec_mul`), `rsxSetSurface` dedup, MVP upload dedup, frame cap 60.

**Dead ends — do not retry:**
- PPC JIT — lv2 NX, see §QVM.
- **SPU vertex offload** (`code/spu/`, `ps3gl_spu.*`, still in-tree, unbuilt): the
  SPU MFC DMA only reaches the 32 MB RSX IO aperture (`0x50100000–0x70100000`);
  game data lives at `~0x10000000` and the SPU faults silently on `mfc_get`. Do
  not reattempt without pre-staging all source data in `rsxMemalign`'d buffers
  inside the aperture.
- VMX vertex loop / VMX sound mixer (`snd_altivec.c`) — unaligned vector loads
  cause ~40-cycle load-hit-store stalls on the PPE's in-order pipe. Reverted; do
  not re-enable `snd_altivec.c`.
- `-O3` (boot failure), `r_dynamic 0`/`r_fastsky 1` (no gain), splitting the
  vertex interleave loop (WC thrash).

The historical .bss-corruption class is **dead**: it was newlib's AltiVec
`setjmp` overflowing the .bss `abortframe` 192 B every `Com_Frame`. Fixed by
`code/sys/ps3_setjmp.S` + the 512 B `jmp_buf` shim in `code/sys/include/setjmp.h`.
Don't re-add sentinels/canaries.

---

## Shaders

- Sources: `code/gl/shaders/q3_vp.vcg`, `q3_fp_*.fcg`. Only recompile if you edit
  them. Build: `code/gl/shaders/compile_shaders.sh` (prefers PS3DK
  `rsx-cg-compiler`, falls back to `cgc` + `cgcomp -a`) → `.vpo`/`.fpo` embedded
  in `ps3gl_shader_data.h`. `q3_fp_modulate2.fcg` is the dual-texture lightmap FP.
- The `.vcg` has a `CLP0` hardware-clip output for when shaders are recompiled
  with `rsx-cg-compiler`; until then the software portal-cull path is active.

---

## Toolchain — PS3DK migration (future, blocked)

Current: **devkitPro** (`ppu-gcc` GCC 7.2, LP64). Candidate: **PS3DK** (GCC 12,
at `E:\…\DEVkits\PS3DK`). **Blocked:** PS3DK GCC 12 defaults to ILP32 (4-byte
pointers); ioq3 assumes LP64. `-mlp64` restores LP64 but PS3DK's LP64 lib tree
only ships `__cell*` FNID stubs — no PSL1GHT wrappers (`audioInit`,
`netInitialize`, …). Not worth mapping until PS3DK ships a PSL1GHT compat layer.
The Makefile already auto-detects `powerpc64-ps3-elf-gcc` vs `ppu-gcc` and falls
back correctly. **Leave the Makefile build as-is; do not migrate to CMake.**

---

## Symptom → root cause index

| Symptom | Root cause & fix |
|---|---|
| Boot crash / OOM | Hunk/zone too big (§Memory); `Cvar_Get` before `Com_Init`; missing `#ifndef` guard on `DEF_COMHUNKMEGS` |
| Boot hang | zone = 32 MB; `-O3` |
| Whole-console freeze mid-render | Misaligned ring/arena carve → bad F32 attribute offset; or a ring fence using a command label instead of a backend label |
| Menu cursor freezes | `PS3_Input_Frame` early-returning on `padData.len==0`; squared stick curve (use linear) (`ps3_input.c`) |
| Menu confirm/back double-fires | Cross/Circle emitting both `K_JOY*` and `K_ENTER`/`K_ESCAPE`; suppress `K_JOY*` when `in_menu` |
| OSK command broadcast as chat | `Console_Key` con_autochat; `PS3_OSK_Open` must inject leading `/` in console context |
| Menu text field won't accept OSK | Needs pak9 (Q3/OA, SE_CHAR) or pak4 (TA, direct cvar write); `ui_ime_target` empty on buttons falls through to K_ENTER |
| USB keyboard drops all input | `ioKbSetReadMode` called before `ioKbSetCodeType(RAW)`; skip SetReadMode |
| "unknown cmd play/music/…" | Not registered in `ps3_snd.c::S_Init` (snd_main.c isn't compiled) |
| Music opens but silent | `S_Update` calling `S_Update_()` instead of `S_Base_Update()` |
| Mirror black / whole world drawn | Eye-space `plane2` instead of world-space `portalPlane` |
| Lightmaps flat-lit | `ps3gl_shader_key()` only checked TMU0; use `PS3GL_TENV_MODULATE2` |
| Texture binds but renders nothing | `rsxTextureControl` maxlod left at 0 (it's 4.8 fixed point) |
| OA "Game mismatch" | `com_gamename` not "Quake3Arena", or `STANDALONE` defined for OA; delete `oaconfig.cfg` |
| OA no intro cinematic | `CINEMATICS_LOGO` not `"idlogo.roq"` |
| "Invalid GUID" on OA connect | `md5.c` missing/stubbed; must be compiled in |
| Networking 128 MB OOB read | PSL1GHT socket fd has bit 30 set; skip `FD_ISSET` when `fdr==NULL` (`net_ip.c`) |
| UDP send returns ENOENT | PSL1GHT `sockaddr_in` has BSD `sin_len` at offset 0; set it |
| Classic: "CL_ParseSnapshot invalid size" / garbled net | proto-43 needs `Netchan_(Un)ScramblePacket`; XOR encode must be off (`#if defined(LEGACY_PROTOCOL) && !defined(CLASSIC)`) |
| EF: retail QVM crashes/corrupts state on a trap call | Trap-number ABI mismatch in `sv_game.c`/`cl_cgame.c`/`cl_ui.c` — `gameImport_t`/`cgameImport_t`/`uiImport_t` ordinals differ from vanilla Q3, must route per ioEF's own enum, not vanilla's |
| EF build fails on missing `pak9_ps3_embedded.h` | `ps3_main.c`'s embedded-pak `#include` ladder must have its own `#elif defined(ELITEFORCE)` arm (no fix-pak for this flavor) — don't let it fall into the plain-Q3 `#else` |

---

## Where to look

| Need | File |
|---|---|
| End-user install / controls | [README.md](README.md) |
| Engine entry point | [code/sys/ps3_main.c](code/sys/ps3_main.c) |
| PS3 tuning constants | [code/sys/ps3_platform.h](code/sys/ps3_platform.h) |
| GL→RSX hot path | [code/gl/ps3gl_draw.c](code/gl/ps3gl_draw.c) |
| Sound dispatch | [code/audio/ps3_snd.c](code/audio/ps3_snd.c) |
| Input / rumble / OSK / kb / mouse | [code/input/](code/input/) |
| Build logic / variant flags / embedding | [Makefile](Makefile) |
| References | PS4 port `…\ioQuake3-PS4\` (CLASSIC + sibling fixes); lilium-arena-classic (`ELITEFORCE` = proto-43); ioEF `…\Voyager\ioef\` (EF's own `#ifdef ELITEFORCE` source) |
