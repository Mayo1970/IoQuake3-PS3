/*
 * ioquake3-PS3: input/ps3_input.h
 * DualShock 3 input declarations.
 */

#ifndef PS3_INPUT_H
#define PS3_INPUT_H

void     PS3_Input_Init(void);
void     PS3_Input_Shutdown(void);
void     PS3_Input_Frame(void);
qboolean PS3_Input_QuitPressed(void);

#endif /* PS3_INPUT_H */
