#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SSH_TERM_COLS 80
#define SSH_TERM_ROWS 41
#define SSH_HIST_LINES 256
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))
#define ksnprintf snprintf
#define kmalloc_psram(n) calloc(1, n)
#define kfree free
typedef void ssh_session_t;
/* ACTUAL_SSH_PARSER */
static void send_text(ssh_tab_t *t, const char *s) {
    while (*s)
        tab_feed_byte(t, (uint8_t)*s++);
}
int main(void) {
    ssh_tab_t *t = calloc(1, sizeof *t);
    t->hist = calloc(SSH_HIST_LINES, sizeof *t->hist);
    tab_reset_term(t);
    send_text(t, "\033[4;6H\033[2J");
    assert(t->cy == 3 && t->cx == 5);
    send_text(t, "\033[31;44mR\033[38;5;196mG\033[48;2;10;20;30mB");
    assert(t->screen[3].cells[5].fg == s_ansi_palette[1]);
    assert(t->screen[3].cells[5].bg == s_ansi_palette[4]);
    assert(t->screen[3].cells[6].fg == RGB565(255, 0, 0));
    assert(t->screen[3].cells[7].bg == RGB565(10, 20, 30));
    send_text(t, "\0337\033[1;1H\033[0m\0338");
    assert(t->cy == 3 && t->cx == 8 && t->cur_bg == RGB565(10, 20, 30));
    send_text(t,"\033[38;2;1;2;3;48;2;4;5;6mZ");
    assert(t->cur_fg==RGB565(1,2,3)&&t->cur_bg==RGB565(4,5,6));
    tab_reset_term(t);
    for (int r = 0; r < SSH_TERM_ROWS; r++)
        t->screen[r].cells[0].ch = 'A' + r;
    send_text(t, "\033[3;6r\033[6;1H\033D");
    assert(t->screen[0].cells[0].ch == 'A' && t->screen[2].cells[0].ch == 'D' &&
           t->screen[5].cells[0].ch == ' ' && t->screen[6].cells[0].ch == 'G');
    assert(t->hist_count == 0);
    send_text(t, "\033[3;1H\033M");
    assert(t->screen[2].cells[0].ch == ' ' && t->screen[3].cells[0].ch == 'D');
    send_text(t, "\033[?6h\033[1;2H\033[999A");
    assert(t->cy == 2 && t->cx == 1);
    send_text(t, "\033[999B");
    assert(t->cy == 5);
    send_text(t, "\033[6n");
    assert(t->reply_len == 6 && memcmp(t->reply, "\033[4;2R", 6) == 0);
    tab_reset_term(t);
    send_text(t, "MAIN\033[?1049hALT");
    assert(t->alternate && t->screen[0].cells[0].ch == 'A');
    send_text(t, "\033[41;1H\n\n");
    assert(t->hist_count == 0);
    send_text(t, "\033[?1049l");
    assert(!t->alternate && t->screen[0].cells[0].ch == 'M' && t->cx == 4);
    send_text(t, "\033[?1;25h");
    assert(t->app_cursor && t->cursor_visible);
    send_text(t, "\033[?25l");
    assert(!t->cursor_visible);
    tab_reset_term(t);
    send_text(t, "\033(0lqk\033(B");
    assert(t->screen[0].cells[0].ch == 0x250c && t->screen[0].cells[1].ch == 0x2500);
    send_text(t, "\033Pignored\033\\OK");
    assert(t->screen[0].cells[3].ch == 'O');
    tab_reset_term(t);
    send_text(t, "\033[?7l");
    for (int i = 0; i < 83; i++)
        tab_feed_byte(t, 'X');
    assert(t->cy == 0 && t->cx == 80);
    send_text(t, "\033[2J");
    assert(t->cx == 79);
    send_text(t, "\033[9999999999999999999;1H");
    assert(t->cy == 40);
    tab_reset_term(t);
    uint32_t rng = 0x1739ac;
    for (int i = 0; i < 100000; i++) {
        rng = rng * 1664525u + 1013904223u;
        tab_feed_byte(t, rng >> 24);
        assert(t->cy >= 0 && t->cy < SSH_TERM_ROWS && t->cx >= 0 && t->cx <= SSH_TERM_COLS);
    }
    free(t->primary);
    free(t->hist);
    free(t);
    puts("PASS: SSH ANSI colors, margins, save/restore, alternate, DSR, DEC modes, graphics, "
         "parser bounds");
    return 0;
}
