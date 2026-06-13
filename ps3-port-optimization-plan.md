# ioQuake3-PS3 — Performance & Correctness Work Plan

Session-by-session plan derived from the full code review. Each session is self-contained,
ordered by **payoff / risk**, and ends with a verification step so you can commit (or revert)
before moving on. One plan-session per chat session; Matt builds and tests on hardware.

**Baseline symptoms:** frame drops "when there's a lot of action" — caused by a combination of
(a) PPE per-vertex repacking on every draw, (b) RSX texture-bandwidth waste (no mips, linear
layout), (c) possible vertex-ring overwrite mid-frame, and (d) QVM interpreter CPU cost.

**Status:** Session 0 DONE (2026-06-12). Session 7 already shipped pre-plan (vm_powerpc.c JIT
is live — see CLAUDE.md); skip it.

---

## Session 0 — Baseline & Instrumentation  [DONE 2026-06-12]

**Goal:** know your numbers before touching anything, and add the counters that later sessions
need to prove themselves.

**Files:** `code/gl/ps3gl_main.c`, `code/gl/ps3gl_vertices.c`, `code/gl/ps3gl_draw.c`,
`code/gl/ps3gl_textures.c`, `code/gl/ps3gl.h`, `code/sys/ps3_glimp.c`

**Implemented:** `ps3gl_stats_t` per-frame counters (draw calls, direct vs fallback
draws/verts, vring bytes + mid-frame wraps, texture binds, clip-culled tris, index
truncation). Snapshot/reset in `ps3gl_begin_frame`. Console command `ps3gl_stats`
(bare = print once; `ps3gl_stats N` = auto-print every N frames; `0` = off).

**Still to capture on hardware (can fold into Session 1 verification):**
1. Baseline benchmark: `timedemo 1; demo four` FPS, plus worst case q3dm7/q3dm17 + 7 bots.
2. `com_speeds 1` to split client/server/renderer ms.
3. Check `direct=` counter — plan predicts 0 (rsxAddressToOffset fails for .bss tess arrays);
   the 2026-05-31 "direct bind confirmed working" note may be wrong.
4. Check ring wrap count > 0 during heavy firefights.

---

## Session 1 — Mipmaps + Swizzled Textures (biggest GPU win)

**Goal:** stop discarding the mip levels Q3 already uploads; switch from linear to swizzled
layout. Expect the largest fill-rate improvement in dense scenes, plus reduced shimmer.

**Files:** `code/gl/ps3gl_textures.c` (only)

**Why:** `ps3gl_TexImage2D` does `if (level != 0) return;` and builds the gcmTexture with
`mipmap = 1` and `GCM_TEXTURE_FORMAT_LIN`. Minified, linear, 32-bit textures thrash the RSX
texture cache exactly when many surfaces are on screen.

**Steps:**
1. **Restructure texture storage for mip chains.** In `ps3gl_texture_t` add:
   `uint8_t num_levels; uint32_t level_offset[12]; uint32_t total_size;`
   On a `level == 0` upload: compute the full pow2 mip-chain size
   (`sum of max(w>>i,1) * max(h>>i,1) * 4`), allocate once with `rsxMemalign(128, total_size)`,
   fill in `level_offset[]`.
   If dimensions change at level 0, free and reallocate (existing logic, extended).
2. **Accept `level > 0`:** convert pixels into `data + level_offset[level]` and track
   `num_levels = max(num_levels, level+1)`. Q3 uploads levels in order, so this is simple.
3. **Swizzle:** convert each level after pixel conversion. Q3 textures are power-of-two, so
   standard RSX swizzling applies (Morton/Z-order interleave of x/y bits; reference
   implementation in PSL1GHT samples and in every PS3 homebrew texture loader). Implement
   `swizzle_argb8(dst, src, w, h)` and run it per level into the RSX allocation
   (convert into a temporary XDR buffer first, then swizzle into VRAM — don't do
   read-modify-write directly in VRAM).
4. **Descriptor:** `format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_SWZ;`
   `mipmap = num_levels;` `pitch = 0` (swizzled textures ignore pitch).
   Keep `LIN` + `mipmap=1` as a fallback path for **non-pow2** textures (cinematics: 256x256
   usually, but the video RoQ target can be odd — check at runtime with
   `(w & (w-1)) || (h & (h-1))`).
