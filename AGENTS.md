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
  LP64). Matt tests on hardware himself.
- **The toolchain IS installed on the dev host**, at
  `C:\devkitPro\msys2\opt\ps3dev` (`ppu-gcc` 7.2, `ppu-ld`, `ppu-objcopy`,
  `ppu-nm`, plus `sprxlinker`/`make_self_npdrm` in `bin/`). Build and inspect
  the ELF locally before asking for a hardware run — guessing from source when
  a link-level answer was one `ppu-readelf` away wastes a test cycle.
- Run all `make` from MSYS2 bash with `PS3DEV` set. Outside that shell `make`
  may not inherit `PSL1GHT`, which only the final link needs; export both
  `PS3DEV` and `PSL1GHT`, or run the link command by hand.

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
  `com_logfile 2`. Release writes no log file. **`make` keys off timestamps, not
  flags, and both configs share `$(BUILD)`** — going from `DEBUG=1` back to
  release without `make clean` (or a separate `BUILD=`) leaves debug objects in
  the PKG. Check with
  `ppu-readelf -p .rodata build/…elf | grep -c 'boot: '` (release must be 0).
- `NATIVE_GAME=1` is the default for `FLAVOR=q3` and `FLAVOR=ta` and links
  `code/game` into the EBOOT (see §QVM). `NATIVE_GAME=0` forces the QVM back.
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

- **~169 MB free user RAM** at boot, ~167 MB with the native qagame linked in,
  **~166 MB** with the 1.5 MB JIT arena too (measured on Q3; the arena takes
  its size off the pool in every flavor, §QVM). The EBOOT's own segments, the
  8 MB RSX IO buffer and the 4 MB OSK container are already subtracted,
  because they all exist before the probe. Logged as
  `[mem] free user memory at Sys_PlatformInit: ~N MB`.
- Hunk = **96 MB**, Zone = **24 MB** → 120 MB. **The remaining ~47 MB is not
  slack** — read the ledger below before spending any of it.
- The RSX IO buffer (`RSX_HOST_SIZE`, `ps3_glimp.c`) is **8 MB**. `rsxInit` keeps
  only the 1 MB command buffer there; every other RSX buffer is in VRAM. The
  PSL1GHT samples' 32 MB costs 24 MB of this pool. Never go below 8 MB: rsx.h's
  64 KB/1 MB defaults failed on HW.

### The sound cache is the largest single bite of the ~47 MB pool

`SND_setup` (`snd_mem.c`) does the single largest malloc of the whole boot:
`com_soundMegs * 1536 * sizeof(sndBuffer)`, and `sizeof(sndBuffer)` is
**2064 B** on this ABI (1024 shorts + next ptr + size + pad). So:

| `com_soundMegs` | malloc |
|---|---|
| 8 (ioq3 default) | **24.19 MB** |
| 6 (PS3 cap) | 18.14 MB |
| 5 | 15.12 MB |

With the old 32 MB IO buffer the pool was ~23 MB, and at 8 that malloc needed
24.19 MB of it. **lv2 hangs instead of returning NULL**, so the console froze
with no XMB return and the log stopped dead on the line before it. Stock Q3
survived only because it had ~25 MB and cleared 24.19 MB by ~0.8 MB; the native
qagame's ~1.9 MB of extra loaded segments ate that margin. This is also the real
reason "+8 MB of static memory hangs at boot despite the ~25 MB margin" — the
margin was never free.

- `PS3_MAX_COMSOUNDMEGS` (**6**, `ps3_platform.h`) is a **hard cap applied in
  `SND_setup`**, not a default. `com_soundMegs` is `CVAR_ARCHIVE`, so a stale
  `q3config.cfg` beats the boot cmdline — a cmdline-only change does nothing on
  a console that already has a config. Set both, as the tree does now.
- Anything that grows the EBOOT's loaded segments, the hunk, or the zone comes
  out of this pool. Budget it against the table above, not against "~47 MB free".
