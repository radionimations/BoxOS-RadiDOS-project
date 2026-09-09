; Common header for BoxOS apps.
; -----------------------------------------------------------------
; All apps are flat 64-bit binaries linked at 0x200000 and called
; by the kernel as `long entry(const char* args)` (System V ABI).
; They reach back into the kernel via `int 0x80`:
;       rax = syscall number
;       rdi/rsi/rdx = args
;
; Available syscalls:
;       0  EXIT       (just RET works too; this is for symmetry)
;       1  PUTC      rdi=char
;       2  PUTS      rdi=zero-terminated string
;       3  GETC      -> rax (blocks)
;       4  CLS
;       5  SET_COLOR rdi=fg, rsi=bg     (0..15)
;       6  PRINT_INT rdi=signed int
; -----------------------------------------------------------------

%define SYS_EXIT       0
%define SYS_PUTC       1
%define SYS_PUTS       2
%define SYS_GETC       3
%define SYS_CLS        4
%define SYS_SET_COLOR  5
%define SYS_PRINT_INT  6

%macro PUTS 1
    mov     rax, SYS_PUTS
    lea     rdi, [rel %1]
    int     0x80
%endmacro

%macro PUTC 1
    mov     rax, SYS_PUTC
    mov     rdi, %1
    int     0x80
%endmacro

%macro SET_COLOR 2
    mov     rax, SYS_SET_COLOR
    mov     rdi, %1
    mov     rsi, %2
    int     0x80
%endmacro