5. **TexSubImage2D interaction (cinematics):** sub-updates into a swizzled texture are painful.
   Easiest: textures that ever receive `TexSubImage2D` stay linear/no-mips (flag the texture on
   first sub-update, or keep cinematic-sized textures linear). Also add
   `rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE)` after sub-updates — currently
   missing, can cause stale tiles in videos.
6. **Filtering already works:** `gl_to_gcm_filter` already maps the mipmap filter modes; once
   `mipmap = num_levels`, trilinear/bilinear-mip kicks in with no further change.

Note: `gcmTexture.mipmap` is the mip level COUNT (PSL1GHT header comment is misleading).

**Verify:** timedemo FPS up; distant floors/walls no longer shimmer; cinematics still play;
`r_picmip 0` now affordable? (try it). Watch VRAM usage in the init log — mip chains add ~33%.

**Risk:** medium (swizzle math bugs show up as scrambled textures immediately — easy to spot).

---

## Session 2 — Vertex Ring Safety (fixes action-scene corruption)

**Goal:** make mid-frame ring wrap impossible-or-detected. Likely fixes glitches/instability
during firefights even before it helps FPS.

**Files:** `code/gl/ps3gl_vertices.c`, `code/gl/ps3gl.h`, `code/sys/ps3_glimp.c`

**Why:** `ps3gl_vring_alloc` wraps `head` to 0 when full, overwriting vertices from *this same
frame* that the RSX hasn't fetched yet. 2 MB / 36 B ≈ 58k verts; multipass + dynamic lights +
particles exceeds that.

**Steps:**
1. **Cheap insurance first:** `PS3GL_VRING_SIZE` → `16 * 1024 * 1024` (VRAM is plentiful).
   The Session-0 wrap counter should now read 0 even in worst-case scenes.
2. **Correct fix (do it anyway):** double-segment ring with fences.
   - Split the ring in two halves. At each frame end, after the last draw, write a label:
     `gcmSetWriteCommandLabel(ctx, PS3GL_LABEL_VRING, frame_counter)`.
   - Before reusing a half the GPU might still be reading, spin on
     `gcmGetLabelAddress(PS3GL_LABEL_VRING)` until it shows the fence value, with a
     short `usleep(100)` poll.
   - With double buffering + the existing WaitFlip this almost never actually waits; it exists
     to make overflow *safe* instead of corrupting.
3. **Overflow handling:** if a single frame would exceed the whole ring even at 16 MB,
   log loudly and drop the draw (`return NULL` from `vring_alloc`; callers already check).
   A dropped surface beats corrupted memory.

**Verify:** wrap counter = 0 in worst-case scene; no visual glitches in heavy fights; 30 min
botmatch soak.

**Risk:** low.

---

## Session 3 — Index Path: Stop Pushing Indices Through the FIFO

**Goal:** remove the double index copy and the FIFO bloat from inline index draws.

**Files:** `code/gl/ps3gl_draw.c`, `code/gl/ps3gl_vertices.c` (ring infra reuse), `code/gl/ps3gl.h`

**Why:** current path = `memcpy(idx16, indices, ...)` into a 128 KB static buffer, then
`rsxDrawInlineIndexArray16` copies all of it *again* into the command buffer. A 6000-index draw
injects 12 KB into the FIFO; dozens per frame = megabytes of command traffic and PPE writes.

**Steps:**
1. Create an **index ring** alongside the vertex ring (e.g. 2 MB, `rsxMemalign(128, ...)`,
   same fence scheme as Session 2 — can share the label).
