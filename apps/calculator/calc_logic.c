#include "calc.h"

void calc_init(calc_t *c) { *c = (calc_t){ 0 }; }

static void fail(calc_t *c) { c->error = true; c->typing = false; c->op = 0; }

static void apply(calc_t *c, int64_t b)
{
    switch (c->op) {
    case '+': c->acc += b; break;
    case '-': c->acc -= b; break;
    case '*': c->acc *= b; break;
    case '/': if (b == 0) { fail(c); return; } c->acc /= b; break;
    default:  c->acc = b; break;
    }
    if (c->acc > CALC_LIMIT || c->acc < -CALC_LIMIT) fail(c);
}

void calc_press(calc_t *c, char key)
{
    if (key == 'C') { calc_init(c); return; }
    if (c->error) return;

    if (key >= '0' && key <= '9') {
        if (c->after_equals && !c->typing) { c->acc = 0; c->op = 0; }       /* a digit after '=' starts a new sum */
        if (!c->typing) { c->entry = 0; c->digits = 0; c->typing = true; }
        c->after_equals = false;
        if (c->digits < CALC_MAX_DIGITS) {
            c->entry = c->entry * 10 + (key - '0');
            if (c->entry) c->digits++;
        }
        return;
    }

    if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (c->typing) { apply(c, c->entry); c->typing = false; }
        if (c->error) return;
        c->op = key;                                                         /* two operators in a row: the last wins */
        c->after_equals = false;
        return;
    }

    if (key == '=') {
        if (c->op) apply(c, c->typing ? c->entry : c->acc);                  /* "5 + =" gives 10 */
        else if (c->typing) c->acc = c->entry;
        if (c->error) return;
        c->op = 0;
        c->typing = false;
        c->after_equals = true;
    }
}

void calc_display(const calc_t *c, char *out, size_t cap)
{
    if (cap == 0) return;
    if (c->error) { const char *e = "Error"; size_t i = 0; for (; e[i] && i + 1 < cap; i++) out[i] = e[i]; out[i] = 0; return; }
    int64_t v = c->typing ? c->entry : c->acc;
    char tmp[24]; int n = 0;
    bool neg = v < 0;
    uint64_t u = (uint64_t)(neg ? -v : v);
    do { tmp[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    size_t k = 0;
    if (neg && k + 1 < cap) out[k++] = '-';
    while (n && k + 1 < cap) out[k++] = tmp[--n];
    out[k] = 0;
}