- Measured loaded-segment totals with the native qagame and the JIT arena:
  **Q3 26.13 MB**, **OA 26.66 MB**, **TA 27.17 MB** (1.5 / 1.75 / 2.0 MB of it is
  the arena). TA is the tightest flavor — ~164 MB free, so a ~44 MB pool
  against an 18.14 MB sound cache, leaving **~25.9 MB**. If TA ever freezes on the
  `SND_setup:` log line, drop `PS3_MAX_COMSOUNDMEGS` to 5 (15.12 MB) rather than
  cutting the hunk.
- `SND_setup` logs the request under `__PS3__` and `Com_Error`s on a NULL
  instead of walking the list through it. Keep both; they are what turn this
  failure from a console freeze into a message.
- **Do not raise hunk above 112 MB** without re-measuring. **zone = 32 MB hung at
  boot** when this pool was ~23 MB: 96 + 32 MB left ~17 MB, less than the
  24.19 MB sound cache of that time. With ~167 MB free it would leave ~21 MB after
  today's 18.14 MB cache, but no HW boot has tested it — keep
  `DEF_COMZONEMEGS = 24` until one does.
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

## TOC budget — hard constraint

`ppu-gcc` 7.2 has **no `-mcmodel` option**: every object is small-model and
reaches its globals through `R_PPC64_TOC16_*` relocations, which are a signed
16-bit displacement off `r2`. The whole EBOOT therefore gets **one TOC, capped
at 64 KB** (`.got` in the linked ELF holds `*(.got .toc)`).

- Measured: the engine alone is **0x0F230 (60.5 KB)**. That is only ~3 KB of
  headroom. Assume every new translation unit spends some of it.
- Past 64 KB, `ld` does **not** fail. It silently splits the TOC into groups and
  emits `*.long_branch_r2off.*` trampolines that swap `r2` per call. On PS3 that
  binary **hangs at boot with no XMB return** — a linked-in native qagame did
  exactly this (`.got` went to 0x12B28 / 74.8 KB, 95 r2off stubs).
- `-Wl,--no-multi-toc` is in `LDFLAGS` to make that a **link error** instead
  (`relocation truncated to fit: R_PPC64_TOC16_DS`). **Never remove it.** If it
  fires, shrink TOC use; do not silence it.
- The cure is `-mminimal-toc` on the offending objects (each gets its own
  indirect TOC block). `GAME_CFLAGS` uses it for the native qagame, and the
  `vm_powerpc.o` rule for the JIT (8 B of main TOC instead of 440 B).
- Verify after any link that adds code:
  `ppu-nm build/ioquake3_ps3.elf | grep -c r2off` must be **0**, and
  `ppu-readelf -SW build/ioquake3_ps3.elf | grep ' \.got '` must be < 0x10000.
- Current `.got` per flavor (release), with the native qagame where it applies:
  Q3 0x0F3D0 (3120 B left), TA 0x0F0E8 (3864), OA 0x0F318 (3304), Classic
  0x0F230 (3536), EF 0x0F178 (3720); `DEBUG=1` Q3 0x0F498 (2920). **Every
  flavor has under 4 KB left.** A new translation unit anywhere will trip the
  link guard.

---
## Source layout

```
code/
├── qcommon/ client/ server/ botlib/ game/ cgame/ q3_ui/ ui/
│   renderercommon/ renderergl1/ renderergl2/ null/ asm/ sdl/ thirdparty/
│        ← VENDORED upstream ioq3, patched in place.
├── oa/       ← VENDORED OA 0.8.8 gamecode (native OA qagame only). Nested as
│              oa/code/{game,qcommon,botlib} + oa/ui/ on purpose -- see §QVM.
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
  ├─ PS3_InitDefaultPlayerName() ← caches XMB nickname (see below). Safe here:
  │                          sysUtil only, no Cvar_Get.
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
  sizes matter. `PS3_InitDefaultPlayerName()` (`ps3_main.c`, decl in
  `code/sys/ps3_account.h`) caches it before `Com_Init`; `CL_Init` then passes it
  to `Cvar_Get("name", …)` under `#ifdef __PS3__` as the **compiled-in default**.
  In-game renames still persist (config exec beats a `Cvar_Get` default, which
  only sets `resetString`).
