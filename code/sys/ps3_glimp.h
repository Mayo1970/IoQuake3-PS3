/*
 * ioquake3-PS3: sys/ps3_glimp.h
 * RSX display initialization and frame management declarations.
 */

#ifndef PS3_GLIMP_H
#define PS3_GLIMP_H

void PS3_RSX_Init(void);
void PS3_RSX_Shutdown(void);
void PS3_RSX_BeginFrame(void);
void PS3_RSX_EndFrame(void);

#endif /* PS3_GLIMP_H */
