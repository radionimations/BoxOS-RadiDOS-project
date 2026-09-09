/* OS theme registry. A handful of presets that swap the palette
 * indices used for chrome elements. wm_init / paint_executive
 * read theme_active() so a TheMe change at runtime is just a value
 * swap + a repaint. */

#include "boxos.h"

static const struct os_theme g_themes[] = {
    {
        .name = "Classic",
        .menu_bg = 15, .menu_fg = 0,           /* white / black */
        .title_bg = 1,  .title_fg = 15,        /* dark blue / white */
        .desktop_bg = 8,                       /* dark grey */
        .accent = 9,                           /* light blue */
        .status_bg = 8, .status_fg = 15,
    },
    {
        .name = "Dark",
        .menu_bg = 0,  .menu_fg = 15,
        .title_bg = 8, .title_fg = 15,
        .desktop_bg = 0,
        .accent = 4,                           /* dark red */
        .status_bg = 8, .status_fg = 15,
    },
    {
        .name = "Sunset",
        .menu_bg = 6,  .menu_fg = 14,          /* brown / yellow */
        .title_bg = 4, .title_fg = 14,         /* dark red / yellow */
        .desktop_bg = 6,
        .accent = 12,                          /* light red */
        .status_bg = 4, .status_fg = 14,
    },
    {
        .name = "Neon",
        .menu_bg = 5,  .menu_fg = 14,          /* magenta / yellow */
        .title_bg = 2, .title_fg = 14,
        .desktop_bg = 0,
        .accent = 13,                          /* light magenta */
        .status_bg = 2, .status_fg = 15,
    },
};

#define N_THEMES ((int)(sizeof(g_themes) / sizeof(g_themes[0])))

static int g_active = 0;

const struct os_theme* theme_active(void)    { return &g_themes[g_active]; }
int                    theme_count(void)     { return N_THEMES; }
const struct os_theme* theme_at(int i)       {
    if (i < 0 || i >= N_THEMES) return &g_themes[0];
    return &g_themes[i];
}
int  theme_index(void)                       { return g_active; }
void theme_set(int i) {
    if (i < 0 || i >= N_THEMES) return;
    g_active = i;
}
