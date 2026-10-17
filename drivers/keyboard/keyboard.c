/*
 * M5Cardputer keyboard: 4 rows x 14 columns of keys, two hardware versions.
 *
 * Original: a 74HC138 decoder (selects on GPIO 8, 9, 11) picks one of 8 lines; seven input pins
 *   (GPIO 13, 15, 3, 4, 5, 6, 7, pull-ups, pressed = low) read the keys on that line.
 *   Line i and input j map to key (x, y) with the chart below.
 * Cardputer-ADV: a TCA8418 keypad controller on I2C (SDA = GPIO8, SCL = GPIO9, address 0x34)
 *   queues press/release events; they are remapped to the same (x, y) space.
 *
 * Everything (pins, chart, key map, remap rule, register numbers) is transcribed from
 * M5Cardputer: IOMatrix.cpp/.h, Keyboard.h, TCA8418.cpp and the Adafruit_TCA8418 sources.
 */
#include "keyboard.h"
#include <m5os/types.h>
#include <m5os/hal_gpio.h>
#include <m5os/hal_i2c.h>
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <m5os/kprintf.h>

#define KB_ROWS   4
#define KB_COLS   14
#define SCAN_MS   10u                  /* effective period is one scheduler tick */
#define REPEAT_DELAY_MS   450u
#define REPEAT_PERIOD_MS  60u
#define SETTLE_US 8u                   /* let the decoder output and the pull-ups settle */

static volatile keyboard_stats_t s_stats = { 0, -1, -1, -1, 0 };

/* ---- key map ---------------------------------------------------------------- */
enum {
    K_NONE = 0,
    K_BKSP = 0x81, K_TAB, K_ENTER, K_ESC, K_DEL, K_UP, K_DOWN, K_LEFT, K_RIGHT,
    K_FN, K_SHIFT, K_CTRL, K_OPT, K_ALT,
    K_F1 = 0x90,                       /* F1..F12 = 0x90..0x9B */
};

typedef struct { uint8_t n, s, f; } keydef_t;       /* normal, shifted ("Aa"), fn layer */
#define L(c) { (c), (c) - 32, K_NONE }               /* a letter: shifted = uppercase */

static const keydef_t KEYS[KB_ROWS][KB_COLS] = {
    { {'`','~',K_ESC}, {'1','!',K_F1}, {'2','@',K_F1+1}, {'3','#',K_F1+2}, {'4','$',K_F1+3}, {'5','%',K_F1+4}, {'6','^',K_F1+5},
      {'7','&',K_F1+6}, {'8','*',K_F1+7}, {'9','(',K_F1+8}, {'0',')',K_F1+9}, {'-','_',K_F1+10}, {'=','+',K_F1+11}, {K_BKSP,K_BKSP,K_DEL} },
    { {K_TAB,K_TAB,0}, L('q'), L('w'), L('e'), L('r'), L('t'), L('y'), L('u'), L('i'), L('o'), L('p'),
      {'[','{',0}, {']','}',0}, {'\\','|',0} },
    { {K_FN,K_FN,K_FN}, {K_SHIFT,K_SHIFT,0}, L('a'), L('s'), L('d'), L('f'), L('g'), L('h'), L('j'), L('k'), L('l'),
      {';',':',K_UP}, {'\'','"',0}, {K_ENTER,K_ENTER,0} },
    { {K_CTRL,K_CTRL,0}, {K_OPT,K_OPT,0}, {K_ALT,K_ALT,0}, L('z'), L('x'), L('c'), L('v'), L('b'), L('n'), L('m'),
      {',','<',K_LEFT}, {'.','>',K_DOWN}, {'/','?',K_RIGHT}, {' ',' ',0} },
};

/* ---- hardware: original matrix ---------------------------------------------- */
static const int SEL_PINS[3] = { 8, 9, 11 };
static const int IN_PINS[7]  = { 13, 15, 3, 4, 5, 6, 7 };
static const struct { uint8_t x1, x2; } X_CHART[7] = { {0,1}, {2,3}, {4,5}, {6,7}, {8,9}, {10,11}, {12,13} };

static void matrix_init(void)
{
    for (int i = 0; i < 3; i++) hal_gpio_config_output(SEL_PINS[i], 0);
    for (int j = 0; j < 7; j++) hal_gpio_config_input_pullup(IN_PINS[j]);
}

static void matrix_scan(bool down[KB_ROWS][KB_COLS])
{
    for (int y = 0; y < KB_ROWS; y++) for (int x = 0; x < KB_COLS; x++) down[y][x] = false;
    for (int i = 0; i < 8; i++) {
        for (int b = 0; b < 3; b++) hal_gpio_write(SEL_PINS[b], (i >> b) & 1);
        hal_delay_us(SETTLE_US);
        for (int j = 0; j < 7; j++) {
            if (hal_gpio_read(IN_PINS[j])) continue;                  /* high = not pressed */
            int x = (i > 3) ? X_CHART[j].x1 : X_CHART[j].x2;
            int y = 3 - ((i > 3) ? i - 4 : i);                         /* "keep the same as the picture" */
            down[y][x] = true;
        }
    }
}

/* ---- hardware: Cardputer-ADV (TCA8418) --------------------------------------- */
#define TCA_ADDR            0x34
#define TCA_REG_CFG         0x01
#define TCA_REG_INT_STAT    0x02
#define TCA_REG_KEY_LCK_EC  0x03
#define TCA_REG_KEY_EVENT_A 0x04
#define TCA_REG_KP_GPIO_1   0x1D
#define TCA_REG_KP_GPIO_2   0x1E
#define TCA_CFG_KE_GPI_IEN  0x03       /* key-event + GPI interrupt enable (the FIFO fills either way) */
#define TCA_ROWS_MASK       0x7F       /* matrix(7, 8): seven rows ... */
#define TCA_COLS_MASK       0xFF       /* ... eight columns */
#define TCA_FLUSH_MAX       64

