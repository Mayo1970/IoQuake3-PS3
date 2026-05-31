/* ps3_osk.h -- PS3 system on-screen keyboard via sysutil/osk. */

#ifndef PS3_OSK_H
#define PS3_OSK_H

#include <ppu-types.h>
#include "qcommon/q_shared.h"

void     PS3_OSK_Init(void);
void     PS3_OSK_Shutdown(void);

/* Open the on-screen keyboard. No-op if already open.
 * maxlen: maximum characters the user can type (clamped to 256).
 * autoSubmit: if qtrue, send Enter after injecting the result text.
 * prependSlash: if qtrue, inject a leading '/' before the result text
 *               so Console_Key treats it as a command, not chat. */
void     PS3_OSK_Open(int maxlen, qboolean autoSubmit, qboolean prependSlash);

/* Called from the sysutil callback to deliver OSK events. */
void     PS3_OSK_SysutilCallback(u64 status, u64 param);

/* Returns qtrue while the OSK overlay is visible or finishing. */
qboolean PS3_OSK_IsActive(void);

#endif /* PS3_OSK_H */
