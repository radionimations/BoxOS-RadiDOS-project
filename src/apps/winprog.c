/* WINPROG — Program Manager for BoxOS 2.0.
 *
 * A Windows 3.1-style single window holding three icon groups
 * ("Main", "Accessories", "Games"). Each icon is a small hand-drawn
 * glyph painted with gui_fill_rect — no BMP assets, just code. Click
 * one to launch its associated .BIN; the new app runs as its own
 * task so Program Manager stays alive in the background. */

#include "boxos_app.h"

#define WIN_W 580
#define WIN_H 420
#define WIN_X ((640 - WIN_W) / 2)
#define WIN_Y 30

static int  win;
static int  cw, ch;
static int  win_x = WIN_X, win_y = WIN_Y;

#define ICON_W           90
#define ICON_H           62
#define ICONS_PER_ROW    6
#define GLYPH_W          40
#define GLYPH_H          36

typedef void (*icon_draw_fn)(int x, int y);

struct prog_entry {
    const char* label;
    const char* binary;      /* NULL = unimplemented placeholder    */
    icon_draw_fn draw;
};

/* ---- drawing primitives --------------------------------------- */

static void rfill(int x, int y, int w, int h, int c) {
    gui_fill_rect(win, x, y, w, h, c);
}

static void routline(int x, int y, int w, int h) {
    rfill(x, y, w, 1, 0);
    rfill(x, y + h - 1, w, 1, 0);
    rfill(x, y, 1, h, 0);
    rfill(x + w - 1, y, 1, h, 0);
}

/* ---- per-app icon glyphs (40x36 each) ------------------------- */

static void icon_folder(int x, int y) {                /* File Mgr */
    rfill(x, y, GLYPH_W, GLYPH_H, 9);
    rfill(x + 4, y + 8, 14, 4, 6);                     /* tab */
    rfill(x + 4, y + 11, 32, 21, 14);                  /* body */
    rfill(x + 4, y + 11, 32, 2, 6);                    /* lip   */
    routline(x + 4, y + 11, 32, 21);
    routline(x + 4, y + 8,  14, 5);
}

static void icon_gear(int x, int y) {                  /* Cntrl Panel */
    rfill(x, y, GLYPH_W, GLYPH_H, 11);                 /* light cyan bg */
    int cx = x + 20, cy = y + 18;
    /* 8 teeth, drawn BEFORE the body so they fan out from under it.
     * Each one is sized so it visibly protrudes 4-5 px past the
     * 16x16 central body. */
    rfill(cx - 2,  cy - 16, 4, 6, 8);                  /* N  */
    rfill(cx - 2,  cy + 10, 4, 6, 8);                  /* S  */
    rfill(cx - 16, cy - 2,  6, 4, 8);                  /* W  */
    rfill(cx + 10, cy - 2,  6, 4, 8);                  /* E  */
    rfill(cx - 13, cy - 12, 6, 6, 8);                  /* NW */
    rfill(cx + 7,  cy - 12, 6, 6, 8);                  /* NE */
    rfill(cx - 13, cy + 6,  6, 6, 8);                  /* SW */
    rfill(cx + 7,  cy + 6,  6, 6, 8);                  /* SE */
    /* Body */
    rfill(cx - 8, cy - 8, 16, 16, 8);
    rfill(cx - 6, cy - 6, 12, 12, 7);                  /* inset */
    /* Black axle hole */
    rfill(cx - 3, cy - 3, 6, 6, 0);
}

static void icon_picture(int x, int y) {               /* Image View */
    rfill(x, y, GLYPH_W, GLYPH_H, 7);
    rfill(x + 4, y + 6, 32, 24, 0);                    /* frame */
    rfill(x + 6, y + 8, 28, 20, 11);                   /* sky */
    /* Mountains */
    for (int i = 0; i < 8; i++) {
        int mh = (i & 1) ? 10 : 14;
        rfill(x + 6 + i * 4, y + 28 - mh, 4, mh, 2);
    }
    rfill(x + 26, y + 11, 4, 4, 14);                   /* sun */
}

