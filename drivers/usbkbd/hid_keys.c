#include "hid_keys.h"

/* HID usage 0x04..0x38 -> ASCII, unshifted and shifted (US layout). */
static const char UNSHIFTED[] = "abcdefghijklmnopqrstuvwxyz1234567890\r\x1b\x7f\t -=[]\\#;'`,./";
static const char SHIFTED[]   = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\r\x1b\x7f\t _+{}|~:\"~<>?";
#define USAGE_LAST   0x38
#define USAGE_LETTER_LAST 0x1D        /* 'z' */

void hid_decode(uint8_t usage, uint8_t mods, bool caps, char out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    bool shift = mods & (HID_MOD_LSHIFT | HID_MOD_RSHIFT);
    bool ctrl  = mods & (HID_MOD_LCTRL | HID_MOD_RCTRL);
    if (usage >= HID_USAGE_FIRST && usage <= USAGE_LAST) {
        int i = usage - HID_USAGE_FIRST;
        bool letter = usage <= USAGE_LETTER_LAST;
        bool upper = letter ? (shift != caps) : shift;                 /* Caps Lock only flips letters */
        char c = upper ? SHIFTED[i] : UNSHIFTED[i];
        if (ctrl && letter) c = (char)(UNSHIFTED[i] - 'a' + 1);        /* Ctrl-C = 0x03, ... */
        out[0] = c;
        return;
    }
    switch (usage) {
    case 0x4C: out[0] = 0x7F; break;                                       /* Delete */
    case 0x4F: out[0] = 0x1B; out[1] = '['; out[2] = 'C'; break;           /* right */
    case 0x50: out[0] = 0x1B; out[1] = '['; out[2] = 'D'; break;           /* left  */
    case 0x51: out[0] = 0x1B; out[1] = '['; out[2] = 'B'; break;           /* down  */
    case 0x52: out[0] = 0x1B; out[1] = '['; out[2] = 'A'; break;           /* up    */
    default: break;
    }
}
