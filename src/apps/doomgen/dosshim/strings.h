/* BSD-style strings.h. We just forward to string.h. */
#ifndef _STRINGS_H_SHIM
#define _STRINGS_H_SHIM
#include <string.h>
static inline void bzero(void* p, unsigned long n) { memset(p, 0, n); }
static inline void bcopy(const void* s, void* d, unsigned long n) { memmove(d, s, n); }
static inline char* index(const char* s, int c)  { return strchr(s, c); }
static inline char* rindex(const char* s, int c) { return strrchr(s, c); }
#endif