static bool tca_present(void)
{
    uint8_t v;
    hal_i2c_init();
    return hal_i2c_read_reg(TCA_ADDR, TCA_REG_CFG, &v);
}

static void tca_init(void)
{
    uint8_t v = 0, ev = 0;
    hal_i2c_write_reg(TCA_ADDR, TCA_REG_KP_GPIO_1, TCA_ROWS_MASK);
    hal_i2c_write_reg(TCA_ADDR, TCA_REG_KP_GPIO_2, TCA_COLS_MASK);
    for (int i = 0; i < TCA_FLUSH_MAX; i++) { if (!hal_i2c_read_reg(TCA_ADDR, TCA_REG_KEY_EVENT_A, &ev) || ev == 0) break; }
    hal_i2c_write_reg(TCA_ADDR, TCA_REG_INT_STAT, 3);
    if (hal_i2c_read_reg(TCA_ADDR, TCA_REG_CFG, &v)) hal_i2c_write_reg(TCA_ADDR, TCA_REG_CFG, (uint8_t)(v | TCA_CFG_KE_GPI_IEN));
}

static void tca_poll(bool down[KB_ROWS][KB_COLS])               /* keeps the state between calls */
{
    uint8_t count = 0;
    if (!hal_i2c_read_reg(TCA_ADDR, TCA_REG_KEY_LCK_EC, &count)) { s_stats.i2c_errors++; return; }
    count &= 0x0F;
    while (count--) {
        uint8_t e;
        if (!hal_i2c_read_reg(TCA_ADDR, TCA_REG_KEY_EVENT_A, &e) || e == 0) break;
        bool press = e & 0x80;
        int n = (e & 0x7F) - 1;
        int row = n / 10, col = n % 10;
        int x = row * 2 + (col > 3 ? 1 : 0);                       /* remap "to the same as cardputer" */
        int y = col % 4;
        if (x < KB_COLS && y < KB_ROWS) down[y][x] = press;
    }
}

/* ---- decoding ------------------------------------------------------------------ */
static const char *s_type = "none";

static bool is_modifier(uint8_t k) { return k == K_FN || k == K_SHIFT || k == K_CTRL || k == K_OPT || k == K_ALT; }

static void emit(const keydef_t *k, bool shift, bool fn, bool ctrl)
{
    uint8_t c = fn ? k->f : (shift ? k->s : k->n);
    if (ctrl && k->n >= 'a' && k->n <= 'z') c = (uint8_t)(k->n - 'a' + 1);          /* Ctrl-C = 0x03, ... */
    char buf[4] = { 0 };
    switch (c) {
    case K_NONE: case K_OPT: return;
    case K_ENTER: buf[0] = '\r'; break;
    case K_BKSP: case K_DEL: buf[0] = 0x7F; break;
    case K_TAB:  buf[0] = '\t'; break;
    case K_ESC:  buf[0] = 0x1B; break;
    case K_UP: case K_DOWN: case K_LEFT: case K_RIGHT:
        buf[0] = 0x1B; buf[1] = '['; buf[2] = (char)(c == K_UP ? 'A' : c == K_DOWN ? 'B' : c == K_RIGHT ? 'C' : 'D'); break;
    default:
        if (c >= K_F1 && c < K_F1 + 12) return;                                       /* F-keys: not used yet */
        if (c < 0x80) buf[0] = (char)c;
        break;
    }
    if (buf[0]) { kconsole_inject(buf); s_stats.last_char = (unsigned char)buf[0]; }
}

static void kbd_task(void *arg)
{
    (void)arg;
    bool down[KB_ROWS][KB_COLS] = { { 0 } }, prev[KB_ROWS][KB_COLS] = { { 0 } };
    const keydef_t *held = NULL;
    uint64_t pressed_at = 0, last_repeat = 0;

    for (;;) {
        task_sleep(SCAN_MS);
        if (s_type[0] == 'm') matrix_scan(down); else tca_poll(down);

        bool shift = down[2][1], fn = down[2][0], ctrl = down[3][0];
        uint64_t now = hal_time_now_us();
        for (int y = 0; y < KB_ROWS; y++) {
            for (int x = 0; x < KB_COLS; x++) {
                const keydef_t *k = &KEYS[y][x];
                if (!down[y][x] || is_modifier(k->n)) {
                    if (!down[y][x] && prev[y][x] && held == k) held = NULL;
                    continue;
                }
                if (!prev[y][x]) {                                   /* a new press */
                    s_stats.presses++; s_stats.last_x = x; s_stats.last_y = y;
                    emit(k, shift, fn, ctrl);
                    held = k; pressed_at = now; last_repeat = now;
                } else if (held == k && now - pressed_at > REPEAT_DELAY_MS * 1000u && now - last_repeat > REPEAT_PERIOD_MS * 1000u) {
                    emit(k, shift, fn, ctrl);                          /* auto-repeat while held */
                    last_repeat = now;
                }
            }
        }
        for (int y = 0; y < KB_ROWS; y++) for (int x = 0; x < KB_COLS; x++) prev[y][x] = down[y][x];
    }
}

const char *keyboard_type(void) { return s_type; }

void keyboard_stats(keyboard_stats_t *out) { *out = s_stats; }

void keyboard_start(void)
{
    if (tca_present()) { s_type = "TCA8418"; tca_init(); }
    else               { s_type = "matrix"; matrix_init(); }
    kprintf("keyboard: %s\n", s_type);
    task_create_stack("kbd", kbd_task, NULL, TASK_PRIO_DEFAULT + 2, 3072);
}