static void icon_paper(int x, int y) {                 /* Notepad */
    rfill(x, y, GLYPH_W, GLYPH_H, 7);
    rfill(x + 6, y + 3, 28, 30, 15);                   /* sheet */
    routline(x + 6, y + 3, 28, 30);
    /* Ruled lines */
    for (int i = 0; i < 6; i++) rfill(x + 10, y + 8 + i * 4, 20, 1, 9);
    /* Spiral binding */
    for (int i = 0; i < 4; i++) rfill(x + 8, y + 6 + i * 8, 2, 4, 8);
}

static void icon_calc(int x, int y) {                  /* Calculator */
    rfill(x, y, GLYPH_W, GLYPH_H, 8);
    rfill(x + 4, y + 3, 32, 30, 7);                    /* case */
    routline(x + 4, y + 3, 32, 30);
    rfill(x + 7, y + 6, 26, 6, 0);                     /* display */
    rfill(x + 27, y + 8, 4, 2, 10);                    /* display digits */
    rfill(x + 22, y + 8, 4, 2, 10);
    /* Buttons grid 4x3 */
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++)
        rfill(x + 7 + c * 7, y + 14 + r * 6, 5, 4, 9);
}

static void icon_brush(int x, int y) {                 /* Paint */
    rfill(x, y, GLYPH_W, GLYPH_H, 10);
    /* Handle (diagonal-ish stepped) */
    for (int i = 0; i < 14; i++) rfill(x + 5 + i, y + 26 - i, 3, 3, 6);
    /* Ferrule */
    rfill(x + 18, y + 12, 8, 6, 8);
    routline(x + 18, y + 12, 8, 6);
    /* Bristles */
    rfill(x + 16, y + 6,  3, 8, 12);
    rfill(x + 21, y + 4,  3, 10, 14);
    rfill(x + 26, y + 6,  3, 8, 4);
    rfill(x + 26, y + 8,  3, 4, 1);
}

static void icon_clock(int x, int y) {                 /* Clock */
    rfill(x, y, GLYPH_W, GLYPH_H, 7);
    /* Round-ish face */
    rfill(x + 6, y + 4, 28, 28, 0);
    rfill(x + 8, y + 6, 24, 24, 15);
    /* 12/3/6/9 marks */
    rfill(x + 19, y + 8,  2, 2, 0);
    rfill(x + 28, y + 17, 2, 2, 0);
    rfill(x + 19, y + 26, 2, 2, 0);
    rfill(x + 10, y + 17, 2, 2, 0);
    /* Hands */
    rfill(x + 20, y + 12, 1, 7, 0);                    /* hour */
    rfill(x + 20, y + 18, 8, 1, 4);                    /* minute */
    rfill(x + 19, y + 17, 3, 3, 4);                    /* hub */
}

static void icon_music(int x, int y) {                 /* Music */
    rfill(x, y, GLYPH_W, GLYPH_H, 7);                  /* light grey bg */
    /* Single eighth note, big enough to read. Pill-shaped note head
     * at the bottom-left, vertical stem rising to a tail/flag on the
     * upper right. */
    rfill(x + 8,  y + 22, 10, 10, 0);                  /* head core */
    rfill(x + 6,  y + 24, 14, 6,  0);                  /* head wings */
    rfill(x + 16, y + 4,  3,  22, 0);                  /* stem */
    rfill(x + 19, y + 4,  10, 4,  0);                  /* flag top */
    rfill(x + 21, y + 8,  8,  3,  0);                  /* flag mid */
    rfill(x + 23, y + 11, 6,  3,  0);                  /* flag tip */
}

