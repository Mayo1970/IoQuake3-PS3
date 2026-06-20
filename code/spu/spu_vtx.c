/*
 * spu_vtx.c -- SPU vertex interleave program.
 *
 * Compiled with spu-gcc. Runs as a persistent SPU thread that blocks
 * on the inbound mailbox waiting for jobs from the PPE.
 *
 * Protocol (per job):
 *   PPE sends 3 inbound mailbox words:
 *     [0] SPU_VTX_CMD_JOB
 *     [1] descriptor EA high 32 bits
 *     [2] descriptor EA low  32 bits
 *   SPU:
 *     DMA-get descriptor (128 bytes, tag 0)
 *     DMA-get source arrays into LS (tag 1, serialized per array)
 *     interleave into LS output buffer
 *     DMA-put output to XDR staging buffer (tag 2)
 *     write outbound mailbox: SPU_VTX_DONE
 *
 * IMPORTANT: dst_ea must point to XDR (normal host memory).
 * SPU DMA cannot reach RSX VRAM. The PPE copies staging→RSX ring after wait.
 *
 * Local store layout:
 *   ls_desc  [128 B, 128-aligned]  job descriptor
 *   ls_pos   [16 KB, 16-aligned]   position array  (1000 × 16)
 *   ls_tc0   [16 KB, 16-aligned]   texcoord 0      (1000 × 16 worst-case)
 *   ls_tc1   [16 KB, 16-aligned]   texcoord 1
 *   ls_col   [ 4 KB, 16-aligned]   color array     (1000 × 4)
 *   ls_out   [36 KB, 128-aligned]  interleaved output (1000 × 36 = 36000, pad to 36096)
 *   Total: ~88 KB of 256 KB LS used.
 */

#include <stdint.h>
#include <string.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>

#include "spu_vtx_shared.h"

#define TAG_DESC  0
#define TAG_SRC   1
#define TAG_OUT   2

/* Round size up to next multiple of 16 for DMA. */
static inline uint32_t dma_size(uint32_t n)
{
    return (n + 15u) & ~15u;
}

static inline void dma_get(void *ls, uint64_t ea, uint32_t size, uint32_t tag)
{
    mfc_get(ls, ea, dma_size(size), tag, 0, 0);
}

static inline void dma_put(void *ls, uint64_t ea, uint32_t size, uint32_t tag)
{
    mfc_put(ls, ea, dma_size(size), tag, 0, 0);
}

static inline void dma_wait(uint32_t tag)
{
    mfc_write_tag_mask(1u << tag);
    mfc_read_tag_status_all();
}

static void interleave(
    const spu_vtx_job_t *job,
    const uint8_t *pos_ls,
    const uint8_t *tc0_ls,
    const uint8_t *tc1_ls,
    const uint8_t *col_ls,
    uint8_t *out_ls)
{
    const int n       = (int)job->num_verts;
    const int has_z   = (job->pos_size >= 3);
    const int has_tc0 = (job->tc0_ea != 0);
    const int has_tc1 = (job->tc1_ea != 0);
    const int has_col = (job->col_ea != 0);
    const uint32_t imm = job->imm_color;
    const int vs  = (int)job->pos_stride;
    const int ts0 = (int)job->tc0_stride;
    const int ts1 = (int)job->tc1_stride;
    const int cs  = (int)job->col_stride;

    uint8_t *dst = out_ls;

    if (has_tc0 && has_tc1 && has_col) {
        for (int i = 0; i < n; i++) {
            const float   *p  = (const float *)(pos_ls + i * vs);
            const float   *t0 = (const float *)(tc0_ls + i * ts0);
            const float   *t1 = (const float *)(tc1_ls + i * ts1);
            const uint8_t *c  = col_ls + i * cs;
            float *o = (float *)dst;
            o[0] = p[0]; o[1] = p[1]; o[2] = has_z ? p[2] : 0.0f; o[3] = 1.0f;
            o[4] = t0[0]; o[5] = t0[1];
            o[6] = t1[0]; o[7] = t1[1];
            *(uint32_t *)(dst + 32) = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16)
                                    | ((uint32_t)c[2] << 8)  |  (uint32_t)c[3];
            dst += SPU_VTX_VERTEX_SIZE;
        }
    } else if (has_tc0 && has_col) {
        for (int i = 0; i < n; i++) {
            const float   *p  = (const float *)(pos_ls + i * vs);
            const float   *t0 = (const float *)(tc0_ls + i * ts0);
            const uint8_t *c  = col_ls + i * cs;
            float *o = (float *)dst;
            o[0] = p[0]; o[1] = p[1]; o[2] = has_z ? p[2] : 0.0f; o[3] = 1.0f;
            o[4] = t0[0]; o[5] = t0[1];
            o[6] = 0.0f;  o[7] = 0.0f;
            *(uint32_t *)(dst + 32) = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16)
                                    | ((uint32_t)c[2] << 8)  |  (uint32_t)c[3];
            dst += SPU_VTX_VERTEX_SIZE;
        }
    } else {
        for (int i = 0; i < n; i++) {
            const float *p = (const float *)(pos_ls + i * vs);
            float *o = (float *)dst;
            o[0] = p[0]; o[1] = p[1]; o[2] = has_z ? p[2] : 0.0f; o[3] = 1.0f;
            if (has_tc0) {
                const float *t0 = (const float *)(tc0_ls + i * ts0);
                o[4] = t0[0]; o[5] = t0[1];
            } else { o[4] = 0.0f; o[5] = 0.0f; }
            if (has_tc1) {
                const float *t1 = (const float *)(tc1_ls + i * ts1);
                o[6] = t1[0]; o[7] = t1[1];
            } else { o[6] = 0.0f; o[7] = 0.0f; }
            if (has_col) {
                const uint8_t *c = col_ls + i * cs;
                *(uint32_t *)(dst + 32) = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16)
                                        | ((uint32_t)c[2] << 8)  |  (uint32_t)c[3];
            } else {
                *(uint32_t *)(dst + 32) = imm;
            }
            dst += SPU_VTX_VERTEX_SIZE;
        }
    }
}

