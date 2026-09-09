#ifndef _TIME_H_SHIM
#define _TIME_H_SHIM
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000
time_t time(time_t* t);
clock_t clock(void);
#endif
