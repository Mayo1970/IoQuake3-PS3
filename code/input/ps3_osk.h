/* ps3_osk.h -- PS3 system on-screen keyboard via sysutil/osk. */

#ifndef PS3_OSK_H
#define PS3_OSK_H

#include <ppu-types.h>
#include "qcommon/q_shared.h"

void     PS3_OSK_Init(void);
void     PS3_OSK_Shutdown(void);

/* Open the on-screen keyboard. No-op if already open.
 * title:        ASCII prompt shown above the input area.
 * maxlen:       maximum characters the user can type (clamped to 255).
 * start_text:   initial text shown in the OSK field (NULL = empty).
 * autoSubmit:   if qtrue, send Enter after injecting the result text.
 * prependSlash: if qtrue, inject a leading '/' so Console_Key treats
 *               the result as a command, not chat.
 * fieldClear:   if qtrue, inject K_END + 80 backspaces before the result
 *               so the target menu field is cleared first. */
void     PS3_OSK_Open(const char *title, int maxlen, const char *start_text,
                      qboolean autoSubmit, qboolean prependSlash,
                      qboolean fieldClear);

/* Called from the sysutil callback to deliver OSK events. */
void     PS3_OSK_SysutilCallback(u64 status, u64 param);

/* Returns qtrue while the OSK overlay is visible or finishing. */
qboolean PS3_OSK_IsActive(void);

#endif /* PS3_OSK_H */
