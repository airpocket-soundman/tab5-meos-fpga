/*
 * A small full-screen editor.  Design choices, since an OS course should say why:
 *  - One flat byte buffer with the cursor as a byte offset.  Inserting or deleting moves the tail with
 *    memmove; for files of a few KB that is faster than the screen redraw it triggers, and far simpler
 *    than a gap buffer or a line table.
 *  - Lines are not stored.  Every operation that needs one (cursor up, redraw) finds it by scanning for
 *    '\n' from a known offset.
 *  - The screen is redrawn completely after each key.  The framebuffer makes that cheap and means there
 *    is no "dirty region" bookkeeping to get wrong.
 */
#include "edit.h"
#include "../tui/dqwin.h"
#include <m5os/vfs.h>
#include <m5os/errno.h>
#include <m5os/kprintf.h>
#include <string.h>

#define HEADER_ROWS  3
#define FOOTER_ROWS  3
#define TAB_SPACES   2
#define STATUS_LEN   80

#define CTRL(c)      ((c) - 'a' + 1)
#define K_SAVE       CTRL('s')
#define K_QUIT       CTRL('q')

static char s_buf[EDIT_MAX + 1];
static int  s_len, s_cur, s_top, s_hscroll, s_want;
static bool s_modified, s_saved_any;
static char s_status[STATUS_LEN];
static char s_path[VFS_PATH_MAX];

/* ---- lines ----------------------------------------------------------------------- */

static int line_start(int pos) { while (pos > 0 && s_buf[pos - 1] != '\n') pos--; return pos; }
static int line_end(int pos)   { while (pos < s_len && s_buf[pos] != '\n') pos++; return pos; }

static int vis_rows(void)  { return dq_rows() - HEADER_ROWS - FOOTER_ROWS; }
static int vis_cols(void)  { return dq_cols() - 4; }

static int line_number(int pos)            /* 1-based */
{
    int n = 1;
    for (int i = 0; i < pos; i++) if (s_buf[i] == '\n') n++;
    return n;
}

static void scroll_to_cursor(void)
{
    int ls = line_start(s_cur);
    if (ls < s_top) s_top = ls;
    int below = 0;                                          /* lines between top and the cursor's line */
    for (int i = s_top; i < ls; i++) if (s_buf[i] == '\n') below++;
    while (below >= vis_rows()) { s_top = line_end(s_top) + 1; below--; }
    int col = s_cur - ls;
    if (col < s_hscroll) s_hscroll = col;
    if (col >= s_hscroll + vis_cols()) s_hscroll = col - vis_cols() + 1;
}

/* ---- drawing --------------------------------------------------------------------- */

