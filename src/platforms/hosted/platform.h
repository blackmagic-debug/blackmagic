#ifndef __PLATFORM_H
#include "timing.h"

char *platform_ident(void);
void platform_buffer_flush(void);

#define PLATFORM_IDENT() "NONE"
#define SET_IDLE_STATE(x)
#define SET_RUN_STATE(x)

#endif
