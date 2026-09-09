#ifndef _CONIO_H_SHIM
#define _CONIO_H_SHIM
#include "boxos_compat.h"
static inline int kbhit(void) { return 0; }
static inline int getch(void) { return 0; }
#endif
