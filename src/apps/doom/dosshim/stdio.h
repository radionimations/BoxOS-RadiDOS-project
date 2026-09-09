/* Minimal hosted-libc stdio for FastDoom on BoxOS. */
#ifndef _STDIO_H_SHIM
#define _STDIO_H_SHIM
#include <stddef.h>
#include <stdarg.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EOF (-1)
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define BUFSIZ 1024

typedef struct FILE FILE;
extern FILE* const stdin;
extern FILE* const stdout;
extern FILE* const stderr;

int   printf  (const char* fmt, ...);
int   sprintf (char* buf, const char* fmt, ...);
int   snprintf(char* buf, size_t n, const char* fmt, ...);
int   vprintf (const char* fmt, va_list ap);
int   vsprintf(char* buf, const char* fmt, va_list ap);
int   fprintf (FILE* f, const char* fmt, ...);
int   puts    (const char* s);
int   putchar (int c);
int   fputc   (int c, FILE* f);
int   fputs   (const char* s, FILE* f);
int   fflush  (FILE* f);

FILE* fopen   (const char* path, const char* mode);
int   fclose  (FILE* f);
size_t fread  (void* buf, size_t sz, size_t n, FILE* f);
size_t fwrite (const void* buf, size_t sz, size_t n, FILE* f);
int    fseek  (FILE* f, long off, int whence);
long   ftell  (FILE* f);
int    feof   (FILE* f);
int    fgetc  (FILE* f);
char*  fgets  (char* s, int n, FILE* f);
int    ferror (FILE* f);
void   clearerr(FILE* f);
int    setvbuf(FILE* f, char* buf, int mode, size_t sz);
int    rename (const char* from, const char* to);
int    remove (const char* path);

#endif
