/*
 * ps3gl_main.c -- GL-to-RSX layer: initialization, shutdown, frame bracket.
 */

#include "ps3gl.h"
#include <stdio.h>
#include <stdlib.h>

extern void ps3_log(const char *msg);

ps3gl_state_t *ps3gl_ptr = NULL;

/* Per-frame renderer counters (Session 0 instrumentation). */
ps3gl_stats_t ps3gl_stats_cur;
ps3gl_stats_t ps3gl_stats_last;
uint32_t      ps3gl_stats_frames = 0;

gcmContextData *ps3gl_get_ctx(void)
{
    return ps3gl_ptr ? ps3gl.ctx : NULL;
}

void ps3gl_init(gcmContextData *ctx, uint32_t w, uint32_t h)
{
    if (!ps3gl_ptr) {
        ps3gl_ptr = (ps3gl_state_t *)malloc(sizeof(ps3gl_state_t));
        if (!ps3gl_ptr) {
            ps3_log("ps3gl_init: FATAL malloc failed!");
            return;
        }
    }
    memset(ps3gl_ptr, 0, sizeof(ps3gl_state_t));

    ps3gl.ctx      = ctx;
    ps3gl.screen_w = w;
    ps3gl.screen_h = h;
    ps3gl.dirty    = PS3GL_DIRTY_ALL;

    ps3gl.imm.color = ps3gl_pack_color(1.0f, 1.0f, 1.0f, 1.0f);

    /* Default render state */
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

    /* Initialize subsystems */
    ps3gl_vring_init();
    ps3gl_matrices_init();
    ps3gl_textures_init();
    ps3gl_shaders_init();
    ps3gl_states_init();

    printf("[ps3gl] Initialized (%ux%u)\n", w, h);
}

void ps3gl_shutdown(void)
{
    ps3gl_shaders_shutdown();
    ps3gl_textures_shutdown();
    ps3gl_vring_shutdown();

    free(ps3gl_ptr);
    ps3gl_ptr = NULL;

    printf("[ps3gl] Shutdown\n");
}

void ps3gl_begin_frame(void)
{
    if (!ps3gl_ptr) return;

    /* Snapshot last frame's counters, reset for this frame */
    ps3gl_stats_last = ps3gl_stats_cur;
    memset(&ps3gl_stats_cur, 0, sizeof(ps3gl_stats_cur));
    ps3gl_stats_frames++;

    /* Rewind ring buffer to segment 0 for this frame (fence-checked) */
    ps3gl_vring_frame_reset();

    /* Mark all state dirty so it gets pushed at first draw */
    ps3gl.dirty     = PS3GL_DIRTY_ALL;
    ps3gl.mv.dirty  = 1;
    ps3gl.proj.dirty = 1;
    ps3gl.active_shader = -1;
    for (int i = 0; i < PS3GL_MAX_TMUS; i++)
        ps3gl.tmu[i].dirty = 1;
}

void ps3gl_end_frame(void)
{
    if (!ps3gl_ptr) return;

    /* Fence the vertex ring segment this frame finished in; the flip in
     * ps3_glimp.c flushes the command buffer right after. */
    ps3gl_vring_frame_end();
}
