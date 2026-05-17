/* ps3_input.h -- DS3 controller input via libpad. */

#ifndef PS3_INPUT_H
#define PS3_INPUT_H

void     PS3_Input_Init(void);
void     PS3_Input_Shutdown(void);
void     PS3_Input_Frame(void);
qboolean PS3_Input_QuitPressed(void);
void     PS3_SetRumble(uint8_t large, uint8_t small_motor, int durationMs);

#endif /* PS3_INPUT_H */
