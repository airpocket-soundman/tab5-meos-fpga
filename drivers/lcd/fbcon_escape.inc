/* Small output-side ANSI/DEC terminal. Unknown controls are consumed, not printed.
 * This is a practical subset; device queries and keyboard protocols are separate. */
static uint8_t s_esc_state, s_csi_len, s_utf8_rem;
static char s_csi_buf[96];
static uint32_t s_utf8_cp, s_utf8_min;
static int s_margin_top, s_margin_bottom = -1;
static bool s_origin, s_autowrap = true, s_reverse, s_bold, s_insert;
static bool s_g0_graphics, s_g1_graphics, s_use_g1;
static uint8_t s_charset_target;
static uint16_t s_base_fg = COL_FG, s_base_bg = COL_BG;
static struct {
    int row, col;
    uint16_t fg, bg;
    bool origin, wrap, bold, reverse;
} s_saved = {.fg = COL_FG, .bg = COL_BG, .wrap = true};
static struct {
    uint16_t cp[ROWS][COLS], fg[ROWS][COLS], bg[ROWS][COLS];
    int row, col;
} *s_primary;

static int term_bottom(void) {
    return s_margin_bottom < 0 || s_margin_bottom >= s_rows ? s_rows - 1 : s_margin_bottom;
}
static int term_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint16_t term_palette(int n);
static void term_attrs(void) {
    uint16_t fg = s_base_fg;
    if (s_bold)
        for (int i = 0; i < 8; i++)
            if (fg == term_palette(i)) {
                fg = term_palette(i + 8);
                break;
            }
    s_attr_fg = s_reverse ? s_base_bg : fg;
    s_attr_bg = s_reverse ? fg : s_base_bg;
}
static void term_save(void) {
    s_saved.row = s_row;
    s_saved.col = s_col;
    s_saved.fg = s_base_fg;
    s_saved.bg = s_base_bg;
    s_saved.origin = s_origin;
    s_saved.wrap = s_autowrap;
    s_saved.bold = s_bold;
    s_saved.reverse = s_reverse;
}
static void term_restore(void) {
    s_row = term_clamp(s_saved.row, 0, s_rows - 1);
    s_col = term_clamp(s_saved.col, 0, COLS - 1);
    s_base_fg = s_saved.fg;
    s_base_bg = s_saved.bg;
    s_origin = s_saved.origin;
    s_autowrap = s_saved.wrap;
    s_bold = s_saved.bold;
    s_reverse = s_saved.reverse;
    term_attrs();
}
static void term_copy_row(int dst, int src) {
    for (int c = 0; c < COLS; c++) {
        s_scr[dst][c] = s_scr[src][c];
        s_fg[dst][c] = s_fg[src][c];
        s_bg[dst][c] = s_bg[src][c];
    }
    s_dirty[dst] = true;
}
static void term_scroll(int top, int bottom, int n, bool down) {
    n = term_clamp(n, 1, bottom - top + 1);
    if (!down && top == 0 && bottom == s_rows - 1 && !s_alternate)
        for (int r = 0; r < n; r++)
            history_save(s_scr[r]);
    if (down) {
        for (int r = bottom; r >= top + n; r--)
            term_copy_row(r, r - n);
        for (int r = top; r < top + n; r++)
            clear_row(r);
    } else {
        for (int r = top; r <= bottom - n; r++)
            term_copy_row(r, r + n);
        for (int r = bottom - n + 1; r <= bottom; r++)
            clear_row(r);
    }
}
static void term_index(bool reverse) {
    int bottom = term_bottom();
    if (reverse) {
        if (s_row == s_margin_top)
            term_scroll(s_margin_top, bottom, 1, true);
        else if (s_row > 0)
            s_row--;
    } else {
        if (s_row == bottom)
            term_scroll(s_margin_top, bottom, 1, false);
        else if (s_row < s_rows - 1)
            s_row++;
    }
}
static void term_blank(int row, int start, int end) {
    start = term_clamp(start, 0, COLS);
    end = term_clamp(end, 0, COLS);
    for (int c = start; c < end; c++) {
        s_scr[row][c] = ' ';
        s_fg[row][c] = s_attr_fg;
        s_bg[row][c] = s_attr_bg;
    }
    s_dirty[row] = true;
}
static void term_alternate(bool enable, bool save) {
    if (enable == s_alternate)
        return;
    if (enable) {
        if (!s_primary)
            s_primary = kmalloc_psram(sizeof *s_primary);
        if (!s_primary)
            return;
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                s_primary->cp[r][c] = s_scr[r][c];
                s_primary->fg[r][c] = s_fg[r][c];
                s_primary->bg[r][c] = s_bg[r][c];
            }
        s_primary->row = s_row;
        s_primary->col = s_col;
        if (save)
            term_save();
        s_alternate = true;
        s_view_offset = 0;
        for (int r = 0; r < s_rows; r++)
            clear_row(r);
        s_row = s_col = 0;
    } else {
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                s_scr[r][c] = s_primary->cp[r][c];
                s_fg[r][c] = s_primary->fg[r][c];
                s_bg[r][c] = s_primary->bg[r][c];
            }
        s_alternate = false;
        s_row = term_clamp(s_primary->row, 0, s_rows - 1);
        s_col = term_clamp(s_primary->col, 0, COLS - 1);
        if (save)
            term_restore();
    }
    dirty_all();
}
static uint16_t term_palette(int n) {
    static const uint16_t p[16] = {
        RGB565(0, 0, 0),       RGB565(205, 49, 49),   RGB565(13, 188, 121), RGB565(229, 229, 16),
        RGB565(36, 114, 200),  RGB565(188, 63, 188),  RGB565(17, 168, 205), RGB565(229, 229, 229),
        RGB565(102, 102, 102), RGB565(241, 76, 76),   RGB565(35, 209, 139), RGB565(245, 245, 67),
        RGB565(59, 142, 234),  RGB565(214, 112, 214), RGB565(41, 184, 219), RGB565(255, 255, 255)};
    n = term_clamp(n, 0, 255);
    if (n < 16)
        return p[n];
    if (n >= 232) {
        int v = 8 + (n - 232) * 10;
        return RGB565(v, v, v);
    }
    n -= 16;
    int r = n / 36, g = (n / 6) % 6, b = n % 6;
    return RGB565(r ? r * 40 + 55 : 0, g ? g * 40 + 55 : 0, b ? b * 40 + 55 : 0);
}
static void term_sgr(const int *p, int count) {
    for (int i = 0; i < count; i++) {
        int v = p[i];
        if (v == 0) {
            s_base_fg = COL_FG;
            s_base_bg = COL_BG;
            s_bold = s_reverse = false;
        } else if (v == 1)
            s_bold = true;
        else if (v == 22)
            s_bold = false;
        else if (v == 7)
            s_reverse = true;
        else if (v == 27)
            s_reverse = false;
        else if (v >= 30 && v <= 37)
            s_base_fg = term_palette(v - 30);
        else if (v >= 40 && v <= 47)
            s_base_bg = term_palette(v - 40);
        else if (v >= 90 && v <= 97)
            s_base_fg = term_palette(v - 90 + 8);
        else if (v >= 100 && v <= 107)
            s_base_bg = term_palette(v - 100 + 8);
        else if (v == 39)
            s_base_fg = COL_FG;
        else if (v == 49)
            s_base_bg = COL_BG;
        else if ((v == 38 || v == 48) && i + 2 < count && p[i + 1] == 5) {
            uint16_t color = term_palette(p[i + 2]);
            if (v == 38)
                s_base_fg = color;
            else
                s_base_bg = color;
            i += 2;
        } else if ((v == 38 || v == 48) && i + 4 < count && p[i + 1] == 2) {
            uint16_t color = RGB565(term_clamp(p[i + 2], 0, 255), term_clamp(p[i + 3], 0, 255),
                                    term_clamp(p[i + 4], 0, 255));
            if (v == 38)
                s_base_fg = color;
            else
                s_base_bg = color;
            i += 4;
        }
    }
    term_attrs();
}
static void term_reset(void) {
    if (s_alternate)
        term_alternate(false, false);
    s_margin_top = 0;
    s_margin_bottom = -1;
    s_origin = false;
    s_autowrap = true;
    s_insert = false;
    s_bold = s_reverse = s_g0_graphics = s_g1_graphics = s_use_g1 = false;
    s_cursor_visible = true;
    s_base_fg = COL_FG;
    s_base_bg = COL_BG;
    term_attrs();
    s_row = s_col = 0;
    s_utf8_rem = 0;
    s_esc_state = 0;
    for (int r = 0; r < s_rows; r++)
        clear_row(r);
    term_save();
}
static void term_csi(char final) {
    int p[16] = {0}, count = 1;
    const char *s = s_csi_buf;
    bool priv = *s == '?';
    bool ignored = false;
    if (*s == '?' || *s == '>' || *s == '=') {
        if (*s != '?')
            ignored = true;
        s++;
    }
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9')
            p[count - 1] = term_clamp(p[count - 1] * 10 + (*s - '0'), 0, 9999);
        else if (*s == ';') {
            if (count == 16) {
                ignored = true;
                break;
            }
            count++;
        } else {
            ignored = true;
            break;
        }
    }
    if (ignored)
        return;
    int n = p[0] ? p[0] : 1, bottom = term_bottom();
    if (priv) {
        if (final != 'h' && final != 'l')
            return;
        bool on = final == 'h';
        for (int i = 0; i < count; i++) {
            if (p[i] == 6) {
                s_origin = on;
                s_row = on ? s_margin_top : 0;
                s_col = 0;
            } else if (p[i] == 7)
                s_autowrap = on;
            else if (p[i] == 25) {
                s_cursor_visible = on;
                dirty_all();
            } else if (p[i] == 47 || p[i] == 1047 || p[i] == 1049)
                term_alternate(on, p[i] == 1049);
            else if (p[i] == 1048) {
                if (on)
                    term_save();
                else
                    term_restore();
            }
        }
        return;
    }
    /* A position/edit command cancels pending right-margin wrap. SGR does not. */
    if (final != 'm')
        s_col = term_clamp(s_col, 0, COLS - 1);
    int lo = s_origin ? s_margin_top : 0, hi = s_origin ? bottom : s_rows - 1;
    switch (final) {
    case 'm':
        term_sgr(p, count);
        break;
    case 'A':
        s_row = term_clamp(s_row - n, lo, hi);
        break;
    case 'B':
    case 'e':
        s_row = term_clamp(s_row + n, lo, hi);
        break;
    case 'C':
    case 'a':
        s_col = term_clamp(s_col + n, 0, COLS - 1);
        break;
    case 'D':
        s_col = term_clamp(s_col - n, 0, COLS - 1);
        break;
    case 'E':
        s_row = term_clamp(s_row + n, lo, hi);
        s_col = 0;
        break;
    case 'F':
        s_row = term_clamp(s_row - n, lo, hi);
        s_col = 0;
        break;
    case 'G':
    case '`':
        s_col = term_clamp(n - 1, 0, COLS - 1);
        break;
    case 'd':
        s_row = term_clamp(n - 1 + lo, lo, hi);
        break;
    case 'H':
    case 'f':
        s_row = term_clamp(n - 1 + lo, lo, hi);
        s_col = term_clamp((count > 1 && p[1] ? p[1] : 1) - 1, 0, COLS - 1);
        break;
    case 'J':
        if (p[0] == 0) {
            term_blank(s_row, s_col, COLS);
            for (int r = s_row + 1; r < s_rows; r++)
                clear_row(r);
        } else if (p[0] == 1) {
            for (int r = 0; r < s_row; r++)
                clear_row(r);
            term_blank(s_row, 0, s_col + 1);
        } else if (p[0] == 2) {
            for (int r = 0; r < s_rows; r++)
                clear_row(r);
        } else if (p[0] == 3) {
            s_history_count = s_history_head = s_view_offset = 0;
            dirty_all();
        }
        break;
    case 'K':
        if (p[0] <= 2)
            term_blank(s_row, p[0] == 0 ? s_col : 0, p[0] == 1 ? s_col + 1 : COLS);
        break;
    case 'X':
        term_blank(s_row, s_col, s_col + term_clamp(n, 1, COLS));
        break;
    case 'P':
    case '@':
        n = term_clamp(n, 1, COLS - s_col);
        if (final == 'P') {
            for (int c = s_col; c < COLS - n; c++) {
                s_scr[s_row][c] = s_scr[s_row][c + n];
                s_fg[s_row][c] = s_fg[s_row][c + n];
                s_bg[s_row][c] = s_bg[s_row][c + n];
            }
            term_blank(s_row, COLS - n, COLS);
        } else {
            for (int c = COLS - 1; c >= s_col + n; c--) {
                s_scr[s_row][c] = s_scr[s_row][c - n];
                s_fg[s_row][c] = s_fg[s_row][c - n];
                s_bg[s_row][c] = s_bg[s_row][c - n];
            }
            term_blank(s_row, s_col, s_col + n);
        }
        break;
    case 'L':
    case 'M':
        if (s_row >= s_margin_top && s_row <= bottom)
            term_scroll(s_row, bottom, n, final == 'L');
        break;
    case 'S':
    case 'T':
        term_scroll(s_margin_top, bottom, n, final == 'T');
        break;
    case 'r': {
        int top = n - 1, bot = count > 1 && p[1] ? p[1] - 1 : s_rows - 1;
        if (top >= 0 && top < bot && bot < s_rows) {
            s_margin_top = top;
            s_margin_bottom = bot;
            s_row = s_origin ? top : 0;
            s_col = 0;
        }
        break;
    }
    case 's':
        term_save();
        break;
    case 'u':
        term_restore();
        break;
    case 'h':
    case 'l':
        if (p[0] == 4)
            s_insert = final == 'h';
        break;
    default:
        break;
    }
}
static uint32_t term_graphic(uint32_t cp) {
    static const uint16_t map[31] = {0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
                                     0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
                                     0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
                                     0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7};
    return cp >= 0x60 && cp <= 0x7e && (s_use_g1 ? s_g1_graphics : s_g0_graphics) ? map[cp - 0x60]
                                                                                  : cp;
}
static void put_codepoint(uint32_t cp) {
    if (s_col >= COLS) {
        s_col = s_autowrap ? 0 : COLS - 1;
        if (s_autowrap)
            term_index(false);
    }
    if (s_insert) {
        for (int c = COLS - 1; c > s_col; c--) {
            s_scr[s_row][c] = s_scr[s_row][c - 1];
            s_fg[s_row][c] = s_fg[s_row][c - 1];
            s_bg[s_row][c] = s_bg[s_row][c - 1];
        }
    }
    if (cp > 0xffffu || (cp >= 0xd800u && cp <= 0xdfffu))
        cp = 0xfffdu;
    s_scr[s_row][s_col] = term_graphic(cp);
    s_fg[s_row][s_col] = s_attr_fg;
    s_bg[s_row][s_col] = s_attr_bg;
    s_col++;
    s_dirty[s_row] = true;
}
static void feed(char ch) {
    unsigned char u = (unsigned char)ch;
    if (u == 0x18 || u == 0x1a) {
        s_esc_state = 0;
        s_utf8_rem = 0;
        return;
    }
    if (s_esc_state == 3 || s_esc_state == 5) {
        if (u == 7 || (s_esc_state == 5 && ch == '\\'))
            s_esc_state = 0;
        else
            s_esc_state = u == 27 ? 5 : 3;
        return;
    }
    if (u == 27) {
        s_esc_state = 1;
        s_utf8_rem = 0;
        return;
    }
    if (s_esc_state == 4) {
        if (s_charset_target == 0)
            s_g0_graphics = ch == '0';
        else if (s_charset_target == 1)
            s_g1_graphics = ch == '0';
        s_esc_state = 0;
        return;
    }
    if (s_esc_state == 1) {
        s_esc_state = 0;
        if (ch == '[') {
            s_esc_state = 2;
            s_csi_len = 0;
        } else if (ch == ']' || ch == 'P' || ch == '^' || ch == '_' || ch == 'X')
            s_esc_state = 3;
        else if (ch == '(' || ch == ')') {
            s_charset_target = ch == ')';
            s_esc_state = 4;
        } else if (ch == '*' || ch == '+' || ch == '%' || ch == '#') {
            s_charset_target = 2;
            s_esc_state = 4;
        } else if (ch == '7')
            term_save();
        else if (ch == '8')
            term_restore();
        else if (ch == 'M')
            term_index(true);
        else if (ch == 'D' || ch == 'E') {
            if (ch == 'E')
                s_col = 0;
            term_index(false);
        } else if (ch == 'c')
            term_reset();
        return;
    }
    if (s_esc_state == 2) {
        if (u >= 0x40 && u <= 0x7e) {
            s_csi_buf[s_csi_len] = 0;
            term_csi(ch);
            s_esc_state = 0;
            return;
        }
        if (u < 0x20)
            return;
        if (s_csi_len + 1 < sizeof s_csi_buf)
            s_csi_buf[s_csi_len++] = ch;
        else {
            s_esc_state = 6;
        }
        return;
    }
    if (s_esc_state == 6) {
        if (u >= 0x40 && u <= 0x7e)
            s_esc_state = 0;
        return;
    }
    if (u >= 0x80) {
        if ((u & 0xc0) == 0x80) {
            if (s_utf8_rem) {
                s_utf8_cp = (s_utf8_cp << 6) | (u & 0x3f);
                if (--s_utf8_rem == 0)
                    put_codepoint(s_utf8_cp >= s_utf8_min ? s_utf8_cp : 0xfffd);
            }
        } else if ((u & 0xe0) == 0xc0) {
            s_utf8_cp = u & 31;
            s_utf8_rem = 1;
            s_utf8_min = 0x80;
        } else if ((u & 0xf0) == 0xe0) {
            s_utf8_cp = u & 15;
            s_utf8_rem = 2;
            s_utf8_min = 0x800;
        } else if ((u & 0xf8) == 0xf0) {
            s_utf8_cp = u & 7;
            s_utf8_rem = 3;
            s_utf8_min = 0x10000;
        } else {
            s_utf8_rem = 0;
            put_codepoint(0xfffd);
        }
        return;
    }
    s_utf8_rem = 0;
    if (ch == '\r')
        s_col = 0;
    else if (ch == '\n') {
        s_col = 0;
        term_index(false);
    } /* Native shell prints LF without CR. */
    else if (ch == '\v' || ch == '\f')
        term_index(false);
    else if (ch == '\b') {
        if (s_col > 0)
            s_col--;
    } else if (ch == '\t')
        s_col = term_clamp((s_col + 8) & ~7, 0, COLS - 1);
    else if (u == 14)
        s_use_g1 = true;
    else if (u == 15)
        s_use_g1 = false;
    else if (u >= 0x20 && u != 0x7f)
        put_codepoint(u);
}
