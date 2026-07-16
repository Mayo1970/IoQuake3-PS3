/* ps3_osk.h -- PS3 system on-screen keyboard via sysutil/osk. */

#ifndef PS3_OSK_H
#define PS3_OSK_H

#include <ppu-types.h>
#include "qcommon/q_shared.h"

void     PS3_OSK_Init(void);
void     PS3_OSK_Shutdown(void);

/* Opens the OSK (no-op if already open). autoSubmit sends Enter after the result,
 * prependSlash prefixes '/' for Console_Key, fieldClear wipes the field via K_END + 80 backspaces. */
void     PS3_OSK_Open(const char *title, int maxlen, const char *start_text,
                      qboolean autoSubmit, qboolean prependSlash,
                      qboolean fieldClear);

/* Called from the sysutil callback to deliver OSK events. */
void     PS3_OSK_SysutilCallback(u64 status, u64 param);

/* Returns qtrue while the OSK overlay is visible or finishing. */
qboolean PS3_OSK_IsActive(void);

#endif /* PS3_OSK_H */
