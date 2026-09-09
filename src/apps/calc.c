/* Tiny calculator. Evaluates infix expressions with + - * / % ( ) on
 * 64-bit signed integers, plus bare-hex (0x...) and bare-bin (0b...).
 * Type 'quit' or 'exit' to leave. */

#include "boxos_app.h"

static int  is_digit(char c) { return c >= '0' && c <= '9'; }
static int  is_hex(char c)   { return is_digit(c) || ((c|32) >= 'a' && (c|32) <= 'f'); }

static const char* p;
static int err;

static void skip_ws(void) { while (*p == ' ' || *p == '\t') p++; }

static int64_t parse_expr(void);

static int64_t parse_num(void) {
    int64_t v = 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p == '+')  p++;
    skip_ws();
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (!is_hex(*p)) { err = 1; return 0; }
        while (is_hex(*p)) {
            int d = (*p >= '0' && *p <= '9') ? *p - '0' : (*p|32) - 'a' + 10;
            v = v * 16 + d;
            p++;
        }
        return neg ? -v : v;
    }
    if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        p += 2;
        if (*p != '0' && *p != '1') { err = 1; return 0; }
        while (*p == '0' || *p == '1') v = v * 2 + (*p++ - '0');
        return neg ? -v : v;
    }
    if (!is_digit(*p)) { err = 1; return 0; }
    while (is_digit(*p)) v = v * 10 + (*p++ - '0');
    return neg ? -v : v;
}

static int64_t parse_atom(void) {
    skip_ws();
    if (*p == '(') {
        p++;
        int64_t v = parse_expr();
        skip_ws();
        if (*p != ')') { err = 1; return 0; }
        p++;
        return v;
    }
    return parse_num();
}

static int64_t parse_term(void) {
    int64_t v = parse_atom();
    for (;;) {
        skip_ws();
        char op = *p;
        if (op != '*' && op != '/' && op != '%') return v;
        p++;
        int64_t r = parse_atom();
        if (op == '*')      v = v * r;
        else if (r == 0)    { err = 1; return 0; }
        else if (op == '/') v = v / r;
        else                v = v % r;
    }
}

static int64_t parse_expr(void) {
    int64_t v = parse_term();
    for (;;) {
        skip_ws();
        char op = *p;
        if (op != '+' && op != '-') return v;
        p++;
        int64_t r = parse_term();
        v = (op == '+') ? v + r : v - r;
    }
}

static void print_int_dec(int64_t v) {
    char buf[32];
    int n = 0;
    if (v < 0) { bos_putc('-'); v = -v; }
    if (v == 0) { bos_putc('0'); return; }
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) bos_putc(buf[n]);
}

static void print_int_hex(uint64_t v) {
    char buf[24];
    int n = 0;
    if (v == 0) { bos_puts("0x0"); return; }
    while (v) { int d = v & 0xF; buf[n++] = d < 10 ? '0' + d : 'a' + d - 10; v >>= 4; }
    bos_puts("0x");
    while (n--) bos_putc(buf[n]);
}

static size_t read_line(char* buf, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = bos_getc();
        if (c == '\n' || c == '\r') { bos_putc('\n'); buf[n] = 0; return n; }
        if (c == 8 || c == 127) { if (n > 0) { n--; bos_putc('\b'); bos_putc(' '); bos_putc('\b'); } continue; }
        if (c >= ' ' && c < 127 && n + 1 < cap) { buf[n++] = c; bos_putc(c); }
    }
}

static int eq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

int app_main(const char* args) {
    (void)args;
    bos_puts("calc -- type an expression, or 'quit'.\n");
    bos_puts("supports + - * / % () and 0x.. / 0b.. literals.\n");
    char buf[128];
    for (;;) {
        bos_puts("calc> ");
        size_t n = read_line(buf, sizeof(buf));
        if (!n) continue;
        if (eq(buf, "quit") || eq(buf, "exit") || eq(buf, "q")) break;
        if (eq(buf, "help") || eq(buf, "?")) {
            bos_puts("examples:\n  1 + 2 * 3\n  (10 + 5) / 3\n  0xff & ... no, just arithmetic\n");
            continue;
        }
        p = buf;
        err = 0;
        int64_t v = parse_expr();
        skip_ws();
        if (err || *p) { bos_puts("  syntax error\n"); continue; }
        bos_puts("  = ");
        print_int_dec(v);
        bos_puts("   ");
        print_int_hex((uint64_t)v);
        bos_putc('\n');
    }
    return 0;
}
