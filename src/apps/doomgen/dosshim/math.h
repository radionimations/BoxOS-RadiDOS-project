/* doomgeneric uses very little math.h. We don't have an FPU enabled
 * so we stub the few functions it might pull in. */
#ifndef _MATH_H_SHIM
#define _MATH_H_SHIM
static inline double sqrt(double x) { (void)x; return 0; }
static inline double sin(double x)  { (void)x; return 0; }
static inline double cos(double x)  { (void)x; return 0; }
static inline double atan2(double y, double x) { (void)y; (void)x; return 0; }
static inline double pow(double b, double e)   { (void)b; (void)e; return 0; }
#endif
