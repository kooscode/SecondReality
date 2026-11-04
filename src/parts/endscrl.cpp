// ENDSCRL.EXE - the closing text scroller (WILDFIRE).
// Port of ENDSCRL/MAIN.C and ASMYT.ASM, updated to the shipped (newer) executable:
// 25 pixel font with 1550 columns, "[NN" lines are NN scanline gaps and a line
// starting with '%' ends the part.
#include "../core/dos.h"
#include "../core/parts.h"

namespace {

const int FONAY = 25, FONAX = 1550;
const uint8_t *font;            // font[FONAY][FONAX]
const char fonaorder0[] = "ABCDEFGHIJKLMNOPQRSTUVWXabcdefghijklmnopqrstuvwxyz0123456789!?,.:"
                          "\x8f\x8f()+-*='Z\x84\x94Y/&";
int fonap[256], fonaw[256];

uint8_t text[64000];
uint32_t tptr;
int tstart, chars;
const int mtau[8] = {128, 64, 32, 16, 8, 4, 2, 1};
uint8_t textline[100];
uint8_t scanbuf[4][80];
int yscrl, line;

void setrgbpalette(int p, int r, int g, int b)
{
    outp(0x3c8, p);
    outp(0x3c9, r);
    outp(0x3c9, g);
    outp(0x3c9, b);
}

void setstart(int start)
{
    outpw(0x3d4, 0x0d | ((start & 0xff) << 8));
    outpw(0x3d4, 0x0c | (start & 0xff00));
}

uint8_t tch() { return tptr < sizeof text ? text[tptr] : 0; }

// returns false when the part exits
bool do_scroll()
{
    int a, b, x;
    if (line == 0) {
        for (a = 0, tstart = 0, chars = 0; tch() != '\n' && a < 99; a++, chars++) {
            textline[a] = tch();
            tstart += fonaw[tch()] + 2;
            tptr++;
        }
        textline[a] = tch();
        tptr++;
        tstart = (639 - tstart) / 2;
        if (textline[0] == '[') chars = 0;
    }
    memset(scanbuf, 0, sizeof scanbuf);

    for (a = 0, x = tstart; a < chars; a++, x += 2)
        for (b = 0; b < fonaw[textline[a]]; b++, x++) {
            int m = mtau[x & 7];
            uint8_t f = font[(uint16_t)(line * FONAX + fonap[textline[a]] + b)];
            int bx = (int16_t)x / 8;
            if (bx < 0 || bx >= 80) continue;
            if (f & 1) scanbuf[0][bx] ^= m;
            if (f & 2) scanbuf[1][bx] ^= m;
            if (f & 4) scanbuf[2][bx] ^= m;
            if (f & 8) scanbuf[3][bx] ^= m;
        }
    for (int p = 0; p < 4; p++) {
        outpw(0x3c4, 0x0002 | (0x100 << p));
        for (int i = 0; i < 80; i++) {
            vga::wr((uint16_t)(80 * yscrl + i), scanbuf[p][i]);
            vga::wr((uint16_t)(80 * (yscrl + 401) + i), scanbuf[p][i]);
        }
    }
    yscrl = (yscrl + 1) % 401;
    if (textline[0] != '[') line = (line + 1) % FONAY;
    else {
        int n = (textline[1] - '0') * 10 + textline[2] - '0';
        line = n ? (line + 1) % n : 0;
    }
    setstart(yscrl * 80 + 80 * 1);
    return textline[0] != '%';
}

void init()
{
    int x, y, b;
    uint32_t n;
    const uint8_t *t = data_file("ENDSCROL.TXT", &n);
    memset(text, 0, sizeof text);
    if (t) memcpy(text, t, n < 60000 ? n : 60000);

    memset(fonap, 0, sizeof fonap);
    memset(fonaw, 0, sizeof fonaw);
    const uint8_t *fo = (const uint8_t *)fonaorder0;
    for (x = 0; x < FONAX && *fo;) {
        while (x < FONAX) {
            for (y = 0; y < FONAY; y++) if (font[y * FONAX + x]) break;
            if (y != FONAY) break;
            x++;
        }
        b = x;
        while (x < FONAX) {
            for (y = 0; y < FONAY; y++) if (font[y * FONAX + x]) break;
            if (y == FONAY) break;
            x++;
        }
        fonap[*fo] = b;
        fonaw[*fo] = x - b;
        fo++;
    }
    fonap[32] = FONAX - 20;
    fonaw[32] = 16;
}

} // namespace

void part_endscrl()
{
    font = data_file("ENDSCRL_FONT.BIN");
    if (!font) return;
    tptr = 0;
    tstart = chars = 0;
    yscrl = line = 0;
    memset(textline, 0, sizeof textline);

    dis_partstart();
    dis_waitb();
    vga::setmode(0x0e);         // 640x200 16 colours
    outpw(0x3d4, 0x0009);       // ...shown as 400 lines
    setrgbpalette(1, 20, 20, 20);
    setrgbpalette(2, 40, 40, 40);
    for (int c = 3; c < 16; c++) setrgbpalette(c, 60, 60, 60);

    init();

    while (!dis_exit()) {
        if (dis_waitb() == 1) dis_waitb();
        if (!do_scroll()) break;
    }
}
