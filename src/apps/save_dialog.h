/* Save As dialog — header-only, included by apps that want a
 * folder + filename picker.  Opens a modal window over whatever the
 * caller had on screen, captures events until the user clicks Save
 * or Cancel, and returns the user's choice.
 *
 *   int save_dialog(const char* default_name,
 *                   const char* default_folder,
 *                   char* out_folder, int folder_cap,
 *                   char* out_name,   int name_cap);
 *
 * Returns 1 if the user picked Save, 0 if cancelled.  out_folder is
 * one of "", "APPS", "GAMES", "SYS", "DOCS" (root = empty string).
 *
 * After the dialog returns, the caller's window is whatever it
 * looked like before — the dialog's window is closed cleanly.  The
 * caller usually wants to repaint its own window, so calling this
 * before the actual save logic is intended. */

#ifndef BOXOS_SAVE_DIALOG_H
#define BOXOS_SAVE_DIALOG_H

#include "boxos_app.h"

#define SD_W   360
#define SD_H   180

#define SD_X   ((640 - SD_W) / 2)
#define SD_Y   ((480 - SD_H) / 2)

#define SD_NAME_MAX 13

static const char* sd_folders[] = { "/", "APPS", "GAMES", "SYS", "DOCS" };
#define SD_N_FOLDERS ((int)(sizeof(sd_folders) / sizeof(sd_folders[0])))

static int sd_strlen(const char* s) {
    int n = 0; while (s && s[n]) n++; return n;
}

static void sd_paint(int win, int folder_idx, const char* name, int has_focus) {
    int cw, ch;
    gui_size(win, &cw, &ch);
    /* body */
    gui_fill_rect(win, 0, 0, cw, ch, 7);

    gui_text(win, 16, 12, "Folder:", 0, 7);

    /* Folder buttons in a row. */
    int bx = 16;
    int by = 28;
    for (int i = 0; i < SD_N_FOLDERS; i++) {
        int n = sd_strlen(sd_folders[i]);
        int bw = n * 8 + 12;
        int sel = (i == folder_idx);
        gui_fill_rect(win, bx, by, bw, 18, sel ? 9 : 15);
        gui_text(win, bx + 6, by + 5, sd_folders[i], sel ? 15 : 0, sel ? 9 : 15);
        /* simple 1-px border */
        gui_fill_rect(win, bx, by, bw, 1, 0);
        gui_fill_rect(win, bx, by + 17, bw, 1, 0);
        gui_fill_rect(win, bx, by, 1, 18, 0);
        gui_fill_rect(win, bx + bw - 1, by, 1, 18, 0);
        bx += bw + 6;
    }

    gui_text(win, 16, 64, "File name:", 0, 7);
    /* input box */
    int ix = 16, iy = 80, iw = cw - 32, ih = 22;
    gui_fill_rect(win, ix, iy, iw, ih, 15);
    gui_fill_rect(win, ix, iy, iw, 1, 0);
    gui_fill_rect(win, ix, iy + ih - 1, iw, 1, 0);
    gui_fill_rect(win, ix, iy, 1, ih, 0);
    gui_fill_rect(win, ix + iw - 1, iy, 1, ih, 0);
    gui_text(win, ix + 6, iy + 7, name, 0, 15);
    if (has_focus) {
        /* a thin black caret at the end of the text */
        int n = sd_strlen(name);
        int cx = ix + 6 + n * 8;
        gui_fill_rect(win, cx, iy + 5, 1, ih - 10, 0);
    }

    /* OK / Cancel */
    int oy = ch - 32;
    gui_fill_rect(win, cw - 80, oy, 64, 22, 2);
    gui_text(win, cw - 80 + 20, oy + 7, "Save", 15, 2);
    gui_fill_rect(win, cw - 160, oy, 64, 22, 12);
    gui_text(win, cw - 160 + 14, oy + 7, "Cancel", 15, 12);
}

#define SD_HIT_NONE   0
#define SD_HIT_FOLDER 1
#define SD_HIT_INPUT  2
#define SD_HIT_OK     3
#define SD_HIT_CANCEL 4

