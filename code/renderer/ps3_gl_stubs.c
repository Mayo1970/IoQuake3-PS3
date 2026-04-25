/* ps3_gl_stubs.c -- GL version globals required by renderergl1. */

#include "renderercommon/qgl.h"

int qglMajorVersion = 1;
int qglMinorVersion = 1;

/* GLES version globals (declared in qgl.h, not used on PS3 but must exist) */
int qglesMajorVersion = 0;
int qglesMinorVersion = 0;