static void draw(void)
{
    dq_window(0, 0, dq_cols(), dq_rows());
    char head[160];
    ksnprintf(head, sizeof head, "EDIT  %s%s", s_path, s_modified ? "  [modified]" : "");
    dq_text(2, 1, head);

    int vw = vis_cols(), vr = vis_rows();
    int cur_ls = line_start(s_cur);
    int pos = s_top;
    char line[200];
    for (int r = 0; r < vr; r++) {
        int n = 0;
        if (pos <= s_len) {
            int le = line_end(pos);
            for (int i = pos + s_hscroll; i < le && n < vw && n < (int)sizeof line - 1; i++) {
                unsigned char c = (unsigned char)s_buf[i];
                line[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
            }
            if (pos == cur_ls) {                                   /* the cursor: one reversed cell */
                int cc = s_cur - cur_ls - s_hscroll;
                while (n <= cc && n < (int)sizeof line - 1) line[n++] = ' ';
                line[n] = 0;
                char under[2] = { line[cc], 0 };
                while (n < vw && n < (int)sizeof line - 1) line[n++] = ' ';
                line[n] = 0;
                dq_text(2, HEADER_ROWS + r, line);
                dq_text_rev(2 + cc, HEADER_ROWS + r, under);
                pos = le + 1;
                continue;
            }
            pos = le + 1;
        }
        while (n < vw && n < (int)sizeof line - 1) line[n++] = ' ';
        line[n] = 0;
        dq_text(2, HEADER_ROWS + r, line);
    }

    int fy = dq_rows() - FOOTER_ROWS;
    dq_text(2, fy, "^S:save  ^Q/ESC:quit  arrows/PgUp/PgDn/Home/End:move  Del/BS:delete  Tab:indent");
    char st[160];
    ksnprintf(st, sizeof st, "Ln %d Col %d   %d/%d bytes   %s", line_number(s_cur), s_cur - cur_ls + 1, s_len, EDIT_MAX, s_status);
    dq_text(2, fy + 1, st);
}

/* ---- editing --------------------------------------------------------------------- */

static void say(const char *m) { ksnprintf(s_status, sizeof s_status, "%s", m); }

static void insert(char c)
{
    if (s_len >= EDIT_MAX) { say("buffer full"); return; }
    memmove(&s_buf[s_cur + 1], &s_buf[s_cur], (size_t)(s_len - s_cur));
    s_buf[s_cur++] = c;
    s_len++;
    s_modified = true;
}

static void erase_at(int pos)                    /* delete the byte at pos */
{
    if (pos < 0 || pos >= s_len) return;
    memmove(&s_buf[pos], &s_buf[pos + 1], (size_t)(s_len - pos - 1));
    s_len--;
    s_modified = true;
}

static void set_cursor(int pos) { s_cur = pos; s_want = pos - line_start(pos); }

static void move_up(void)
{
    int ls = line_start(s_cur);
    if (ls == 0) return;
    int pend = ls - 1, pls = line_start(pend), l = pend - pls;
    s_cur = pls + (s_want < l ? s_want : l);
}

static void move_down(void)
{
    int le = line_end(s_cur);
    if (le >= s_len) return;
    int nls = le + 1, nle = line_end(nls), l = nle - nls;
    s_cur = nls + (s_want < l ? s_want : l);
}

/* ---- file ------------------------------------------------------------------------ */

static int load(void)
{
    vfs_info_t st;
    s_len = 0;
    if (vfs_stat(s_path, &st) != ERR_OK) return 0;                 /* a new file */
    if (st.type != FS_TYPE_FILE) return ERR_INVAL;
    if (st.size > EDIT_MAX) return ERR_NOSPC;
    int fd = vfs_open(s_path, VFS_O_READ);
    if (fd < 0) return fd;
    int n;
    while (s_len < EDIT_MAX && (n = vfs_read(fd, &s_buf[s_len], (size_t)(EDIT_MAX - s_len))) > 0) s_len += n;
    vfs_close(fd);
    for (int i = 0; i < s_len; i++) {                              /* the 8x8 font is ASCII: refuse binary data */
        unsigned char c = (unsigned char)s_buf[i];
        if ((c < 0x20 && c != '\n' && c != '\t' && c != '\r') || c >= 0x7F) return ERR_INVAL;
    }
    return 0;
}

static bool save(void)
{
    int fd = vfs_open(s_path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd < 0) { ksnprintf(s_status, sizeof s_status, "save failed: cannot open (error %d)", fd); return false; }
    int w = s_len ? vfs_write(fd, s_buf, (size_t)s_len) : 0;
    vfs_close(fd);
    if (w != s_len) { ksnprintf(s_status, sizeof s_status, "save failed: wrote %d of %d bytes (error %d)", w, s_len, w < 0 ? w : ERR_NOSPC); return false; }
    s_modified = false;
    s_saved_any = true;
    say("saved");
    return true;
}

bool edit_file(const char *path)
{
    ksnprintf(s_path, sizeof s_path, "%s", path);
    s_status[0] = 0; s_cur = s_top = s_hscroll = s_want = 0; s_modified = s_saved_any = false;
    int rc = load();
    if (rc == ERR_NOSPC) { dq_message("File is larger than the editor buffer", "(32 KB)."); return false; }
    if (rc == ERR_INVAL) { dq_message("Cannot edit: not a plain ASCII text file.", NULL); return false; }
    if (rc < 0)          { dq_message("Cannot open the file.", NULL); return false; }

    for (;;) {
        scroll_to_cursor();
        draw();
        int k = dq_getkey();
        s_status[0] = 0;
        switch (k) {
        case K_SAVE: save(); break;
        case K_QUIT: case DQK_ESC:
            if (!s_modified || dq_confirm("Discard unsaved changes?")) return s_saved_any;
            break;
        case DQK_UP:    move_up(); break;
        case DQK_DOWN:  move_down(); break;
        case DQK_LEFT:  if (s_cur > 0) set_cursor(s_cur - 1); break;
        case DQK_RIGHT: if (s_cur < s_len) set_cursor(s_cur + 1); break;
        case DQK_HOME:  set_cursor(line_start(s_cur)); break;
        case DQK_END:   set_cursor(line_end(s_cur)); break;
        case DQK_PGUP:  for (int i = 0; i < vis_rows() - 1; i++) move_up(); break;
        case DQK_PGDN:  for (int i = 0; i < vis_rows() - 1; i++) move_down(); break;
        case DQK_ENTER: insert('\n'); set_cursor(s_cur); break;
        case DQK_BACKSPACE: if (s_cur > 0) { erase_at(s_cur - 1); set_cursor(s_cur - 1); } break;
        case DQK_DEL:   erase_at(s_cur); set_cursor(s_cur); break;
        case '\t':      for (int i = 0; i < TAB_SPACES; i++) insert(' '); set_cursor(s_cur); break;
        default:
            if (k >= 0x20 && k < 0x7F) { insert((char)k); set_cursor(s_cur); }
            break;
        }
    }
}