static int sd_hit(int rx, int ry, int* out_idx) {
    int by = 28;
    if (ry >= by && ry < by + 18) {
        int bx = 16;
        for (int i = 0; i < SD_N_FOLDERS; i++) {
            int n = sd_strlen(sd_folders[i]);
            int bw = n * 8 + 12;
            if (rx >= bx && rx < bx + bw) { *out_idx = i; return SD_HIT_FOLDER; }
            bx += bw + 6;
        }
    }
    if (ry >= 80 && ry < 80 + 22) return SD_HIT_INPUT;
    int oy = SD_H - 32;
    if (ry >= oy && ry < oy + 22) {
        if (rx >= SD_W - 80  && rx < SD_W - 80  + 64) return SD_HIT_OK;
        if (rx >= SD_W - 160 && rx < SD_W - 160 + 64) return SD_HIT_CANCEL;
    }
    return SD_HIT_NONE;
}

static int save_dialog(const char* default_name,
                       const char* default_folder,
                       char* out_folder, int folder_cap,
                       char* out_name,   int name_cap) {
    int win = gui_open_window("Save As", SD_X, SD_Y, SD_W, SD_H);
    if (win < 0) return 0;

    char name[SD_NAME_MAX];
    int  n_len = 0;
    if (default_name) {
        while (default_name[n_len] && n_len < SD_NAME_MAX - 1) {
            name[n_len] = default_name[n_len];
            n_len++;
        }
    }
    name[n_len] = 0;

    int folder_idx = 4;   /* DOCS */
    if (default_folder) {
        for (int i = 0; i < SD_N_FOLDERS; i++) {
            int j = 0;
            int eq = 1;
            while (default_folder[j] || sd_folders[i][j]) {
                if (default_folder[j] != sd_folders[i][j]) { eq = 0; break; }
                j++;
            }
            if (eq) { folder_idx = i; break; }
        }
    }
    int has_focus = 1;

    sd_paint(win, folder_idx, name, has_focus);

    for (;;) {
        struct gui_event ev;
        if (!gui_poll_event(&ev)) { sleep_ms(15); continue; }
        if (ev.type == GUI_EV_KEY && ((ev.arg1 >> 8) & 1)) {
            unsigned a = ev.arg1 & 0xFF;
            if (a == 27) { gui_close_window(win); return 0; }
            if (a == '\r' || a == '\n') {
                if (n_len == 0) continue;
                /* commit */
                int i = 0;
                while (sd_folders[folder_idx][i] && i < folder_cap - 1) {
                    out_folder[i] = sd_folders[folder_idx][i]; i++;
                }
                out_folder[i] = 0;
                /* "/" -> "" so the kernel treats it as root */
                if (out_folder[0] == '/' && out_folder[1] == 0) out_folder[0] = 0;
                i = 0;
                while (name[i] && i < name_cap - 1) { out_name[i] = name[i]; i++; }
                out_name[i] = 0;
                gui_close_window(win);
                return 1;
            }
            if (a == 8) {                       /* backspace */
                if (n_len > 0) { name[--n_len] = 0; sd_paint(win, folder_idx, name, has_focus); }
                continue;
            }
            if (a >= ' ' && a < 127 && n_len < SD_NAME_MAX - 1) {
                /* Fold to upper case to fit FAT12 8.3 conventions. */
                if (a >= 'a' && a <= 'z') a -= 32;
                name[n_len++] = (char)a;
                name[n_len] = 0;
                sd_paint(win, folder_idx, name, has_focus);
            }
        } else if (ev.type == GUI_EV_MOUSE_DOWN && (ev.arg1 & 1)) {
            int rx = ev.x - SD_X - 1;
            int ry = ev.y - SD_Y - 13;
            int idx = 0;
            int h = sd_hit(rx, ry, &idx);
            if (h == SD_HIT_FOLDER) {
                folder_idx = idx;
                sd_paint(win, folder_idx, name, has_focus);
            } else if (h == SD_HIT_OK) {
                if (n_len == 0) continue;
                int i = 0;
                while (sd_folders[folder_idx][i] && i < folder_cap - 1) {
                    out_folder[i] = sd_folders[folder_idx][i]; i++;
                }
                out_folder[i] = 0;
                if (out_folder[0] == '/' && out_folder[1] == 0) out_folder[0] = 0;
                i = 0;
                while (name[i] && i < name_cap - 1) { out_name[i] = name[i]; i++; }
                out_name[i] = 0;
                gui_close_window(win);
                return 1;
            } else if (h == SD_HIT_CANCEL) {
                gui_close_window(win);
                return 0;
            } else if (h == SD_HIT_INPUT) {
                has_focus = 1;
                sd_paint(win, folder_idx, name, has_focus);
            }
        }
    }
}

#endif
