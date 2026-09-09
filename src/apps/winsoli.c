/* WINSOLI — Klondike Solitaire for BoxOS 2.0.
 *
 * Standard 1-card-draw Klondike. Four foundations (build up by suit,
 * Ace to King) and seven tableau piles (build down by alternating
 * colour). Click a pile to select it, click another pile to move
 * the top card (or the whole face-up sequence, for tableau piles).
 * Click the stock to draw the next card to the waste; click the
 * empty stock to recycle the waste. R = new game, Esc = close. */

#include "boxos_app.h"

#define CW   56
#define CH   76
#define GAP   6
#define OVERLAP 18

#define TOP_Y    8
#define TAB_Y   (TOP_Y + CH + 16)

#define WIN_W  (GAP + 7 * (CW + GAP))
#define WIN_H  460

#define WIN_X  60
#define WIN_Y  18

static int win;
static int cw, ch;

/* ---- card model ----------------------------------------------- */

struct card { uint8_t rank; uint8_t suit; uint8_t up; };

#define MAX_PILE 52

struct pile {
    struct card cards[MAX_PILE];
    int n;
};

/* Pile ids — selected_pile uses these. */
#define P_STOCK         0
#define P_WASTE         1
#define P_FOUND_BASE    2          /* +0..3 */
#define P_TAB_BASE      6          /* +0..6 */
#define N_PILES         13

static struct pile stock, waste;
static struct pile foundations[4];
static struct pile tableau[7];

static int selected_pile = -1;
static int selected_idx  = 0;
static int game_over     = 0;

static uint64_t last_click_ms = 0;
static int      last_click_pile = -1;
static int      last_click_idx  = -1;

/* xorshift PRNG */
static uint32_t rng = 0x13579bdfu;
static uint32_t rng_next(void) {
    uint32_t x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x; return x;
}

static struct pile* pile_ptr(int id) {
    if (id == P_STOCK) return &stock;
    if (id == P_WASTE) return &waste;
    if (id >= P_FOUND_BASE && id < P_FOUND_BASE + 4)
        return &foundations[id - P_FOUND_BASE];
    if (id >= P_TAB_BASE && id < P_TAB_BASE + 7)
        return &tableau[id - P_TAB_BASE];
    return 0;
}

static int is_red(int suit) { return suit == 1 || suit == 2; }

/* ---- setup ---------------------------------------------------- */

