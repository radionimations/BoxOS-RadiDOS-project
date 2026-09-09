#ifndef _SYS_STAT_H_SHIM
#define _SYS_STAT_H_SHIM
struct stat {
    long st_size;
    long st_mtime;
    int  st_mode;
};
static inline int stat (const char* p, struct stat* s) { (void)p; (void)s; return -1; }
static inline int fstat(int fd,         struct stat* s) { (void)fd; (void)s; return -1; }
#define S_ISREG(m) 0
#define S_ISDIR(m) 0
#endif
