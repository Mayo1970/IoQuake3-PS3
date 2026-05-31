/* ps3gl_spu.h -- PPE interface to the SPU vertex interleave thread. */

#ifndef PS3GL_SPU_H
#define PS3GL_SPU_H

#include <stdint.h>
#include "../spu/spu_vtx_shared.h"

/* Start the SPU vertex thread. Returns 1 on success, 0 on failure (scalar fallback). */
int  ps3gl_spu_init(void);

/* Stop the SPU vertex thread. */
void ps3gl_spu_shutdown(void);

/*
 * Dispatch a vertex interleave job.
 * Copies job descriptor to the shared XDR buffer and sends mailbox to SPU.
 * Returns immediately — SPU works in parallel.
 */
void ps3gl_spu_dispatch(const spu_vtx_job_t *job);

/*
 * Block until the SPU signals job completion via outbound mailbox.
 * Must be called before using the staging buffer contents.
 */
void ps3gl_spu_wait(void);

/*
 * Pointer to the XDR staging buffer where the SPU writes its output.
 * After ps3gl_spu_wait() returns, copy this to the RSX ring buffer.
 * Aligned to 128 bytes, size = SHADER_MAX_VERTEXES * SPU_VTX_VERTEX_SIZE.
 */
void *ps3gl_spu_staging(void);

/* 1 if SPU thread initialized successfully, 0 if using scalar fallback. */
int  ps3gl_spu_available(void);

#endif /* PS3GL_SPU_H */