static void new_game(void) {
    rng = (uint32_t)(ticks_ms() ^ 0xcafebabeu) | 1;
    struct card deck[52];
    int k = 0;
    for (int s = 0; s < 4; s++)
        for (int r = 1; r <= 13; r++) {
            deck[k].suit = (uint8_t)s;
            deck[k].rank = (uint8_t)r;
            deck[k].up = 0;
            k++;
        }
    /* Fisher–Yates */
    for (int i = 51; i > 0; i--) {
        int j = (int)(rng_next() % (uint32_t)(i + 1));
        struct card t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    for (int i = 0; i < 7; i++) tableau[i].n = 0;
    for (int i = 0; i < 4; i++) foundations[i].n = 0;
    stock.n = 0; waste.n = 0;
    k = 0;
    for (int col = 0; col < 7; col++) {
        for (int row = 0; row <= col; row++) {
            deck[k].up = (row == col) ? 1 : 0;
            tableau[col].cards[tableau[col].n++] = deck[k++];
        }
    }
    while (k < 52) {
        deck[k].up = 0;
        stock.cards[stock.n++] = deck[k++];
    }
    selected_pile = -1; selected_idx = 0;
    game_over = 0;
    last_click_pile = -1;
}

/* ---- move legality -------------------------------------------- */

static int can_drop_on_tab(struct card* mover, struct pile* dest) {
    if (dest->n == 0) return mover->rank == 13;
    struct card* top = &dest->cards[dest->n - 1];
    if (!top->up) return 0;
    if (is_red(mover->suit) == is_red(top->suit)) return 0;
    return mover->rank + 1 == top->rank;
}

static int can_drop_on_found(struct card* mover, struct pile* dest, int found_idx) {
    if (dest->n == 0) return mover->rank == 1 && mover->suit == found_idx;
    struct card* top = &dest->cards[dest->n - 1];
    return mover->suit == top->suit && mover->rank == top->rank + 1;
}

/* Move cards [from->cards[from_idx..n-1]] to dest. Returns 1 on success. */
static int do_move(struct pile* from, int from_idx, struct pile* dest) {
    int count = from->n - from_idx;
    if (count <= 0) return 0;
    for (int i = 0; i < count; i++) {
        if (dest->n >= MAX_PILE) return 0;
        dest->cards[dest->n++] = from->cards[from_idx + i];
    }
    from->n = from_idx;
    /* Auto-flip newly exposed face-down top card. */
    if (from->n > 0 && !from->cards[from->n - 1].up)
        from->cards[from->n - 1].up = 1;
    return 1;
}

static int find_pile_idx_in_array(struct pile* p, struct pile* arr, int n) {
    for (int i = 0; i < n; i++) if (&arr[i] == p) return i;
    return -1;
}

/* Try to move cards [src_idx..top] from `src` to `dest`. */
static int try_move(struct pile* src, int src_idx, struct pile* dest) {
    if (src_idx < 0 || src_idx >= src->n) return 0;
    struct card* mover = &src->cards[src_idx];
    if (!mover->up) return 0;
    int dest_idx = -1;
    if ((dest_idx = find_pile_idx_in_array(dest, foundations, 4)) >= 0) {
        if (src->n - src_idx != 1) return 0;     /* only one card to foundation */
        if (!can_drop_on_found(mover, dest, dest_idx)) return 0;
    } else if (find_pile_idx_in_array(dest, tableau, 7) >= 0) {
        if (!can_drop_on_tab(mover, dest)) return 0;
    } else {
        return 0;
    }
    return do_move(src, src_idx, dest);
}

/* Auto-send top card of `src` to whichever foundation accepts it. */
static int auto_to_foundation(struct pile* src) {
    if (src->n == 0) return 0;
    int idx = src->n - 1;
    struct card* c = &src->cards[idx];
    if (!c->up) return 0;
    return try_move(src, idx, &foundations[c->suit]);
}

static void check_win(void) {
    int total = 0;
    for (int i = 0; i < 4; i++) total += foundations[i].n;
    if (total == 52) game_over = 1;
}

/* ---- layout & hit-testing ------------------------------------- */

static int top_pile_x(int idx) { return GAP + idx * (CW + GAP); }

/* Return pile id under (x,y), and the card index within tableau piles. */
static int hit_pile(int x, int y, int* out_card_idx) {
    *out_card_idx = -1;
    if (y >= TOP_Y && y < TOP_Y + CH) {
        for (int i = 0; i < 4; i++) {
            int px = top_pile_x(i);
            if (x >= px && x < px + CW) {
                *out_card_idx = foundations[i].n - 1;
                return P_FOUND_BASE + i;
            }
        }
        int stock_x = top_pile_x(5);
        int waste_x = top_pile_x(6);
        if (x >= stock_x && x < stock_x + CW) {
            *out_card_idx = stock.n - 1;
            return P_STOCK;
        }
        if (x >= waste_x && x < waste_x + CW) {
            *out_card_idx = waste.n - 1;
            return P_WASTE;
        }
    }
    /* Tableau columns */
    for (int col = 0; col < 7; col++) {
        int px = top_pile_x(col);
        if (x < px || x >= px + CW) continue;
        struct pile* p = &tableau[col];
        if (p->n == 0) {
            if (y >= TAB_Y && y < TAB_Y + CH) {
                *out_card_idx = -1;
                return P_TAB_BASE + col;
            }
            continue;
        }
        /* All cards visible at staggered Y. Last card is full height. */
        for (int i = p->n - 1; i >= 0; i--) {
            int cy = TAB_Y + i * OVERLAP;
            int bottom = (i == p->n - 1) ? cy + CH : cy + OVERLAP;
            if (y >= cy && y < bottom) {
                *out_card_idx = i;
                return P_TAB_BASE + col;
            }
        }
    }
    return -1;
}

/* ---- painting -------------------------------------------------- */

static void rank_str(int rank, char* out) {
    static const char* names[] = {
        "?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    const char* s = (rank >= 1 && rank <= 13) ? names[rank] : "?";
    int i = 0;
    while (s[i] && i < 3) { out[i] = s[i]; i++; }
    out[i] = 0;
}

static void draw_card_back(int x, int y) {
    gui_fill_rect(win, x, y, CW, CH, 1);                    /* blue */
    gui_fill_rect(win, x + 4, y + 4, CW - 8, CH - 8, 9);    /* light blue inset */
    gui_fill_rect(win, x + 8, y + 8, CW - 16, CH - 16, 1);
    gui_fill_rect(win, x, y, CW, 1, 0);
    gui_fill_rect(win, x, y + CH - 1, CW, 1, 0);
    gui_fill_rect(win, x, y, 1, CH, 0);
    gui_fill_rect(win, x + CW - 1, y, 1, CH, 0);
}

static void draw_card_face(int x, int y, struct card* c, int partial) {
    int h = partial ? OVERLAP + 2 : CH;
    gui_fill_rect(win, x, y, CW, h, 15);
    gui_fill_rect(win, x, y, CW, 1, 0);
    gui_fill_rect(win, x, y, 1, h, 0);
    gui_fill_rect(win, x + CW - 1, y, 1, h, 0);
    if (!partial) gui_fill_rect(win, x, y + CH - 1, CW, 1, 0);

    int fg = is_red(c->suit) ? 12 : 0;
    char rb[4]; rank_str(c->rank, rb);
    char sb[2] = { "SHDC"[c->suit], 0 };

    gui_text(win, x + 3, y + 3,  rb, fg, 15);
    gui_text(win, x + 3, y + 13, sb, fg, 15);
    if (!partial) {
        /* Centre suit (a single big-ish glyph). */
        gui_text(win, x + CW / 2 - 4, y + CH / 2 - 4, sb, fg, 15);
        /* Bottom-right corner (rank only; no rotation). */
        int rl = (int)strlen(rb);
        gui_text(win, x + CW - 4 - rl * 8, y + CH - 16, rb, fg, 15);
    }
}

static void draw_card(int x, int y, struct card* c, int partial) {
    if (c->up) draw_card_face(x, y, c, partial);
    else       draw_card_back(x, y);
}

static void draw_empty_slot(int x, int y) {
    gui_fill_rect(win, x, y, CW, CH, 2);
    gui_fill_rect(win, x + 2, y + 2, CW - 4, CH - 4, 10);
    gui_fill_rect(win, x, y, CW, 1, 0);
    gui_fill_rect(win, x, y + CH - 1, CW, 1, 0);
    gui_fill_rect(win, x, y, 1, CH, 0);
    gui_fill_rect(win, x + CW - 1, y, 1, CH, 0);
}

static void draw_selection(int x, int y, int h) {
    gui_fill_rect(win, x - 2, y - 2, CW + 4, 2, 14);
    gui_fill_rect(win, x - 2, y + h, CW + 4, 2, 14);
    gui_fill_rect(win, x - 2, y - 2, 2, h + 4, 14);
    gui_fill_rect(win, x + CW, y - 2, 2, h + 4, 14);
}

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 2);                /* table green */

    /* Foundations */
    for (int i = 0; i < 4; i++) {
        int x = top_pile_x(i);
        if (foundations[i].n == 0) draw_empty_slot(x, TOP_Y);
        else draw_card(x, TOP_Y, &foundations[i].cards[foundations[i].n - 1], 0);
    }
    /* Stock + waste */
    int sx = top_pile_x(5), wx = top_pile_x(6);
    if (stock.n == 0) draw_empty_slot(sx, TOP_Y);
    else draw_card_back(sx, TOP_Y);
    if (waste.n == 0) draw_empty_slot(wx, TOP_Y);
    else draw_card(wx, TOP_Y, &waste.cards[waste.n - 1], 0);

    /* Tableau */
    for (int col = 0; col < 7; col++) {
        int x = top_pile_x(col);
        struct pile* p = &tableau[col];
        if (p->n == 0) { draw_empty_slot(x, TAB_Y); continue; }
        for (int i = 0; i < p->n; i++) {
            int y = TAB_Y + i * OVERLAP;
            int partial = (i != p->n - 1);
            draw_card(x, y, &p->cards[i], partial);
        }
    }

    /* Selection highlight */
    if (selected_pile >= 0) {
        if (selected_pile >= P_TAB_BASE) {
            int col = selected_pile - P_TAB_BASE;
            int x = top_pile_x(col);
            int y = TAB_Y + selected_idx * OVERLAP;
            int h = CH + (tableau[col].n - 1 - selected_idx) * OVERLAP;
            draw_selection(x, y, h);
        } else {
            int x = (selected_pile == P_WASTE) ? top_pile_x(6) :
                    (selected_pile >= P_FOUND_BASE && selected_pile < P_FOUND_BASE + 4)
                      ? top_pile_x(selected_pile - P_FOUND_BASE) : 0;
            draw_selection(x, TOP_Y, CH);
        }
    }

    if (game_over) {
        const char* msg = "YOU WIN!  press R for a new deal";
        gui_fill_rect(win, 0, ch / 2 - 12, cw, 24, 14);
        gui_text(win, (cw - 32 * 8) / 2, ch / 2 - 4, msg, 0, 14);
    }
}