2. In `ps3gl_DrawElements`, write indices once into the index ring (straight `memcpy` —
   Q3's indices are already `uint16_t`), get the RSX offset, and issue:
   `rsxDrawIndexArray(ctx, GCM_TYPE_TRIANGLES, idx_offset, n, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);`
3. The software clip-plane path writes its filtered indices into the same index ring instead
   of the static buffer, then uses the same draw call.
4. Delete `static uint16_t idx16[65536]` (reclaims 128 KB of .bss — relevant to Session 6).
5. Remove the silent `count > 65536` truncation — log if it ever triggers (it shouldn't:
   `SHADER_MAX_INDEXES` = 6000).

**Verify:** identical rendering, timedemo equal or better. Prerequisite cleanup for Session 4.

**Risk:** low.

---

## Session 4 — Kill the Per-Vertex Repack (biggest CPU win)

**Goal:** eliminate the scalar interleave loop that currently runs for **every vertex of every
draw of every pass**. Two stages: a low-risk bulk-memcpy version, then optional true zero-copy.

**Files:** `code/gl/ps3gl_draw.c`, `code/gl/ps3gl.h` (no .vcg change — attributes are already
separate streams)

**Background (the false assumption):** the comment in `ps3gl_draw.c` claims PSL1GHT maps all
XDR into RSX IO space. It doesn't — only the 32 MB host buffer passed to `rsxInit()` is mapped.
Q3's `tess` arrays live in `.bss`, so `rsxAddressToOffset()` always fails and the "direct"
path is dead code. Session 0's counter proves/disproves this.

### Stage A — non-interleaved bulk copy (do this first)

Q3's source arrays are already contiguous fixed-stride streams:
position 16 B (float4, w = padding), texcoords 8 B (float2), colors 4 B (u8x4).
There is **no need to interleave** — RSX binds each attribute with its own offset and stride.

1. In the fallback path, replace the per-vertex switch/loop with **three or four bulk
   `memcpy` calls** into the (vertex) ring:
   - `memcpy(ring_pos, vp,  num_verts * 16)` → bind POS    stride 16, 4xF32
   - `memcpy(ring_tc0, tp0, num_verts * ts0)` → bind TEX0  stride ts0, 2xF32
   - `memcpy(ring_tc1, tp1, num_verts * ts1)` (only when present)
   - `memcpy(ring_col, cp,  num_verts * 4)`  → bind COLOR stride 4, 4xU8
   Handle the "no color array" case with a small splat loop (rare).
   When Q3 passes `stride 0`, the existing default-stride logic already computes the
   packed stride — copies stay a single memcpy.
2. Fix the latent `size >= 4 ? 4 : 4` bug (always binds 4 components; honor `va_vertex.size`
   or assert it's 4 — in Q3 it always is).
3. Delete (or `#if 0`) the interleaved `ps3gl_vertex_t` fallback. Keep the immediate-mode
   (`glBegin/glEnd`) interleaved path as-is — only used for tiny 2D batches.

**Expected:** repack cost drops 5-10x (memcpy vs scalar gather/convert).

### Stage B — true zero-copy via mapped arena (optional, bigger surgery)

Only if Stage A measurements say vertex copying is still significant:

1. Carve a dedicated arena out of the already-mapped 32 MB `rsxInit` host buffer (or map a new
   region with `gcmMapEaIoAddress` — 1 MB-aligned, multiple-of-1MB size).
2. Patch the renderer so `tess.xyz`, `tess.svars.texcoords[*]`, `tess.svars.colors` are
   **rotating buffers** allocated from that arena (N >= 4, switched per `RB_EndSurface`) —
   rotation + the Session 2 fence is mandatory.
3. Bind with `GCM_LOCATION_CELL` using the now-working `rsxAddressToOffset`.

**Trade-off to verify on hardware:** RSX pulling attributes from XDR is slower per-fetch than
VRAM; for Q3-scale vertex counts it's normally a win vs PPE copying, but measure. If Stage A
already hits 60 FPS locked, skip Stage B and delete the dead direct-bind code instead.

### Cleanup
- The SPU interleaver (`ps3gl_spu.c`, `spu/spu_vtx.c`) becomes obsolete once interleaving is
  gone — delete it or keep it on a branch.

**Verify:** timedemo FPS (this is where "lots of action" scenes should transform);
`com_speeds` renderer-ms drop; no geometry corruption (fences holding).

**Risk:** Stage A low, Stage B medium.

---

## Session 5 — Frame Pacing & Small GPU/CPU Wins

**Goal:** smooth out the drops that remain after the big two.

**Files:** `code/sys/ps3_glimp.c`, `code/gl/ps3gl_shaders.c`, `code/gl/ps3gl_matrices.c`

**Steps:**
1. **Triple buffering:** `RSX_FB_COUNT 2 → 3` (third color buffer; depth shared).
   With vsync double-buffering, any 17 ms frame hard-drops to 30 FPS; a third buffer absorbs
   spikes. Adjust `ps3_current_fb = (ps3_current_fb + 1) % 3` and the flip/wait logic.
2. **Flip wait granularity:** `usleep(500)` → `usleep(100)`, or register a flip handler
   (`gcmSetFlipHandler`) and wait on a sysSemaphore instead of polling.
3. **Skip redundant MVP uploads:** add `ps3gl.mvp_uploaded` invalidated when
   `mv.dirty || proj.dirty || shader reload`; in `ps3gl_apply_shader`, call
   `rsxSetVertexProgramParameter` only when needed. Saves 17 FIFO words x hundreds of draws
   in the 2D/UI layer.
4. **TexSubImage2D fast path:** for `GL_RGBA` sources with full-width rows into a linear
   texture, replace the per-pixel loop with a per-row convert. Measurable during videos.
5. **Performance-mode cvar profile** (optional `ps3_perf` toggle): `r_dynamic 0`,
   `r_picmip 1`, `cg_marks 0`.

**Verify:** frametime graph shows fewer 33 ms frames; scoreboard cheaper.

**Risk:** low (triple buffering needs careful flip-status rework — test tearing/latency).

---

## Session 6 — Root-Cause the ".bss Corruption" + Hygiene

**Goal:** remove the sentinel/backup band-aids by finding the real bug. Something is zeroing
`.bss` after init — today it hits `ps3gl_ptr` and the GCM context (patched around), tomorrow
it silently hits something without a sentinel.

**Files:** `code/gl/ps3gl_main.c`, `code/sys/ps3_glimp.c`, linker map awareness

**Steps:**
1. Reproduce: add a canary array in `.bss` (`static uint8_t canary[64]`, stamped at init,
   checked every frame).
2. Suspects, in order:
   - a `memset`/`memcpy` with a wrong size near init
   - Q3's `Hunk_Clear` / `Com_TouchMemory` walking past a region
   - thread stack overflow (audio thread stack is 0x4000 — check; SPU mailbox structs)
   - linker placing `.bss` overlapping a DMA target (check map file:
     `-Wl,-Map=output.map`, compare against logged buffer pointers)
3. Once found and fixed, **delete** the sentinel machinery (`s_ps3gl_ptr_backup`,
   `restore_if_needed`, ctx backup) — every frame pays pointer-validation branches in the
   hottest functions.
4. Small fixes batch:
   - `glCopyTexSubImage2D` stub → known-limitations note.
   - `EnableClientState` no-op vs `DisableClientState` NULL-ing pointers: make symmetric
     (track an `enabled` flag instead of NULL-ing).

**Verify:** canary never fires for a full play session → delete sentinels; still stable.

**Risk:** investigative — timebox it; the band-aid works meanwhile.

---

## Session 7 — QVM JIT  [ALREADY DONE pre-plan]

`vm_powerpc.c` JIT is already live on PS3 (see CLAUDE.md "PPC JIT compiler" section,
2026-05-31): mmap→malloc, PS3 cache-flush asm, PPC64 OPD support. Skip this session.

---

## Suggested order & expected impact

| Session | Area | Effort | Expected impact on "action scenes" |
|---|---|---|---|
| 0 | Instrumentation | S | DONE — enables everything else |
| 1 | Mipmaps + swizzle | M | High (GPU fill/bandwidth) |
| 2 | Ring safety | S | Fixes corruption; stability |
| 3 | Index ring | S | Moderate (FIFO/PPE writes) |
| 4A | Bulk-memcpy streams | M | High (PPE per-draw cost) |
| 4B | Zero-copy arena | L | Incremental over 4A — only if needed |
| 5 | Pacing + misc | S | Smoothness (fewer 30 FPS drops) |
| 6 | BSS root cause | ? | Stability + removes hot-path checks |
| 7 | PPC JIT | — | Already shipped (vm_powerpc.c) |

After 1 + 4A, re-measure before deciding whether 4B is worth the renderer surgery.
