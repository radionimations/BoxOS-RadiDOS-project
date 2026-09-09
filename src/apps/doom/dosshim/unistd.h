#ifndef _UNISTD_H_SHIM
#define _UNISTD_H_SHIM
#include <io.h>
static inline int unlink(const char* p) { (void)p; return -1; }
static inline int isatty(int fd)        { (void)fd; return 0; }
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* access() mode flags */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif
