/*
 * spu_vtx_shared.h -- Job descriptor shared between PPE and SPU.
 *
 * Rules:
 *   - All pointer fields are 64-bit EAs, valid on both sides.
 *   - dst_ea points to an XDR staging buffer (NOT RSX VRAM).
 *     SPU DMA cannot reach RSX VRAM; the PPE copies staging→ring after wait.
 *   - Struct is 128-byte aligned and padded to exactly 128 bytes so the SPU
 *     can fetch it in a single 128-byte DMA transfer.
 */

#ifndef SPU_VTX_SHARED_H
#define SPU_VTX_SHARED_H

#include <stdint.h>

/* PPE → SPU inbound mailbox commands */
#define SPU_VTX_CMD_QUIT   0
#define SPU_VTX_CMD_JOB    1

/* SPU → PPE outbound mailbox */
#define SPU_VTX_DONE       1

/* Output vertex stride (must match ps3gl_vertex_t) */
#define SPU_VTX_VERTEX_SIZE 36

/*
 * Layout: x,y,z,w (16) + u0,v0 (8) + u1,v1 (8) + color (4) = 36 bytes.
 *
 * Byte count of fields below:
 *   pos_ea(8) pos_stride(4) pos_size(4)
 *   tc0_ea(8) tc0_stride(4) _pad0(4)
 *   tc1_ea(8) tc1_stride(4) _pad1(4)
 *   col_ea(8) col_stride(4) imm_color(4)
 *   dst_ea(8) num_verts(4) _pad2(4)
 *   = 80 bytes used → 48 bytes padding to reach 128.
 */
typedef struct __attribute__((aligned(128))) {
    uint64_t pos_ea;        /* source position array EA */
    uint32_t pos_stride;    /* bytes between elements (always 16) */
    uint32_t pos_size;      /* floats per vertex (3 or 4) */

    uint64_t tc0_ea;        /* texcoord 0 EA; 0 = none */
    uint32_t tc0_stride;
    uint32_t _pad0;

    uint64_t tc1_ea;        /* texcoord 1 EA; 0 = none */
    uint32_t tc1_stride;
    uint32_t _pad1;

    uint64_t col_ea;        /* color array EA; 0 = use imm_color */
    uint32_t col_stride;
    uint32_t imm_color;     /* fallback RGBA packed color */

    uint64_t dst_ea;        /* XDR staging buffer EA (NOT RSX VRAM) */
    uint32_t num_verts;
    uint32_t _pad2;

    uint8_t  _pad[48];      /* pad to 128 bytes */
} spu_vtx_job_t;

#endif /* SPU_VTX_SHARED_H */
