/* ps3gl_main.c -- GL-to-RSX layer: initialization, shutdown, frame bracket. */

#include "ps3gl.h"
#include <stdio.h>
#include <stdlib.h>

/* Non-zero init forces .data placement; overwritten in ps3gl_init(). */
ps3gl_state_t *ps3gl_ptr = (ps3gl_state_t *)0x1;

/* .data backup survives BSS corruption (separate ELF section). */
static ps3gl_state_t *s_ps3gl_ptr_backup = (ps3gl_state_t *)0xDEAD;

/* Backup context pointer. */
static gcmContextData *s_gcm_ctx_backup = (gcmContextData *)0xDEAD;

void ps3gl_restore_if_needed(void)
{
    if ((uintptr_t)ps3gl_ptr <= 0x1000) {
        extern void ps3_log(const char *msg);
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "ps3gl_restore: ptr=%p backup=%p",
                 (void*)ps3gl_ptr, (void*)s_ps3gl_ptr_backup);
        ps3_log(dbg);

        if (s_ps3gl_ptr_backup && (uintptr_t)s_ps3gl_ptr_backup > 0x1000) {
            ps3gl_ptr = s_ps3gl_ptr_backup;
            ps3_log("ps3gl_restore: RESTORED from .data backup");
        } else {
            ps3_log("ps3gl_restore: backup invalid, cannot restore!");
        }
    }
}

gcmContextData *ps3gl_get_ctx(void)
{
    ps3gl_restore_if_needed();
    if ((uintptr_t)ps3gl_ptr > 0x1000 && ps3gl_ptr->ctx) return ps3gl_ptr->ctx;
    if ((uintptr_t)s_gcm_ctx_backup > 0x1000)
        return s_gcm_ctx_backup;
    return NULL;
}

void ps3gl_init(gcmContextData *ctx, uint32_t w, uint32_t h)
{
    extern void ps3_log(const char *msg);
    char dbg[256];

    snprintf(dbg, sizeof(dbg), "ps3gl_init: entry ps3gl_ptr=%p &ps3gl_ptr=%p",
             (void*)ps3gl_ptr, (void*)&ps3gl_ptr);
    ps3_log(dbg);

    /* Start as sentinel 0x1 or real pointer from previous init; malloc if needed. */
    if ((uintptr_t)ps3gl_ptr <= 0x1000) {
        ps3gl_ptr = (ps3gl_state_t *)malloc(sizeof(ps3gl_state_t));
        snprintf(dbg, sizeof(dbg), "ps3gl_init: malloc returned %p", (void*)ps3gl_ptr);
        ps3_log(dbg);
        if (!ps3gl_ptr) {
            ps3_log("ps3gl_init: FATAL malloc failed!");
            return;
        }
    }
    memset(ps3gl_ptr, 0, sizeof(ps3gl_state_t));
    snprintf(dbg, sizeof(dbg), "ps3gl_init: after memset ps3gl_ptr=%p", (void*)ps3gl_ptr);
    ps3_log(dbg);

    ps3gl.ctx        = ctx;
    s_ps3gl_ptr_backup = ps3gl_ptr;
    s_gcm_ctx_backup   = ctx;

    snprintf(dbg, sizeof(dbg), "ps3gl_init: backup=%p ctx=%p",
             (void*)s_ps3gl_ptr_backup, (void*)s_gcm_ctx_backup);
    ps3_log(dbg);
    ps3gl.screen_w = w;
    ps3gl.screen_h = h;
    ps3gl.dirty    = PS3GL_DIRTY_ALL;

    ps3gl.imm.color = ps3gl_pack_color(1.0f, 1.0f, 1.0f, 1.0f);

    ps3gl.rs.blend_enable       = 0;
    ps3gl.rs.blend_src          = GCM_ONE;
    ps3gl.rs.blend_dst          = GCM_ZERO;
    ps3gl.rs.alpha_test_enable  = 0;
    ps3gl.rs.alpha_func         = GCM_ALWAYS;
    ps3gl.rs.alpha_ref          = 0;
    ps3gl.rs.depth_test_enable  = 0;
    ps3gl.rs.depth_mask         = 1;
    ps3gl.rs.depth_func         = GCM_LESS;
    ps3gl.rs.cull_enable        = 0;
    ps3gl.rs.cull_face          = GCM_CULL_BACK;
    ps3gl.rs.front_face         = GCM_FRONTFACE_CCW;
    ps3gl.rs.scissor_enable     = 0;
    ps3gl.rs.scissor_x          = 0;
    ps3gl.rs.scissor_y          = 0;
    ps3gl.rs.scissor_w          = w;
    ps3gl.rs.scissor_h          = h;
    ps3gl.rs.vp_x               = 0;
    ps3gl.rs.vp_y               = 0;
    ps3gl.rs.vp_w               = w;
    ps3gl.rs.vp_h               = h;
    ps3gl.rs.depth_near         = 0.0f;
    ps3gl.rs.depth_far          = 1.0f;
    ps3gl.rs.color_mask_r       = 1;
    ps3gl.rs.color_mask_g       = 1;
    ps3gl.rs.color_mask_b       = 1;
    ps3gl.rs.color_mask_a       = 1;
    ps3gl.rs.polyoffset_fill    = 0;
    ps3gl.rs.polyoffset_factor  = 0.0f;
    ps3gl.rs.polyoffset_units   = 0.0f;
    ps3gl.rs.shade_model        = GCM_SHADE_MODEL_SMOOTH;
    ps3gl.rs.stencil_enable     = 0;
    ps3gl.rs.stencil_func       = GCM_ALWAYS;
    ps3gl.rs.stencil_ref        = 0;
    ps3gl.rs.stencil_mask       = 0xFFFFFFFF;
    ps3gl.rs.stencil_fail       = GCM_KEEP;
    ps3gl.rs.stencil_zfail      = GCM_KEEP;
    ps3gl.rs.stencil_zpass      = GCM_KEEP;
    ps3gl.rs.stencil_writemask  = 0xFFFFFFFF;
    ps3gl.rs.clear_color        = 0x00000000;
    ps3gl.rs.clear_depth        = 1.0f;
    ps3gl.rs.clear_stencil      = 0;

    for (int i = 0; i < PS3GL_MAX_TMUS; i++) {
        ps3gl.tmu[i].bound   = NULL;
        ps3gl.tmu[i].enabled = 0;
        ps3gl.tmu[i].texenv  = PS3GL_TENV_MODULATE;
        ps3gl.tmu[i].dirty   = 1;  /* force initial state push */
    }

    ps3gl.matrix_mode = GL_MODELVIEW;

    ps3gl_vring_init();
    ps3gl_matrices_init();
    ps3gl_textures_init();
    ps3gl_shaders_init();
    ps3gl_states_init();

    snprintf(dbg, sizeof(dbg), "ps3gl_init: done ps3gl_ptr=%p backup=%p",
             (void*)ps3gl_ptr, (void*)s_ps3gl_ptr_backup);
    ps3_log(dbg);
    printf("[ps3gl] Initialized (%ux%u)\n", w, h);
}