int main(void)
{
    static uint8_t ls_desc[128]   __attribute__((aligned(128)));
    static uint8_t ls_pos[16384]  __attribute__((aligned(16)));
    static uint8_t ls_tc0[16384]  __attribute__((aligned(16)));
    static uint8_t ls_tc1[16384]  __attribute__((aligned(16)));
    static uint8_t ls_col[4096]   __attribute__((aligned(16)));
    /* 1000 × 36 = 36000; round to 36096 (next multiple of 128) */
    static uint8_t ls_out[36096]  __attribute__((aligned(128)));

    while (1) {
        uint32_t cmd = spu_read_in_mbox();
        if (cmd == SPU_VTX_CMD_QUIT)
            break;

        /* Read descriptor EA as two 32-bit words */
        uint32_t ea_hi = spu_read_in_mbox();
        uint32_t ea_lo = spu_read_in_mbox();
        uint64_t desc_ea = ((uint64_t)ea_hi << 32) | ea_lo;

        /* Fetch descriptor */
        dma_get(ls_desc, desc_ea, sizeof(spu_vtx_job_t), TAG_DESC);
        dma_wait(TAG_DESC);

        const spu_vtx_job_t *job = (const spu_vtx_job_t *)ls_desc;
        const int n = (int)job->num_verts;

        if (n <= 0) {
            spu_write_out_mbox(SPU_VTX_DONE);
            continue;
        }

        /* Fetch source arrays */
        dma_get(ls_pos, job->pos_ea, (uint32_t)(n * (int)job->pos_stride), TAG_SRC);
        dma_wait(TAG_SRC);

        if (job->tc0_ea) {
            dma_get(ls_tc0, job->tc0_ea, (uint32_t)(n * (int)job->tc0_stride), TAG_SRC);
            dma_wait(TAG_SRC);
        }
        if (job->tc1_ea) {
            dma_get(ls_tc1, job->tc1_ea, (uint32_t)(n * (int)job->tc1_stride), TAG_SRC);
            dma_wait(TAG_SRC);
        }
        if (job->col_ea) {
            dma_get(ls_col, job->col_ea, (uint32_t)(n * (int)job->col_stride), TAG_SRC);
            dma_wait(TAG_SRC);
        }

        /* Interleave into LS output buffer */
        interleave(job, ls_pos, ls_tc0, ls_tc1, ls_col, ls_out);

        /* DMA-put result to XDR staging buffer (NOT RSX VRAM) */
        dma_put(ls_out, job->dst_ea, (uint32_t)(n * SPU_VTX_VERTEX_SIZE), TAG_OUT);
        dma_wait(TAG_OUT);

        /* Signal PPE: job done */
        spu_write_out_mbox(SPU_VTX_DONE);
    }

    return 0;
}
