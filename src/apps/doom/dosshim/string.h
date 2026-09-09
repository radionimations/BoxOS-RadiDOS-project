#ifndef _STRING_H_SHIM
#define _STRING_H_SHIM
#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

void*  memset  (void* dst, int c, size_t n);
void*  memcpy  (void* dst, const void* src, size_t n);
void*  memmove (void* dst, const void* src, size_t n);
int    memcmp  (const void* a, const void* b, size_t n);
void*  memchr  (const void* s, int c, size_t n);

size_t strlen  (const char* s);
char*  strcpy  (char* d, const char* s);
char*  strncpy (char* d, const char* s, size_t n);
char*  strcat  (char* d, const char* s);
char*  strncat (char* d, const char* s, size_t n);
int    strcmp  (const char* a, const char* b);
int    strncmp (const char* a, const char* b, size_t n);
int    strcasecmp (const char* a, const char* b);
int    strncasecmp(const char* a, const char* b, size_t n);
char*  strchr  (const char* s, int c);
char*  strrchr (const char* s, int c);
char*  strstr  (const char* h, const char* n);
char*  strdup  (const char* s);
char*  strtok  (char* s, const char* sep);

/* Watcom-style. FastDoom uses these. */
int    stricmp (const char* a, const char* b);
int    strnicmp(const char* a, const char* b, size_t n);
char*  strupr  (char* s);
char*  strlwr  (char* s);

#endif