static void icon_film(int x, int y) {                  /* Animator */
    rfill(x, y, GLYPH_W, GLYPH_H, 6);
    rfill(x + 6, y + 4, 28, 28, 8);                    /* film body */
    /* Sprocket holes */
    for (int i = 0; i < 5; i++) {
        rfill(x + 8,  y + 6 + i * 5, 4, 3, 7);
        rfill(x + 28, y + 6 + i * 5, 4, 3, 7);
    }
    rfill(x + 14, y + 8, 12, 20, 15);                  /* frame */
    rfill(x + 18, y + 12, 4, 4, 12);                   /* picture spot */
    rfill(x + 16, y + 20, 8, 4, 9);
}

static void icon_smiley(int x, int y) {                /* Hello */
    rfill(x, y, GLYPH_W, GLYPH_H, 9);
    /* Round face */
    rfill(x + 8, y + 4, 24, 28, 0);
    rfill(x + 6, y + 8, 28, 20, 0);
    rfill(x + 10, y + 6, 20, 24, 14);
    rfill(x + 8,  y + 10, 24, 16, 14);
    /* Eyes */
    rfill(x + 14, y + 13, 3, 3, 0);
    rfill(x + 23, y + 13, 3, 3, 0);
    /* Smile */
    rfill(x + 14, y + 22, 12, 2, 0);
    rfill(x + 12, y + 20, 2, 2, 0);
    rfill(x + 26, y + 20, 2, 2, 0);
}

static void icon_snake(int x, int y) {                 /* Snake */
    rfill(x, y, GLYPH_W, GLYPH_H, 0);
    /* Three green body segments + head */
    rfill(x + 6,  y + 22, 6, 6, 10);
    rfill(x + 12, y + 22, 6, 6, 10);
    rfill(x + 18, y + 16, 6, 6, 10);
    rfill(x + 24, y + 10, 6, 6, 10);
    rfill(x + 30, y + 10, 4, 6, 10);                   /* head */
    /* Eye */
    rfill(x + 32, y + 12, 2, 2, 12);
    /* Food pellet */
    rfill(x + 8, y + 10, 4, 4, 12);
}

static void icon_skull(int x, int y) {                 /* DOOM */
    rfill(x, y, GLYPH_W, GLYPH_H, 12);
    /* Skull body */
    rfill(x + 8, y + 4, 24, 22, 7);
    rfill(x + 10, y + 26, 20, 4, 7);
    rfill(x + 12, y + 30, 16, 2, 7);
    /* Eye sockets */
    rfill(x + 12, y + 12, 6, 6, 0);
    rfill(x + 22, y + 12, 6, 6, 0);
    /* Nose */
    rfill(x + 19, y + 19, 2, 4, 0);
    /* Teeth */
    rfill(x + 14, y + 26, 1, 4, 0);
    rfill(x + 18, y + 26, 1, 4, 0);
    rfill(x + 22, y + 26, 1, 4, 0);
    rfill(x + 26, y + 26, 1, 4, 0);
}

static void icon_bomb(int x, int y) {                  /* Minesweeper */
    rfill(x, y, GLYPH_W, GLYPH_H, 7);
    /* Bomb body */
    rfill(x + 8,  y + 12, 24, 18, 0);
    rfill(x + 10, y + 10, 20, 22, 0);
    rfill(x + 6,  y + 14, 28, 14, 0);
    /* Highlight */
    rfill(x + 13, y + 14, 4, 3, 8);
    /* Fuse */
    rfill(x + 20, y + 6,  2, 6, 6);
    rfill(x + 22, y + 4,  2, 4, 6);
    /* Spark */
    rfill(x + 23, y + 1,  4, 4, 14);
    rfill(x + 22, y + 2,  6, 2, 12);
}

static void icon_pong(int x, int y) {                  /* Pong */
    rfill(x, y, GLYPH_W, GLYPH_H, 0);
    rfill(x + 5,  y + 8,  3, 20, 15);                  /* left paddle */
    rfill(x + 32, y + 12, 3, 20, 15);                  /* right paddle */
    rfill(x + 18, y + 16, 4, 4, 15);                   /* ball */
    /* Centre dotted line */
    for (int i = 0; i < 4; i++) rfill(x + 20, y + 4 + i * 8, 1, 4, 8);
}

