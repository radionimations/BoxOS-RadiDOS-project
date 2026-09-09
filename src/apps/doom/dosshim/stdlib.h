#ifndef _STDLIB_H_SHIM
#define _STDLIB_H_SHIM
#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX     0x7FFFFFFF

void* malloc (size_t n);
void* calloc (size_t n, size_t sz);
void* realloc(void* p, size_t n);
void  free   (void* p);
void  exit   (int code);
void  abort  (void);
int   atoi   (const char* s);
long  atol   (const char* s);
/* abs/labs intentionally omitted: FastDoom's std_func.h defines abs as a macro */
long  labs   (long x);
char* getenv (const char* name);
int   system (const char* cmd);
int   rand   (void);
void  srand  (unsigned seed);
void  qsort  (void* base, size_t n, size_t sz,
              int (*cmp)(const void*, const void*));
int   atexit (void (*fn)(void));

#endif
