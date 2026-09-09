/* DOS-style unbuffered file I/O. We map this to BoxOS's bos_read_file
 * via a tiny per-handle table in libc.c. Writes are no-ops (BoxOS FS
 * is read-only for now). */
#ifndef _IO_H_SHIM
#define _IO_H_SHIM
#include <stddef.h>
#include "boxos_compat.h"

int  open  (const char* path, int flags, ...);
int  close (int fd);
long read  (int fd, void* buf, unsigned long n);
long write (int fd, const void* buf, unsigned long n);
long lseek (int fd, long off, int whence);
int  filelength(int fd);   /* Watcom extension; FastDoom uses this */
int  access(const char* path, int mode);

#endif