- That is deliberate and must stay a `Cvar_Get` default. The old code did a
  post-`Com_Init` `Cvar_Set("name", nick)` guarded on the value still being
  `"UnnamedPlayer"` — `Cvar_Set` never touches `resetString`, so the setup menu's
  Defaults button (`exec default.cfg` + `cvar_restart`, `q3_ui/ui_setup.c`) reset
  the player name to `"UnnamedPlayer"`. Same class of bug as a `+set name`.

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

## QVM execution — interpreter, JIT behind `ps3_vmjit`, linked-in qagame (Q3 + TA + OA)

**QVMs run on the bytecode interpreter unless `ps3_vmjit` is 2** (not
archived; 1 = compile and log only, 2 = run). Its default comes per flavor from
`VMJIT_DEFAULT` in the Makefile, and it is 2 in all five. `q_platform.h` defines
`HAVE_VM_COMPILED` and the Makefile builds `vm_powerpc.c`, but `VM_Create`
forces `VMI_BYTECODE` while `ps3_vmjit` is 0: `vm_cgame`/`vm_game`/`vm_ui` are
archived at 2, so they alone must never turn the JIT on. Work plan and state
per flavor and VM: `QVM_JIT_TODO.md`. All five flavors run `ui` and `cgame` on
the JIT on hardware, and Classic and EF run qagame on it too (`vm_game 2`). The
full S6-S8 test lists and the frame probe have run on Q3 only.
- Keep that default in `vm.c` (`PS3_VMJIT_DEFAULT`), never on the boot cmdline:
  `Com_StartupVariable` applies cmdline `set`s after `autoexec.cfg`, which would
  take away `set ps3_vmjit 0` as the kill switch.

**Q3's, TA's and OA's qagame is native, linked into the EBOOT**
(`NATIVE_GAME=1`, default for `FLAVOR=q3`/`ta`/`oa` → `-DPS3_NATIVE_GAME`,
cmdline `+set vm_game 0`). `NATIVE_GAME=0` forces any flavor back to its QVM, and
`\vm_game 1` + a `map` does the same at runtime without a rebuild.
Native code is fine because it is in the signed SELF, which lv2 maps executable
at load; only runtime-generated code (JIT) is blocked. There is no dll loader.
- `code/game` + its own `q_shared.c`/`q_math.c` are built with `-DQAGAME
  -fno-common`, partially linked with **plain `ppu-ld -r -T code/sys/ps3_qagame.ld`**
  (gcc's PSL1GHT specs inject lv2.ld and break the script), then `objcopy -G`
  localizes everything except `qagame_dllEntry`/`qagame_vmMain` and the
  `__qagame_{data,bss}_{start,end}` brackets. Without that, game symbols clash
  with the engine and `--wrap,Com_Printf` hijacks the game's `Com_Printf`.
- `Sys_LoadGameDll` (`ps3_sys.c`) restores `.data` from a first-load snapshot and
  zeroes `.bss` on every load. The game needs dll-reload-fresh globals
  (`ai_main.c` `botstates[]` point into the reset `G_Alloc` pool).
- **TA builds the same `code/game` with `-DMISSIONPACK`**, exactly as the Q3 SDK
  builds `missionpack/vm/qagame.qvm`. The define goes on `GAME_CFLAGS` only —
  never on the engine objects. Verify it took: the TA blob has ~40
  kamikaze/harvester/proxmine/obelisk symbols, the Q3 blob has 3.
- `VM_Create` uses the native game only when the found `qagame.qvm` comes from
  `PS3_NATIVE_GAME_DIR` (`FS_SearchPathGameDir`), so a mod's qvm still runs
  interpreted. That dir is **`"missionpack"` for TA** and `BASEGAME` otherwise —
  TA's game ships in the fs_game dir, not in baseq3, and fs_game dirs are
  searched first. Using `BASEGAME` for TA silently runs baseq3's qagame.
- `fixes/missionpack/pak4-ps3.pk3` still ships `vm/qagame.qvm` and must keep it:
  it is the fallback if the native path declines, and the pak also carries
  `cgame.qvm`, `ui.qvm` and the `.menu` files.
- **OA builds its own vendored gamecode, NOT `code/game`.** OA's game is a fork:
  40 sources, 9 with no Q3 counterpart (`g_unlagged.c`, `g_admin.c`, `g_vote.c`,
  `g_killspree.c`, `g_playerstore.c`, `g_fileops.c`, `g_cmds_ext.c`,
  `g_svcmds_ext.c`, `bg_alloc.c`), and gametypes through `GT_DOMINATION`. It lives
  at **`code/oa/`** in the layout `code/oa/code/{game,qcommon,botlib}` +
  `code/oa/ui/menudef.h` — that shape is deliberate, so OA's own relative includes
  (`"../qcommon/q_shared.h"`, `"../botlib/…"`, `"../../ui/menudef.h"`) resolve
  **inside** the vendored tree instead of reaching the engine's copies. Do not
  flatten it, and do not edit the sources. The build list is its own `Q3GOBJ_`
  plus `g_syscalls.c`; `bg_lib.c` is `Q3_VM`-only, and `g_mem.c`/`g_rankings.c`
  sit in the dir but are not in its game build.
- **The revision is the gamecode repo's `oa-0.8.8` branch, and that is not
  optional.** The repo's `main` is **OAX** (OpenArena Expanded, a mod, 2016+): its
  `animNumber_t` has **48** entries against retail 0.8.8's **34**, so a native OAX
  qagame feeds animation indices the retail `cgame.qvm` rejects with
  `bad animation number: 38`. Hardware-confirmed. The native game and the
  interpreted retail cgame share `animNumber_t`, the item list and `playerState_t`
  — the vendored revision must be the one the paks were built from.
- **`openarena-ioq3` is the OA *engine* fork and cannot supply the game.** Its
  `code/game` is Q3's stock game code: no OA gametypes, no OA-only sources, and a
  34-entry animation enum identical to Q3's. Useful as an engine reference only.
- OA game↔engine ABI was checked: `MAX_CLIENTS` 64, `GENTITYNUM_BITS` 10,
  `MAX_GENTITIES` 1024 and `MAX_PS_EVENTS` 2 all agree (the engine's
  `MAX_PS_EVENTS 4` is `ELITEFORCE`-only). OA's botlib headers differ from the
  engine's only in `ELITEFORCE`-guarded declarations and one comment; all
  `ACTION_*` values are identical (OA writes them with 7 hex digits, not 8).
- **EF is NOT possible and must not be attempted.** `ioef/code/game` is Q3's game
  code, not Raven's: 0 of its 33 `.c` files carry an `ELITEFORCE` guard, it has no
  assimilation/borg/tricorder/imod symbols, its file list is Q3's and its gametype
  enum stops at `GT_HARVESTER`. Only `bg_public.h`/`g_public.h` are EF-aware, and
  those are the engine-side trap/struct headers this repo already has. There is no
  Holomatch game source; linking `code/game` as EF's qagame would give Q3 rules,
  items and weapons and disagree with the retail EF `cgame.qvm`.
- Classic keeps its QVM: proto-43, and `code/game` does not match it.
- `ui/menudef.h` at the repo root is required by `code/game` (`../../ui/menudef.h`).

- **A JIT is possible, but only from a `.text` array.** Heap, `.bss`,
  `malloc` and `sysMemoryAllocate` pages are NX; fetching from them kills the
  thread (the app looks frozen, no error). lv2 does not enforce W^X on the
  loaded image, so an array declared `__attribute__((aligned(128),
  section(".text"), used))` is writable and executable. Hardware-proven on
  Matt's PS3 by the flycastPS3 SH4 and ARM7 JITs (`flycastPS3/Dynarec.MD` 3.1).
  `vm_powerpc.c` uses one, `vmjit_arena`, cut into a slot per VM.
- Flush generated code with `dcbst` + `sync` + `icbi` + `isync` over
  **128-byte** Cell lines. Call it through an ELFv1 OPD; never write `r2`.
  Sign-extend (`extsw`) every `int` that generated code hands to C, and
  zero-extend (`clrldi`) every `unsigned`: ELFv1 makes the producer extend it,
  `lwz` zero-extends and an `add` can carry into the high word, so -1 can arrive
  as 4294967295 (flycastPS3 `Dynarec.MD` 6.2, bug 4).
- The `.text` array is part of the EBOOT's loaded segments, so its size comes
  out of the ~47 MB pool (§Memory) even with `ps3_vmjit 0`. Slot sizes per
  flavor are `VMJIT_KB` ("ui cgame qagame", KB) in the Makefile: Q3 1.5 MB, TA
  2.0, OA 1.75, Classic 2.5, EF 3.25 in total. Code is ~6 B per QVM op; a VM
  whose code does not fit its slot stays interpreted. A compiled VM skips the
  interpreter's `codeLength*4` hunk copy (2.2–4.9 MB per flavor).
- `vm_powerpc.c` is built with `-mminimal-toc` (8 B of main TOC). It emits
  straight into the slot with four `Z_Malloc` scratch blocks (<0.5 MB). Never
  go back to upstream's one `Z_Malloc` per op: 12–20 MB of scratch per VM
  against the 24 MB zone (`ZONE_DEBUG` headers, `NDEBUG` is never set) →
  `Z_Malloc: failed`; `VM_SYSTEM_MALLOC` is worse (pool `malloc`, lv2 hangs).
  Any compile error, a full slot included, longjmps back to `PPC_Compile` and
  leaves the VM to the interpreter — never `Com_Error` out of `VM_Create`.
- `ps3_vmjit 2` runs generated code only after `VM_JitSelfTest` passes (small
  QVM programs must give the same result and data on the interpreter and the
  JIT). It runs at `VM_Init`, or before the first compile that may run; a
  failure keeps every VM interpreted for the session.
- Guards replace the interpreter's checks, and `cl_allowDownload 1` lets
  server-supplied QVMs reach them: indirect `OP_CALL`/`OP_JUMP` need an index
  below `instructionCount`, and every `OP_ENTER` needs `programStack` above
  `stackBottom`. A failed guard does `bl` to an error stub, which calls
  `VM_JitError` (`Com_Error(ERR_DROP)`). `OPTIMIZE_COPY` is 0, so every
  `OP_BLOCK_COPY` goes through the checked `VM_BlockCopy`.
- A guard's `bl` goes through the jump list (`emitStubCall`), never inline:
  `PPC_FinishFunction` moves code back when it drops the second word of each
  conditional branch, which breaks any raw displacement out of the function.
- `OP_CVFI` is `fctiw` (rounds to nearest, FPSCR from `Sys_SetFloatEnv`), not
  `fctiwz`: the PS3 interpreter's `Q_ftol` is `lrintf`, and the JIT must match it.
- There is no `mmap` or `mprotect`. Never add a shim that pretends heap memory is
  executable: code that "succeeds" with it runs from NX heap (silent hang).

---

## Rendering pipeline

- Hot path: upstream `tr_*` (renderergl1) → `code/renderer/` qgl glue →
  `code/gl/ps3gl_*.c` → RSX. `ps3gl_draw.c::ps3gl_DrawElements` is the critical loop.
- **There is no tess arena.** Q3's `tess` is a plain `.bss` global
  (`tr_shade.c`), so it is never inside the `rsxInit` IO window and the
  direct-bind path in `ps3gl_DrawElements` can never fire for it — every draw
  takes the interleave-copy into the vring. Keep it that way: an attribute
  pointer must pass `ps3gl_io_mapped()` (range check against
  `ps3_rsx_io_base`/`ps3_rsx_io_size`, published by `ps3_glimp.c`) *before*
  `rsxAddressToOffset`. **`gcmAddressToOffset` does not validate an unmapped
  main-memory address** — it can report success with a garbage offset, and the
  RSX then fetches vertices from nowhere, which is a whole-console freeze on the
  first frame. Trusting its return value alone made the boot survive only by
  luck of where `.bss` landed; linking anything new into the EBOOT moved those
  addresses and froze the console.
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
  silently wrap again. `ps3gl_draw.c`'s direct-bind path has **no fence at
  all**; it is currently unreachable (see the tess note above), so any future
  work that makes it reachable must add one first.
  `gcmGetLabelAddress` returns `u32*` — cast to `volatile u32*`.
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
  dedicated EF delta-field table, never shared with Classic's. **No fix-pak**
  — EF loads Matt's retail `qagame`/`cgame`/`ui` QVMs as-is; `code/game`/`cgame`/
  `q3_ui`/`ui` sources are never compiled for EF (this repo's Makefile has no
  QVM-compile step; `code/game` is only built natively for the Q3 flavor, see
  §QVM). Bots work, but retail EF's `qagame.qvm` bot AI only checks
  `ACTION_ATTACK`, never `ACTION_RESPAWN` — `code/botlib/be_ea.c`'s `EA_Respawn()`
  must set `ACTION_ATTACK` under `#ifdef ELITEFORCE` (matches ioEF), or EF bots
  never respawn. Reference for the compat wire-framing mechanism (shared with
  Classic's proto-43 implementation) is ioEF (`E:\…\Voyager\ioef`,
  `#ifdef ELITEFORCE`).

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

Wins that are live: swizzled textures + mips,
`glIndex_t`→`uint16`, indices in an RSX index ring (`rsxDrawIndexArray`), VMX
int16→float32 audio in `ps3_snd.c` (8/iter, `aligned(16)`, `-maltivec` per-file,
`vec_madd` not `vec_mul`), `rsxSetSurface` dedup, MVP upload dedup, frame cap 60.

The QVM JIT (§QVM), on by default in every flavor. On Q3 with 4-6 bots it cuts
cgame's own time from 5.1 to 0.74 ms per frame, and the frame holds the 60 fps
cap (17.1 ms against 18.8 ms on the interpreter).

**Dead ends — do not retry:**
- PPC JIT from heap memory — NX. A `.text`-array code cache works, see §QVM.
- **SPU vertex offload** (`code/spu/`, `ps3gl_spu.*`, still in-tree, unbuilt).
  Per-draw SPU jobs cost more in mailbox round trips than they save; Q3 sends
  hundreds of small draws per frame. Only whole-frame batches can pay off.
  - SPU threads **can** DMA any user main-memory address (PSL1GHT `spudma`
    sample). They cannot reach RSX local memory (VRAM). The old claim that
    SPU DMA only reaches the RSX IO aperture was a misdiagnosis.
  - The silent SPU fault was `spu_vtx.c` breaking MFC rules: one `mfc_put` of
    up to 36000 B. **One MFC command moves at most 16 KB**, EA and LS address
    must be 16-byte aligned with equal low 4 bits. Split larger transfers.
- VMX vertex loop / VMX sound mixer (`snd_altivec.c`) — unaligned vector loads
  cause ~40-cycle load-hit-store stalls on the PPE's in-order pipe. Reverted; do
  not re-enable `snd_altivec.c`.
- `-O3` (boot failure), `r_dynamic 0`/`r_fastsky 1` (no gain), splitting the
  vertex interleave loop (WC thrash).

The historical .bss-corruption class is **dead**: it was newlib's AltiVec
`setjmp` overflowing the .bss `abortframe` 192 B every `Com_Frame`. Fixed by
`code/sys/ps3_setjmp.S` + the 512 B `jmp_buf` shim in `code/sys/include/setjmp.h`.
Don't re-add sentinels/canaries. `ps3_setjmp.S` rounds the buffer to 8 bytes
(`rldicr 3,3,0,60`); rounding to 16 (`,59`) stored `r1` 8 bytes below any
`jmp_buf` at 8 mod 16, so the variable before it took the stack pointer.

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
| Boot freeze right after `SNDDMA_Init: OK`, no XMB return | `SND_setup`'s sound-cache malloc exceeds the ~47 MB post-hunk/zone pool and lv2 hangs on it. Cap `com_soundMegs` (`PS3_MAX_COMSOUNDMEGS`) — a stale `q3config.cfg` overrides the cmdline (§Memory) |
| Boot hang | hunk + zone + sound cache over the free pool (§Memory); `-O3`; or a multi-TOC split — `.got` over 64 KB makes ld emit `r2off` stubs (§TOC budget) |
| Whole-console freeze mid-render or on the first frame | Misaligned ring/arena carve → bad F32 attribute offset; a ring fence using a command label instead of a backend label; or an attribute bound from `rsxAddressToOffset` without the `ps3gl_io_mapped()` range check |
| App frozen with no message, audio still running, right after a QVM goes through the JIT | A fault in generated code killed the main thread: code outside the `.text` array, a missing I-cache flush, or an unguarded indirect jump (§QVM) |
| Freeze right after `[vmjit] <vm>: compiling`, before its size line | The compile itself faulted: a store into the `.text` slot (W^X may differ on another CFW/HEN) or a compiler bug. `ps3_vmjit 0` gives back the interpreter |
| `[vmjit] self-test FAIL k/11` | The JIT and the interpreter disagree; the line before names the program and the difference. Every VM stays interpreted for the session (§QVM) |
| `Error during initialization` right after the `[vmjit] self-test` lines | A guard error got past the test's catch: `VM_JitError` saw `vmjit_testing` 0. Check that `ps3_setjmp.S` rounds the `jmp_buf` to 8 bytes, not 16 (§Performance) |
| `VM program counter out of range in OP_CALL`/`OP_JUMP`, or `VM stack overflow` | With `ps3_vmjit 2` it can be a JIT guard (`VM_JitError`), which prints the interpreter's own messages. `ps3_vmjit 0` tells the two apart (§QVM) |
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
| OA: `bad animation number: N` from cgame | Native qagame built from the wrong gamecode revision — the gamecode repo's `main` is OAX with 48 animations, retail 0.8.8 has 34. Vendor the `oa-0.8.8` branch (§QVM) |
| OA: wrong pickups, mispredicted movement, or missing gametypes after going native | The vendored `code/oa/` gamecode revision does not match the retail 0.8.8 `cgame.qvm` it still runs interpreted. Confirm with `\vm_game 1` + `map` — if the QVM behaves, it is a revision mismatch, not the native plumbing (§QVM) |
| TA: baseq3 rules in a missionpack match, or "qagame: using linked-in native code" never prints | `PS3_NATIVE_GAME_DIR` (`vm.c`) left at `BASEGAME` for TA, or `-DMISSIONPACK` missing from `GAME_CFLAGS` (§QVM) |
| Debug logging in a release PKG, or no log in a `DEBUG=1` PKG | `make` does not track flag changes and both share `$(BUILD)`. Switching `DEBUG` needs `make clean` or a separate `BUILD=` (§Build) |
| EF: bots never respawn after death | `EA_Respawn()` (`code/botlib/be_ea.c`) sets `ACTION_RESPAWN`, but retail EF's `qagame.qvm` bot AI only tests `ACTION_ATTACK` — must set `ACTION_ATTACK` under `#ifdef ELITEFORCE` instead (Q3/OA/TA/Classic keep `ACTION_RESPAWN`) |

---

## Where to look

| Need | File |
|---|---|
| End-user install / controls | [README.md](README.md) |
| Engine entry point | [code/sys/ps3_main.c](code/sys/ps3_main.c) |
| PS3 tuning constants | [code/sys/ps3_platform.h](code/sys/ps3_platform.h) |
| GL→RSX hot path | [code/gl/ps3gl_draw.c](code/gl/ps3gl_draw.c) |
| QVM JIT / `ps3_prof` frame probe | [code/qcommon/vm_powerpc.c](code/qcommon/vm_powerpc.c), [code/qcommon/vm.c](code/qcommon/vm.c), [QVM_JIT_TODO.md](QVM_JIT_TODO.md) |
| Sound dispatch | [code/audio/ps3_snd.c](code/audio/ps3_snd.c) |
| Input / rumble / OSK / kb / mouse | [code/input/](code/input/) |
| Build logic / variant flags / embedding | [Makefile](Makefile) |
| References | PS4 port `…\ioQuake3-PS4\` (CLASSIC + sibling fixes); lilium-arena-classic (`ELITEFORCE` = proto-43); ioEF `…\Voyager\ioef\` (EF's own `#ifdef ELITEFORCE` source) |
