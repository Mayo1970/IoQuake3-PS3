# AGENTS.md — ioquake3-PS3 port (PSL1GHT / devkitPro)

> Agent hand-off doc. Dense by design — read it fully before touching anything.
> End users want [README.md](README.md).

---

## What this project is

A working PS3 port of [ioquake3](https://github.com/ioquake/ioq3) built on the
PSL1GHT homebrew SDK. No GPU OpenGL driver exists on PS3, so the project ships a
custom GL-1.1 → RSX/GCM translation layer (`code/gl/`). All upstream ioq3 sources
are vendored under `code/` and patched in place. One source tree produces **three**
PKGs: Q3A, Open Arena, and Team Arena. It boots, renders, plays online/LAN/bots,
has DS3 input + rumble, OSK, cinematics, and OGG music. Work remaining: perf tuning
and the PS3DK toolchain migration.

---

## Dev environment

- Build requires `PS3DEV` env var set and the devkitPro MSYS2 shell (`ppu-gcc`).
  **You probably cannot build on the dev host** — reason about correctness from
  source when the toolchain is unavailable.
- Run all `make` commands from an MSYS2 bash shell with `PS3DEV` set.
- `make clean && make all-flavors` — builds all three PKGs (Q3A / OA / TA).
- `make clean && make pkg` — Q3A only.
- `make clean && make OA=1 pkg` — Open Arena only.
- `make clean && make TA=1 pkg` — Team Arena only.
- `make clean` wipes `build/`, `build_oa/`, `build_ta/`.
- Append `pkg` for installable PKG, `self` for raw SELF, `install` for FTP layout.
- All `.sh` scripts must use **LF** line endings — CRLF breaks bash on the toolchain.

---

## Editing rules

- Edit `code/<subdir>/<file>` directly — it is the source of truth.
- Never run anything in `legacy_patches/` — it targets a layout that no longer exists.
- Files named `ps3_*` or in `code/gl/ audio/ input/ renderer/ spu/` are PS3-original — edit freely.
- Files with upstream ioq3 names are vendored — keep changes minimal and guard with `#ifdef __PS3__`.
- Stay at **`-O2`** globally — `-O3` breaks boot on this toolchain.
- Keep **`-mno-altivec`** globally — AltiVec is only enabled per-file for `ps3_snd.c`.
- Do **not** swap `vm_powerpc.c` for `vm_none.c` — the stub calls `exit(99)`.
- After any non-obvious fix, append it to this file under the relevant section.

---

## Memory budget — hard constraint

- **~145 MB free user RAM** at boot (GameOS reserves ~88 MB of 256 MB XDR).
- Hunk = **96 MB**, Zone = **24 MB** → 120 MB total, ~25 MB margin.
- **Do not raise hunk above 112 MB** without re-measuring.
- **zone=32 MB was tried and hangs at boot** — keep `DEF_COMZONEMEGS=24`.
- Constants are in [code/sys/ps3_platform.h](code/sys/ps3_platform.h) as **integers**, not strings.
- `common.c`'s `#define DEF_COMHUNKMEGS` / `DEF_COMZONEMEGS` are `#ifndef`-guarded so PS3 overrides win.
- `MAX_CLIENTS` is effectively **64** regardless of `-DMAX_CLIENTS=8` — `q_shared.h` hardcodes it.
- `MAX_RAW_STREAMS` must be ≥ `2*MAX_CLIENTS+1` = 129 or there is an OOB write in `s_rawsamples[]`.

---

## Source layout

```
code/
├── qcommon/ client/ server/ botlib/ game/ cgame/ q3_ui/ ui/
│   renderercommon/ renderergl1/ renderergl2/ null/ asm/ sdl/ thirdparty/
│        ← VENDORED upstream ioq3, patched in place.
│
├── gl/       ← PS3-ORIGINAL. GL-1.1 → RSX/GCM translation layer.
├── audio/    ← PS3-ORIGINAL. ps3_snd.c sole sound dispatcher (snd_main.c NOT compiled).
├── input/    ← PS3-ORIGINAL. DS3 pad, rumble, OSK.
├── renderer/ ← PS3-ORIGINAL. Stub-glue between upstream tr_* and our GL layer.
├── spu/      ← PS3-ORIGINAL. SPU vertex offload experiment (NOT active — see §Perf).
└── sys/
    ├── ps3_main.c ps3_sys.c ps3_glimp.c ps3_platform.h ps3_net.h ps3_setjmp.S
    │        ← compiled. Entry point is ps3_main.c::main().
    ├── include/ ← PS3 shims for headers PSL1GHT lacks (SDL_*.h, endian.h, …).
    └── sys_unix.c sys_main.c …  ← NOT compiled; reference only.
```

---

## Boot order & cvar timing

```
ps3_main.c::main()
  ├─ PS3_Input_Init()      ← BEFORE Com_Init. Must NOT call Cvar_Get here.
  │                          Input cvars go in IN_Init (called after Com_Init).
  ├─ Sys_GetCurrentUser()  ← XMB nickname → injected as +set name "<nick>"
  ├─ PS3_SetupFilesystem() ← probes /dev_hdd0/data/<gamedir>/pak0.pk3
  ├─ Sys_PlatformInit()    ← free-mem probe + FPU setup
  └─ Com_Init(cmdline)     ← engine start; cmdline beats Cvar_Get defaults
        └─ … GLimp_Init → IN_Init (register input cvars here) …
```

- **Never call `Cvar_Get` before `Com_Init`** — it was a real boot crash.
- Boot cmdline in `ps3_main.c` carries all PS3 tuning; `Cvar_Set` from `Sys_Init` gets overwritten.

---

## Boot cmdline — key injected cvars

| Cvar | Value | Reason |
|---|---|---|
| `com_hunkMegs` / `com_zoneMegs` | via ps3_platform.h | Memory budget |
| `g_doWarmup` | `0` | Kills Q3A warmup `map_restart` loop (cgame reload ~5–7 s, serverId race) |
| `com_maxfps` | `60` | Uncapped fills GCM buffer → `rsxFlushBuffer` stalls mid-frame |
| `max_routingcache` | `6144` KB | Botlib routing-cache headroom (default 4096) |
| `r_customwidth/height` | `1280`/`720` | Resolution (see §Perf for 480p trade-off) |
| `cl_allowDownload` | `1` | UDP pk3 downloads from servers |
| `name` | XMB nickname | From `Sys_GetCurrentUser` |
| `fs_game missionpack` | TA only | TA loads via fs_game; BASEGAME stays `baseq3` |
| `com_logfile` | `2` debug / `0` release | ioq3's qconsole.log writer |

---

## Rendering pipeline

- Hot path: upstream `tr_*` (renderergl1) → `code/renderer/qgl_ps3.c` → `code/gl/ps3gl_*.c` → RSX.
- `ps3gl_draw.c::ps3gl_DrawElements` is the critical loop.
- Dual-texture lightmap: `RB_StageIteratorLightmappedMultitexture` needs `PS3GL_TENV_MODULATE2` key (tex0×tex1). If lightmaps render flat-lit, `ps3gl_shader_key()` is only looking at TMU0.
- Mirror/portal: RSX has no fixed-function clip plane. Software triangle culling is in `ps3gl_draw.c`. It must use the **world-space** `portalPlane` via `ps3gl_SetWorldClipPlane` (`#ifdef __PS3__` in `tr_backend.c`) — using eye-space `plane2` gets the wrong sign and blacks the mirror.
- Do **not** split the vertex interleaving loop — RSX ring is write-combined; multiple passes thrash WC hardware.

---

## Sound pipeline

- `snd_main.c` is **not compiled**. `code/audio/ps3_snd.c` calls `S_Base_*` from `snd_dma.c` directly.
- `S_Update` must call **`S_Base_Update()`**, not `S_Update_()` — the raw call skips `S_UpdateBackgroundTrack` and music never plays.
- Any console command `snd_main.c` would register (`play`, `music`, `soundlist`, `soundinfo`) must be registered in `ps3_snd.c::S_Init` — otherwise the client forwards it to the server as an unknown command.
- `s_musicVolume` is defined in `ps3_snd.c` and resolved via `extern` in `snd_local.h`.
- OGG music uses vendored libvorbis 1.3.7 (`code/thirdparty/libvorbis-1.3.7/`). No decoder-only split exists — `block.c` pulls `lpc/lsp/psy/envelope/bitrate/analysis.c`; all are compiled.

---

## QVM execution

- All QVMs (cgame/ui/qagame) use the **PPC JIT** in [code/qcommon/vm_powerpc.c](code/qcommon/vm_powerpc.c).
- Ported from the Wii (upstream ioq3 PPC JIT by Przemyslaw Iskra). Significant framerate gain.
- PS3 changes: `mmap`→`malloc`, `munmap`→`free` (`#ifdef __PS3__`); cache flush via inline `dcbst`/`sync`/`icbi`/`isync`; `gettimeofday`/`timersub` guarded out (not in PSL1GHT newlib).
- Do **not** replace with `vm_none.c` — its `VM_Compile`/`VM_CallCompiled` call `exit(99)`.

---

## Three-variant build (Q3 / OA / TA)

Critical invariants — getting these wrong causes networking or boot failures:

- `STANDALONEOA` must **NOT** define `STANDALONE` (OA uses the legacy networking path).
- `STANDALONETA` **does** define `STANDALONE` (TA uses Q3 assets and the same auth path).
- OA `GAMENAME_FOR_MASTER` must be **`"Quake3Arena"`**, not `"openarena"` — real OA 0.8.8 uses the legacy Q3 gamename. Wrong value → "Game mismatch" rejecting all real OA servers/clients.
- OA `PROTOCOL_LEGACY_VERSION 71` + `AUTHORIZE_SERVER_NAME "dpmaster.deathmask.net"` live in `#ifdef STANDALONEOA` nested inside `qcommon.h`'s `#ifndef STANDALONE`.
- OA `CINEMATICS_LOGO` must be `"idlogo.roq"` (in `q_shared.h`) — OA 0.8.8 paks ship `video/idlogo.roq`.
- TA `BASEGAME` stays `"baseq3"` — setting it to `"missionpack"` crashes at `FS_CheckPak0`.
- `FS_CheckPak0` is guarded off for OA/TA — OA's baseoa checksum doesn't match Q3's.
- `md5.c` must be compiled in for all variants — `cl_guid` must be a valid 32-char hex string or OA qagame rejects connect with "Invalid GUID".
- If cvars misbehave after a gamename/protocol change, delete the on-PS3 `*config.cfg` — stale configs override defaults.

---

## On-disk layout on the PS3

EBOOT in the game container; **game data + log under `/dev_hdd0/data/`** (survives PKG reinstall).

```
/dev_hdd0/game/<TITLE_ID>/USRDIR/EBOOT.BIN
/dev_hdd0/data/ioq3/
  ├── baseq3/pak0.pk3 …          ← Q3 + TA (bring your own paks)
  ├── missionpack/pak0–3.pk3     ← TA only
  ├── baseoa/pak0.pk3 …          ← OA only
  ├── qkey                       ← auto-created at first boot
  ├── q3config.cfg / oaconfig.cfg / teamarenaconfig.cfg
  └── log.txt / log_oa.txt / log_ta.txt   ← debug builds only
```

USB fallback: `/dev_usb000/quake3/<gamedir>/pak0.pk3`.
Path call sites: `ps3_log_path`, `ps3_base_path`, `PS3_SetupFilesystem` (`ps3_main.c`); `ps3_basepath` (`ps3_sys.c`).

---

## Performance — current state

**Two independent bottlenecks, both fixed:**

1. **CPU / bots** — botlib routing-cache LRU thrashing the zone allocator. Fix: proactive eviction in `AAS_AllocRoutingCache` ([code/botlib/be_aas_route.c](code/botlib/be_aas_route.c)). `max_routingcache` 6144 KB in boot cmdline.
2. **GPU / fill rate** — RSX fragment fill rate at 720p. Fix: resolution drop to 480p gives headroom for bots + picmip 0. 720p is viable bot-free.

> ⚠ **Doc-drift:** engineering notes contain both "720p current" and "480p current" from different sessions. Verify actual `VIDEO_RESOLUTION_*` in `ps3_glimp.c::PS3_RSX_Init` and `r_customwidth/height` in the boot cmdline before trusting either. Whoever next builds on hardware should make source and docs agree.

**Other perf wins shipped:**
- PPC JIT (`vm_powerpc.c`) — largest gain.
- `glIndex_t` → `uint16` / `GL_UNSIGNED_SHORT` in `tr_local.h`.
- VMX int16→float32 audio conversion in `ps3_snd.c` (8 samples/iter). Buffer needs `aligned(16)`; compile with `-maltivec` per-file. Use `vec_madd(a,b,vzero)` not `vec_mul` (maps to VSX on this GCC). Use `usleep`, not `sysUsleep`.
- `rsxSetSurface` dedup (`ps3_current_rt` skips redundant GCM command).
- Direct vertex bind: `rsxAddressToOffset` on Q3's arrays → `GCM_LOCATION_CELL`; copy fallback retained.
- Frame cap 60 (prevents GCM buffer overflow).
- Branch-hoisted vertex loop in `ps3gl_draw.c`.

**Dead ends — do not retry:**
- VMX vertex loop (`vec_lvsl`/`vec_ld`/`vec_perm`): union round-trip causes ~40-cycle load-hit-store stall on PPE's in-order pipeline. Reverted.
- VMX sound mixer (`snd_altivec.c`): unaligned `*(vector short*)&samples[...]` — same stall. **Do not re-enable.**
- `r_dynamic 0` / `r_fastsky 1`: no measurable gain.
- `-O3`: boot failure. Stay at `-O2`.
- Splitting the vertex interleaving loop: WC memory; multiple passes thrash hardware.
- **SPU vertex offload** (`code/spu/`): SPU MFC DMA can only reach the 32 MB RSX IO aperture (`0x50100000–0x70100000`); game data lives at `~0x10000000`. The SPU faults silently on `mfc_get`. **Do not reattempt without pre-staging all source data in `rsxMemalign`'d buffers inside the aperture.**

---

## DS3 controls & rumble

- Button aliases in `code/client/cl_keys.c` (`#ifdef __PS3__`, before generic `JOY*`): CROSS=K_JOY1, CIRCLE=K_JOY2, SQUARE=K_JOY3, TRIANGLE=K_JOY4, L1=K_JOY5 … R3=K_JOY10, SELECT=K_JOY11.
- Console layout: `SELECT+TRIANGLE` toggles console; `CROSS` (console open) opens OSK + auto-submits as command; `CIRCLE` closes console; `SELECT+CROSS` opens chat.
- Rumble: `PS3_SetRumble(large, small, ms)` in `ps3_input.c`. DS3 small motor is **binary** — clamp all small values to `(sm > 0) ? 1 : 0`. `PS3_RumbleTick()` zeroes motors when pulse expires.
- Rumble triggers in `snd_dma.c` (`#ifdef __PS3__`): hit `(80,1,90ms)`, own pain `(200,1,180ms)`, rocket `(255,1,160ms)`, shotgun `(230,1,130ms)`, other weapon `(100,1,70ms)`.
- `L3+R3` combo toggles `ps3_rumbleEnable` cvar. Cvars: `ps3_rumbleEnable` (default 1), `ps3_rumbleScale` (default 1.0), both `CVAR_ARCHIVE`.

---

## Custom playlist music

`CG_LoadCustomMusic()` in `code/cgame/cg_main.c` (called from `CG_StartMusic()`). Search order:
1. `playlist_<mapname>.cfg`
2. `autoexec_<mapname>.cfg`
3. `playlist.cfg`

Format: one path per line, `#`/`//` comments, optional `random` keyword picks a random track. Returns `qfalse` → falls back to `CS_MUSIC`. Buffer is 8192-byte `static char` in BSS (no hunk impact).

---

## Shaders

- Sources: `code/gl/shaders/q3_vp.vcg`, `q3_fp_*.fcg`. Only recompile if editing them.
- Build pipeline: `cgc.exe` (Cg Toolkit 3.1, 32-bit, `-profile vp40/fp40`) → ARB asm → `cgcomp -a` → `.vpo`/`.fpo` → embedded in `code/gl/ps3gl_shader_data.h`.
- `compile_shaders.sh` prefers `rsx-cg-compiler` (PS3DK) if present, falls back to cgc+cgcomp.
- `q3_fp_modulate2.fcg` (`tex0*tex1`) added for the dual-texture lightmap fix.

---

## Toolchain — PS3DK migration (future, currently blocked)

Current: **devkitPro** (`ppu-gcc` GCC 7.2, LP64). Candidate: **PS3DK** (GCC 12, at `E:\…\DEVkits\PS3DK`).

**Blocked:** PS3DK GCC 12 defaults to ILP32 (4-byte pointers); ioq3 assumes LP64. `-mlp64` restores LP64 but PS3DK's LP64 lib tree only has `__cell*` FNID stubs — no PSL1GHT wrappers (`audioInit`, `netInitialize`, etc.). Not worth mapping until PS3DK ships a PSL1GHT compat layer.

The Makefile auto-detects PS3DK (`powerpc64-ps3-elf-gcc`) vs devkitPro (`ppu-gcc`) and falls back correctly. Migration shim code is staged. **Leave CMake alone** — the three-flavor Makefile works.

When PS3DK LP64 + PSL1GHT compat is eventually available:
- Verify boot with `-mlp64`.
- Run `compile_shaders.sh` with `rsx-cg-compiler` to regenerate `ps3gl_shader_data.h` with `CLP0` hardware clip output.
- Verify mirror/portal rendering after recompile.

---

## Symptom → root cause index

When something breaks, check here first.

| Symptom | Root cause & fix |
|---|---|
| **Boot crash / OOM** | Hunk/zone too big (§Memory); `Cvar_Get` before `Com_Init`; `#ifndef` guard missing on `DEF_COMHUNKMEGS` |
| **Boot hang** | zone=32 MB; `-O3` |
| **Menu cursor freezes** | `PS3_Input_Frame` early-returning on `padData.len==0` (safe to reprocess last frame). Also: squared curve `fx²` → use linear. (`ps3_input.c`) |
| **Menu confirm/back double-fires** | Cross/Circle emitting both `K_JOY*` and `K_ENTER`/`K_ESCAPE` in one frame. Fix: when `in_menu`, suppress `K_JOY*`. |
| **OSK command broadcast as chat** | `Console_Key` con_autochat prepends `cmd say` when no leading `/`. Fix: `PS3_OSK_Open` takes `prependSlash`; inject `/` in console context. |
| **"unknown cmd play/music/soundlist"** | Not registered in `ps3_snd.c::S_Init` (snd_main.c isn't compiled). |
| **Music opens but is silent** | `S_Update` calling `S_Update_()` instead of `S_Base_Update()`. |
| **Mirror black / whole world rendered** | Using eye-space `plane2` instead of world-space `portalPlane` — wrong sign, all triangles culled. |
| **Lightmaps flat-lit** | `ps3gl_shader_key()` only looked at TMU0; use `PS3GL_TENV_MODULATE2`. |
| **OA "Game mismatch"** | `com_gamename` not "Quake3Arena", or `STANDALONE` accidentally defined for OA. Delete `oaconfig.cfg`. |
| **OA no intro cinematic** | `CINEMATICS_LOGO` not `"idlogo.roq"`. |
| **Networking 128 MB OOB read** | PSL1GHT socket fd has bit 30 set; newlib `FD_ISSET` indexes `fds_bits[fd/64]`. Skip `FD_ISSET` when `fdr==NULL`. (`net_ip.c`) |
| **UDP send returns ENOENT** | PSL1GHT `sockaddr_in` has BSD-style `sin_len` at offset 0; ioq3 leaves it 0. Set it. |
| **"Invalid GUID" on OA connect** | `md5.c` is a stub or missing. Must be compiled in. |
| **`umask` compile error** | Unusable from PSL1GHT newlib — guarded out in `Com_WriteCDKey`. |
| **CDKey screen appears** | `cl_cdkey` should be pre-seeded with `"wj7cplhs2gp3ac3a"`. Don't add a CDKey UI. |

---

## Where to look

| Need | File |
|---|---|
| End-user install / controls / build | [README.md](README.md) |
| Engine entry point | [code/sys/ps3_main.c](code/sys/ps3_main.c) |
| PS3 tuning constants | [code/sys/ps3_platform.h](code/sys/ps3_platform.h) |
| GL→RSX hot path | [code/gl/ps3gl_draw.c](code/gl/ps3gl_draw.c) |
| Sound dispatch | [code/audio/ps3_snd.c](code/audio/ps3_snd.c) |
| Input / rumble / OSK | [code/input/](code/input/) |
| Build logic / variant flags | [Makefile](Makefile) |
| PPC JIT | [code/qcommon/vm_powerpc.c](code/qcommon/vm_powerpc.c) |
| SPU offload analysis (inactive) | [code/spu/](code/spu/) + §Performance |