static void icon_disc(int x, int y) {                  /* Reversi */
    rfill(x, y, GLYPH_W, GLYPH_H, 2);
    /* Two discs side-by-side */
    rfill(x + 6,  y + 10, 16, 16, 0);
    rfill(x + 4,  y + 12, 20, 12, 0);
    rfill(x + 18, y + 10, 16, 16, 15);
    rfill(x + 16, y + 12, 20, 12, 15);
}

static void icon_tetro(int x, int y) {                 /* Tetris */
    rfill(x, y, GLYPH_W, GLYPH_H, 8);
    /* L-piece — colour 6 (orange-ish brown) */
    rfill(x + 10, y + 6,  8, 8, 6);
    rfill(x + 10, y + 14, 8, 8, 6);
    rfill(x + 10, y + 22, 8, 8, 6);
    rfill(x + 18, y + 22, 8, 8, 6);
    routline(x + 10, y + 6,  8, 24);
    routline(x + 10, y + 22, 16, 8);
    /* Stray I cell for flavour */
    rfill(x + 26, y + 10, 6, 6, 11);
    routline(x + 26, y + 10, 6, 6);
}

static void icon_card(int x, int y) {                  /* Solitaire */
    rfill(x, y, GLYPH_W, GLYPH_H, 2);
    /* Two overlapping cards */
    rfill(x + 6,  y + 6, 18, 26, 15);
    routline(x + 6, y + 6, 18, 26);
    rfill(x + 16, y + 4, 18, 26, 15);
    routline(x + 16, y + 4, 18, 26);
    /* Heart on front card */
    rfill(x + 20, y + 10, 4, 4, 12);
    rfill(x + 26, y + 10, 4, 4, 12);
    rfill(x + 19, y + 13, 12, 4, 12);
    rfill(x + 21, y + 17, 8, 3, 12);
    rfill(x + 23, y + 20, 4, 2, 12);
}

static void icon_placeholder(int x, int y) {
    rfill(x, y, GLYPH_W, GLYPH_H, 8);
    rfill(x + 4, y + 4, GLYPH_W - 8, GLYPH_H - 8, 7);
    gui_text(win, x + 17, y + 14, "?", 0, 7);
}

/* ---- program list --------------------------------------------- */

static const struct prog_entry main_group[] = {
    { "File Mgr",    "WINFILES.BIN", icon_folder  },
    { "Cntrl Panel", "WINSETT.BIN",  icon_gear    },
    { "Image View",  "WINIMG.BIN",   icon_picture },
    { "Notepad",     "WINNOTE.BIN",  icon_paper   },
    { "Setup",       "SETUP.BIN",    icon_gear    },
};

static const struct prog_entry accessories[] = {
    { "Calculator",  "WINCALC.BIN",  icon_calc   },
    { "Paint",       "WINPAINT.BIN", icon_brush  },
    { "Clock",       "WINCLOCK.BIN", icon_clock  },
    { "Music",       "WINMUS.BIN",   icon_music  },
    { "Animator",    "WINFLIP.BIN",  icon_film   },
    { "Hello",       "WINHELLO.BIN", icon_smiley },
};

static const struct prog_entry games[] = {
    { "Snake",       "WINSNAKE.BIN", icon_snake },
    { "DOOM",        "DOOM.BIN",     icon_skull },
    { "Minesweepr",  "WINMINES.BIN", icon_bomb  },
    { "Pong",        "WINPONG.BIN",  icon_pong  },
    { "Reversi",     "WINRVRS.BIN",  icon_disc  },
    { "Tetris",      "WINTRIS.BIN",  icon_tetro },
    { "Solitaire",   "WINSOLI.BIN",  icon_card  },
};

#define MAIN_N   ((int)(sizeof(main_group)/sizeof(*main_group)))
#define ACC_N    ((int)(sizeof(accessories)/sizeof(*accessories)))
#define GAMES_N  ((int)(sizeof(games)/sizeof(*games)))

