/* ps3gl_spu.h -- PPE interface to the SPU vertex interleave thread. */

#ifndef PS3GL_SPU_H
#define PS3GL_SPU_H

#include <stdint.h>
#include "../spu/spu_vtx_shared.h"

/* Start the SPU vertex thread. Returns 1 on success, 0 on failure (scalar fallback). */
int  ps3gl_spu_init(void);

/* Stop the SPU vertex thread. */
void ps3gl_spu_shutdown(void);

/* Dispatch a vertex interleave job: copies the descriptor to the shared XDR buffer
 * and mailboxes the SPU, then returns immediately -- SPU runs in parallel. */
void ps3gl_spu_dispatch(const spu_vtx_job_t *job);

/* Blocks until the SPU signals job completion via its outbound mailbox; call before touching the staging buffer. */
void ps3gl_spu_wait(void);

/* XDR staging buffer the SPU writes its output to; copy to the RSX ring after
 * ps3gl_spu_wait() returns. 128-byte aligned, size = SHADER_MAX_VERTEXES * SPU_VTX_VERTEX_SIZE. */
void *ps3gl_spu_staging(void);

/* 1 if SPU thread initialized successfully, 0 if using scalar fallback. */
int  ps3gl_spu_available(void);

#endif /* PS3GL_SPU_H */
