/* x86_64 setjmp/longjmp. We save the callee-saved registers + RSP/RIP. */
#ifndef _SETJMP_H_SHIM
#define _SETJMP_H_SHIM

typedef unsigned long jmp_buf[8];   /* rbx, rbp, r12-r15, rsp, rip */

int  setjmp  (jmp_buf env);
void longjmp (jmp_buf env, int val) __attribute__((noreturn));

#endif