/* ---- interaction ---------------------------------------------- */

static void deal_stock(void) {
    if (stock.n == 0) {
        /* Recycle waste back to stock, face-down, in reverse order. */
        while (waste.n > 0) {
            struct card c = waste.cards[--waste.n];
            c.up = 0;
            stock.cards[stock.n++] = c;
        }
    } else {
        struct card c = stock.cards[--stock.n];
        c.up = 1;
        waste.cards[waste.n++] = c;
    }
}

static int handle_click(int pile_id, int card_idx) {
    /* Stock has its own click semantics — always deals/recycles. */
    if (pile_id == P_STOCK) {
        deal_stock();
        selected_pile = -1;
        return 1;
    }
    /* Nothing selected yet → try to select this pile. */
    if (selected_pile < 0) {
        struct pile* p = pile_ptr(pile_id);
        if (!p) return 0;
        if (p->n == 0) return 0;
        if (pile_id >= P_TAB_BASE) {
            if (card_idx < 0) return 0;
            if (!p->cards[card_idx].up) return 0;
            selected_pile = pile_id;
            selected_idx  = card_idx;
        } else {
            selected_pile = pile_id;
            selected_idx  = p->n - 1;
        }
        return 1;
    }
    /* Already selected — try to move to the clicked pile. */
    struct pile* src = pile_ptr(selected_pile);
    struct pile* dst = pile_ptr(pile_id);
    if (!src || !dst) { selected_pile = -1; return 1; }
    if (src == dst) { selected_pile = -1; return 1; }
    if (try_move(src, selected_idx, dst)) {
        selected_pile = -1;
        check_win();
        return 1;
    }
    /* Illegal move → drop the selection so the click can be a fresh
     * selection on the next press. */
    sound_beep(220, 25);
    selected_pile = -1;
    return 1;
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Solitaire", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINSOLI: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    new_game();
    paint_full();

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(15); continue; }
        if (ev.type == GUI_EV_PAINT) { paint_full(); continue; }

        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            if (a == 'r' || a == 'R') { new_game(); paint_full(); continue; }
        }
        if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - WIN_X - 1;
            int ry = ev.y - WIN_Y - 13;
            int idx = -1;
            int pid = hit_pile(rx, ry, &idx);
            if (pid < 0) { selected_pile = -1; paint_full(); continue; }

            /* Double-click on a face-up top card → auto-foundation. */
            uint64_t now = ticks_ms();
            int is_dbl = (pid == last_click_pile && idx == last_click_idx &&
                          (now - last_click_ms) < 500);
            last_click_pile = pid; last_click_idx = idx; last_click_ms = now;

            if (is_dbl && pid != P_STOCK) {
                struct pile* p = pile_ptr(pid);
                if (p && p->n > 0 && idx == p->n - 1 &&
                    p->cards[idx].up) {
                    auto_to_foundation(p);
                    selected_pile = -1;
                    check_win();
                    paint_full();
                    continue;
                }
            }

            if (handle_click(pid, idx)) paint_full();
        }
    }
}