void ps3gl_shutdown(void)
{
    ps3gl_shaders_shutdown();
    ps3gl_textures_shutdown();
    ps3gl_vring_shutdown();

    free(ps3gl_ptr);
    ps3gl_ptr = NULL;
    s_ps3gl_ptr_backup = NULL;
    s_gcm_ctx_backup = NULL;

    printf("[ps3gl] Shutdown\n");
}

void ps3gl_begin_frame(void)
{
    ps3gl_restore_if_needed();
    if ((uintptr_t)ps3gl_ptr <= 0x1000) return;

    if (!ps3gl.ctx && (uintptr_t)s_gcm_ctx_backup > 0x1000)
        ps3gl.ctx = s_gcm_ctx_backup;

    /* Swap to the other vring segment and wait for the GPU to finish whatever it held
     * last time around -- same one-frame pipeline slack as before, just fenced per segment instead of the whole ring. */
    ps3gl.vring.cur_seg = (ps3gl.vring.cur_seg + 1) % PS3GL_VRING_SEGMENTS;
    {
        int seg = ps3gl.vring.cur_seg;
        volatile uint32_t *label = ps3gl.vring.fence_label[seg];
        if (label) {
            int waited = 0;
            while (*label != ps3gl.vring.fence_val[seg] && waited < 20000) {
                usleep(10);
                waited++;
            }
            if (*label != ps3gl.vring.fence_val[seg])
                printf("[ps3gl] WARNING: vring segment %d fence timed out (val=%u label=%u)\n",
                       seg, ps3gl.vring.fence_val[seg], *label);
        }
    }

    ps3gl.vring.head = 0;

    ps3gl.dirty     = PS3GL_DIRTY_ALL;
    ps3gl.mv.dirty  = 1;
    ps3gl.proj.dirty = 1;
    ps3gl.active_shader = -1;
    /* Reassert VP once per frame too -- sysutil overlays (OSK, dialogs) can
     * rebind their own vertex program behind our active_vp cache's back. */
    ps3gl.active_vp = NULL;
    for (int i = 0; i < PS3GL_MAX_TMUS; i++)
        ps3gl.tmu[i].dirty = 1;
}

void ps3gl_end_frame(void)
{
    /* Backend label write, after this frame's draws: RSX only writes fence_val to fence_label
     * once it has drained every preceding vertex fetch -- begin_frame seeing this value is what makes the segment safe to reuse. */
    gcmContextData *ctx = ps3gl_get_ctx();
    int seg = ps3gl.vring.cur_seg;
    if (ctx && ps3gl.vring.fence_label[seg]) {
        ps3gl.vring.fence_val[seg]++;
        rsxSetWriteBackendLabel(ctx,
            seg == 0 ? PS3GL_LABEL_VRING_SEG0 : PS3GL_LABEL_VRING_SEG1,
            ps3gl.vring.fence_val[seg]);
    }
}
