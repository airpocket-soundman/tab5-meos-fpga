/* Output-side DEC modes used by curses applications. No hardware dependencies. */
static int tab_clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static uint16_t tab_effective_fg(const ssh_tab_t *t) {
    if (t->bold)
        for (int i = 0; i < 8; i++)
            if (t->cur_fg == s_ansi_palette[i])
                return s_ansi_palette[i + 8];
    return t->cur_fg;
}
static uint16_t tab_color256(int n) {
    n = tab_clamp(n, 0, 255);
    if (n < 16)
        return s_ansi_palette[n];
    if (n >= 232) {
        int v = 8 + (n - 232) * 10;
        return RGB565(v, v, v);
    }
    n -= 16;
    int r = n / 36, g = (n / 6) % 6, b = n % 6;
    return RGB565(r ? r * 40 + 55 : 0, g ? g * 40 + 55 : 0, b ? b * 40 + 55 : 0);
}
static void tab_save_cursor(ssh_tab_t *t) {
    t->saved_cx = t->cx;
    t->saved_cy = t->cy;
    t->saved_fg = t->cur_fg;
    t->saved_bg = t->cur_bg;
    t->saved_bold = t->bold;
    t->saved_reverse = t->reverse;
    t->saved_origin = t->origin;
    t->saved_wrap = t->autowrap;
}
static void tab_restore_cursor(ssh_tab_t *t) {
    t->cx = tab_clamp(t->saved_cx, 0, SSH_TERM_COLS - 1);
    t->cy = tab_clamp(t->saved_cy, 0, SSH_TERM_ROWS - 1);
    t->cur_fg = t->saved_fg;
    t->cur_bg = t->saved_bg;
    t->bold = t->saved_bold;
    t->reverse = t->saved_reverse;
    t->origin = t->saved_origin;
    t->autowrap = t->saved_wrap;
    t->all_dirty = true;
}
static void tab_scroll_region(ssh_tab_t *t, int top, int bottom, int n, bool down) {
    n = tab_clamp(n, 1, bottom - top + 1);
    if (!down && top == 0 && bottom == SSH_TERM_ROWS - 1 && !t->alternate && t->hist) {
        for (int y = 0; y < n; y++) {
            t->hist[t->hist_head] = t->screen[y];
            t->hist_head = (t->hist_head + 1) % SSH_HIST_LINES;
            if (t->hist_count < SSH_HIST_LINES)
                t->hist_count++;
            if (t->scroll_offset > 0 && t->scroll_offset < t->hist_count)
                t->scroll_offset++;
        }
    }
    uint16_t fg = t->reverse ? t->cur_bg : tab_effective_fg(t);
    uint16_t bg = t->reverse ? tab_effective_fg(t) : t->cur_bg;
    if (down) {
        for (int y = bottom; y >= top + n; y--)
            t->screen[y] = t->screen[y - n];
        for (int y = top; y < top + n; y++)
            term_clear_line(&t->screen[y], fg, bg);
    } else {
        for (int y = top; y <= bottom - n; y++)
            t->screen[y] = t->screen[y + n];
        for (int y = bottom - n + 1; y <= bottom; y++)
            term_clear_line(&t->screen[y], fg, bg);
    }
    for (int y = top; y <= bottom; y++)
        t->dirty_rows[y] = true;
}
static void tab_newline(ssh_tab_t *t) {
    t->dirty_rows[t->cy] = true;
    if (t->cy == t->margin_bottom)
        tab_scroll_region(t, t->margin_top, t->margin_bottom, 1, false);
    else if (t->cy < SSH_TERM_ROWS - 1)
        t->cy++;
    t->dirty_rows[t->cy] = true;
}
static void tab_alternate(ssh_tab_t *t, bool on, bool save) {
    if (on == t->alternate)
        return;
    if (on) {
        if (!t->primary)
            t->primary = kmalloc_psram(sizeof t->screen);
        if (!t->primary)
            return;
        memcpy(t->primary, t->screen, sizeof t->screen);
        t->primary_cx = t->cx;
        t->primary_cy = t->cy;
        if (save)
            tab_save_cursor(t);
        for (int y = 0; y < SSH_TERM_ROWS; y++)
            term_clear_line(&t->screen[y], t->cur_fg, t->cur_bg);
        t->cx = t->cy = 0;
        t->scroll_offset = 0;
    } else {
        memcpy(t->screen, t->primary, sizeof t->screen);
        t->cx = tab_clamp(t->primary_cx, 0, SSH_TERM_COLS - 1);
        t->cy = tab_clamp(t->primary_cy, 0, SSH_TERM_ROWS - 1);
        if (save)
            tab_restore_cursor(t);
    }
    t->alternate = on;
    t->all_dirty = true;
}
static void tab_private_modes(ssh_tab_t *t, bool on) {
    for (int i = 0; i < t->csi_nparams; i++)
        switch (t->csi_params[i]) {
        case 1:
            t->app_cursor = on;
            break;
        case 6:
            t->origin = on;
            t->cy = on ? t->margin_top : 0;
            t->cx = 0;
            break;
        case 7:
            t->autowrap = on;
            break;
        case 25:
            t->cursor_visible = on;
            t->all_dirty = true;
            break;
        case 47:
        case 1047:
        case 1049:
            tab_alternate(t, on, t->csi_params[i] == 1049);
            break;
        case 1048:
            if (on)
                tab_save_cursor(t);
            else
                tab_restore_cursor(t);
            break;
        default:
            break;
        }
}
static void tab_status_reply(ssh_tab_t *t, int query) {
    char reply[32];
    int n = 0;
    if (query == 5) {
        memcpy(reply, "\033[0n", 4);
        n = 4;
    } else if (query == 6)
        n = ksnprintf(reply, sizeof reply, "\033[%d;%dR",
                      t->cy + 1 - (t->origin ? t->margin_top : 0),
                      tab_clamp(t->cx, 0, SSH_TERM_COLS - 1) + 1);
    if (n > 0 && n < (int)sizeof reply && t->reply_len + n <= sizeof t->reply) {
        memcpy(t->reply + t->reply_len, reply, n);
        t->reply_len += n;
    }
}
static uint32_t tab_graphic(ssh_tab_t *t, uint32_t cp) {
    static const uint16_t map[31] = {0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
                                     0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
                                     0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
                                     0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7};
    if (cp >= 0x60 && cp <= 0x7e && (t->use_g1 ? t->g1_graphics : t->g0_graphics))
        return map[cp - 0x60];
    return cp > 0xffff || (cp >= 0xd800 && cp <= 0xdfff) ? 0xfffd : cp;
}