static int strlen_l(const char* s) { int n=0; while(s&&s[n])n++; return n; }

static void paint_icon(int x, int y, const struct prog_entry* p) {
    int ix = x + (ICON_W - GLYPH_W) / 2;
    int iy = y + 4;
    if (!p->binary) icon_placeholder(ix, iy);
    else if (p->draw) p->draw(ix, iy);
    else icon_placeholder(ix, iy);
    /* Frame around the whole 40x36 icon. */
    routline(ix, iy, GLYPH_W, GLYPH_H);
    /* Label below. */
    int lw = strlen_l(p->label) * 8;
    int lx = x + (ICON_W - lw) / 2;
    gui_text(win, lx, y + 46, p->label, p->binary ? 0 : 8, 7);
}

static void paint_group_section(const char* title, int yy,
                                const struct prog_entry* entries, int n) {
    /* Section header bar. */
    gui_fill_rect(win, 4, yy, cw - 8, 14, 1);
    int tl = strlen_l(title) * 8;
    gui_text(win, 4 + (cw - 8 - tl) / 2, yy + 3, title, 15, 1);
    /* Icons. */
    int by = yy + 18;
    for (int i = 0; i < n; i++) {
        int col = i % ICONS_PER_ROW;
        int row = i / ICONS_PER_ROW;
        int x = 4 + col * ICON_W;
        int y = by + row * ICON_H;
        paint_icon(x, y, &entries[i]);
    }
}

/* Y positions of each section header, derived from row counts. */
#define MAIN_Y   8
#define MAIN_H   (18 + ((MAIN_N  + ICONS_PER_ROW - 1) / ICONS_PER_ROW) * ICON_H + 4)
#define ACC_Y    (MAIN_Y + MAIN_H)
#define ACC_H    (18 + ((ACC_N   + ICONS_PER_ROW - 1) / ICONS_PER_ROW) * ICON_H + 4)
#define GAMES_Y  (ACC_Y + ACC_H)

static void paint_full(void) {
    gui_fill_rect(win, 0, 0, cw, ch, 7);
    paint_group_section("Main",        MAIN_Y,  main_group,  MAIN_N);
    paint_group_section("Accessories", ACC_Y,   accessories, ACC_N);
    paint_group_section("Games",       GAMES_Y, games,       GAMES_N);
}

/* Returns the entry under (rx, ry) or NULL. */
static const struct prog_entry* hit_icon(int rx, int ry) {
    const struct { int yy; const struct prog_entry* e; int n; } g[3] = {
        { MAIN_Y,  main_group,  MAIN_N },
        { ACC_Y,   accessories, ACC_N  },
        { GAMES_Y, games,       GAMES_N},
    };
    for (int s = 0; s < 3; s++) {
        int by = g[s].yy + 18;
        for (int i = 0; i < g[s].n; i++) {
            int col = i % ICONS_PER_ROW;
            int row = i / ICONS_PER_ROW;
            int x = 4 + col * ICON_W;
            int y = by + row * ICON_H;
            if (rx >= x && rx < x + ICON_W && ry >= y && ry < y + ICON_H)
                return &g[s].e[i];
        }
    }
    return 0;
}

int app_main(const char* args) {
    (void)args;
    win = gui_open_window("Program Manager", win_x, win_y, WIN_W, WIN_H);
    if (win < 0) { bos_puts("WINPROG: cannot open window\n"); return 1; }
    gui_size(win, &cw, &ch);
    paint_full();

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(15); continue; }

        if (ev.type == GUI_EV_PAINT) {
            paint_full();
            continue;
        }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - win_x - 1;
            int ry = ev.y - win_y - 13;
            const struct prog_entry* p = hit_icon(rx, ry);
            if (p && p->binary) {
                (void)bos_launch_app(p->binary, "");
            }
        }
    }
}
