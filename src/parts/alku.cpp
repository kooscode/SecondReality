// ALKU.EXE - opening texts over the scrolling horizon (WILDF)
// Port of ALKU/MAIN.C, ALKU/COPPER.ASM, ALKU/ASMYT.ASM, ALKU/TWEAK.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include <atomic>

namespace {

const int SCRLF = 9;

// ---- COPPER.ASM
std::atomic<int> frame_count;
const uint8_t *volatile cop_pal;
volatile int do_pal;
volatile int cop_start;
volatile int cop_scrl;
std::atomic<int> cop_dofade;
uint8_t fadepal[768 * 2];
const int16_t *volatile cop_fadepal;

void copper1()   // before retrace: scroll position
{
    outpw(0x3d4, 0x0d | ((cop_start & 255) << 8));
    outpw(0x3d4, 0x0c | (((cop_start >> 8) & 255) << 8));
    outp(0x3c0, 0x33);
    outp(0x3c0, cop_scrl);
}

void copper2()   // in retrace: frame counter, palette
{
    frame_count++;
    if (do_pal) {
        outp(0x3c8, 0);
        const uint8_t *p = cop_pal;
        for (int i = 0; i < 768; i++) outp(0x3c9, p[i]);
        do_pal = 0;
    }
}

void copper3()   // top of screen: 8.8 fixed point palette fade
{
    if (cop_dofade == 0) return;
    cop_dofade--;
    cop_pal = fadepal;
    do_pal = 1;
    const int16_t *s = cop_fadepal;
    for (int i = 0; i < 768; i++) {
        unsigned v = (fadepal[i] << 8) | fadepal[i + 768];
        v += (uint16_t)s[i];
        fadepal[i + 768] = (uint8_t)v;
        fadepal[i] = (uint8_t)(v >> 8);
    }
}

void init_copper()
{
    dis_setcopper(1, copper1);
    dis_setcopper(2, copper2);
    dis_setcopper(0, copper3);
}

void close_copper()
{
    dis_setcopper(0, nullptr);
    dis_setcopper(1, nullptr);
    dis_setcopper(2, nullptr);
}

// ---- TWEAK.ASM
void tw_opengraph()
{
    vga::setmode(0x13);
    outpw(0x3c4, 0x0604);      // chain4 off
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);    // clear vmem
    outpw(0x3d4, 0x0014);      // crtc long off
    outpw(0x3d4, 0xe317);      // crtc byte on
    outpw(0x3d4, 0x0009);      // 400
    outpw(0x3d4, 0x5813);      // 640 wide
    outpw(0x3d4, 0x7018);      // linecompare
    outpw(0x3d4, 0x1f07);      // 8th bit
}

void tw_putpixel(int x, int y, int color)
{
    static const uint8_t planetau[4] = {1, 2, 4, 8};
    outpw(0x3c4, 0x02 | (planetau[x & 3] << 8));
    vga::wr((uint16_t)((x >> 2) + y * 176), (uint8_t)color);
}

int tw_getpixel(int x, int y)
{
    outpw(0x3ce, 0x04 | ((x & 3) << 8));
    return vga::rd((uint16_t)(y * 176 + (x >> 2)));
}

void tw_setpalette(const uint8_t *pal)
{
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, pal[i]);
}

// ---- ASMYT.ASM
// copies one 4 pixel wide column of the 640x150 picture to video memory,
// doubling every row (dest rows are 176 bytes)
void outline(const uint8_t *src, uint32_t dest)
{
    int mrol = 0x08;
    for (int cx = 4; cx >= 1; cx--) {
        outpw(0x3c4, 0x02 | (mrol << 8));
        const uint8_t *si = src + cx - 1;
        vga::wr((uint16_t)(dest - 352), 0);
        vga::wr((uint16_t)(dest - 352 + 176), 0);
        for (int c = 0; c < 150; c++) {
            uint8_t al = si[c * 640];
            vga::wr((uint16_t)(dest + c * 352), al);
            vga::wr((uint16_t)(dest + c * 352 + 176), al);
        }
        mrol >>= 1;
    }
}

uint16_t dtau[30000];

void ascrolltext(int scrl, const uint16_t *text)
{
    static const uint16_t mmask[4] = {0x0102, 0x0202, 0x0402, 0x0802};
    static const uint16_t rmask[4] = {0x0004, 0x0104, 0x0204, 0x0304};
    const uint16_t *si = text;
    for (int cx = 0; cx < 4; cx++) {
        int bx = (cx + scrl) & 3;
        outpw(0x3c4, mmask[bx]);
        outpw(0x3ce, rmask[bx]);
        uint16_t di = (uint16_t)((uint16_t)(cx + scrl) >> 2);
        for (;;) {
            uint16_t b = si[0], c = si[1];
            si += 2;
            if (b == 0xFFFF) break;
            uint16_t o = (uint16_t)(b + di);
            vga::wr(o, vga::rd(o) ^ (uint8_t)c);
        }
    }
}

// ---- MAIN.C
const uint8_t *hzpic;
uint8_t font[32][1500];
uint8_t palette[768];    // pic
uint8_t palette2[768];   // pic & text
uint8_t fade1[768];      // black
uint8_t fade2[768];      // text
int16_t picin[768], textin[768], textout[768];
int fonap[256], fonaw[256];
uint8_t tbuf_mem[186 * 352 + 8];
uint8_t *const tbuf = tbuf_mem + 4;   // tbuf[y][x] = tbuf[y*352+x]; x-2 may reach into the previous row
#define TB(y, x) tbuf[(y) * 352 + (x)]
int a = 0, p = 0, tptr = 0;

int do_scroll(int mode);

void wait(int t)
{
    while (frame_count < t && !dis_exit()) sys::idle();
    frame_count = 0;
}

void fonapois()
{
    static const uint16_t mm[4] = {0x0102, 0x0202, 0x0402, 0x0802};
    static const uint16_t rm[4] = {0x0004, 0x0104, 0x0204, 0x0304};
    for (int pl = 0; pl < 4; pl++) {
        outpw(0x3c4, mm[pl]);
        outpw(0x3ce, rm[pl]);
        for (unsigned o = 160 * 64; o < 160U * (64 + 256); o++) vga::wr(o, vga::rd(o) & 63);
    }
}

void ffonapois()
{
    static const uint16_t mm[4] = {0x0102, 0x0202, 0x0402, 0x0802};
    static const uint16_t rm[4] = {0x0004, 0x0104, 0x0204, 0x0304};
    for (int pl = 0; pl < 4; pl++) {
        outpw(0x3c4, mm[pl]);
        outpw(0x3ce, rm[pl]);
        for (unsigned o = 40 * 64 * 4; o < 40U * (64 + 256 + 10) * 4; o++) vga::wr(o, vga::rd(o) & 0x3f);
        do_scroll(0);
    }
}

void prt(int x, int y, const char *txt)
{
    int x2w, x2, y2, y2w = y + 32, sx, d;
    while (*txt) {
        uint8_t ch = (uint8_t)*txt;
        x2w = fonaw[ch] + x;
        sx = fonap[ch];
        for (x2 = x; x2 < x2w; x2++) {
            for (y2 = y; y2 < y2w; y2++) {
                d = font[y2 - y][sx];
                tw_putpixel(x2, y2, tw_getpixel(x2, y2) | d);
            }
            sx++;
        }
        x = x2 + 2;
        txt++;
    }
}

void prtc(int x, int y, const char *txt)
{
    int w = 0;
    for (const char *t = txt; *t; t++) w += fonaw[(uint8_t)*t] + 2;
    prt(x - w / 2, y, txt);
}

void dofade(const uint8_t *pal1, const uint8_t *pal2)
{
    static uint8_t pal[768];
    for (int a = 0; a < 64 && !dis_exit(); a++) {
        for (int b = 0; b < 768; b++) pal[b] = (uint8_t)((pal1[b] * (64 - a) + pal2[b] * a) >> 6);
        cop_pal = pal; do_pal = 1;
        while (frame_count < 1) sys::idle();
        frame_count = 0;
    }
}

void addtext(int tx, int ty, const char *txt)
{
    int w = 0;
    for (const char *t = txt; *t; t++) w += fonaw[(uint8_t)*t] + 2;
    w /= 2;
    for (const char *t = txt; *t; t++) {
        uint8_t ch = (uint8_t)*t;
        for (int x = 0; x < fonaw[ch]; x++)
            for (int y = 0; y < 32; y++) TB(y + ty, tx + x - w) = font[y][fonap[ch] + x];
        tx += fonaw[ch] + 2;
    }
}

void faddtext(int tx, int ty, const char *txt)
{
    int w = 0;
    for (const char *t = txt; *t; t++) w += fonaw[(uint8_t)*t] + 2;
    w /= 2;
    for (const char *t = txt; *t; t++) {
        uint8_t ch = (uint8_t)*t;
        for (int x = 0; x < fonaw[ch]; x++)
            for (int y = 0; y < 32; y++) TB(y + ty, tx + x - w) = font[y][fonap[ch] + x];
        do_scroll(0);
        tx += fonaw[ch] + 2;
    }
}

void fmaketext(int scrl)
{
    static const uint16_t mtau[4] = {1 * 256 + 2, 2 * 256 + 2, 4 * 256 + 2, 8 * 256 + 2};
    uint16_t *p1 = dtau;
    for (int m = 0; m < 4; m++) {
        for (int x = m; x < 320; x += 4) {
            for (int y = 1; y < 184; y++)
                if (TB(y, x) != TB(y, x - 2)) {
                    *p1++ = (uint16_t)(x / 4 + y * 176 + 100 * 176);
                    *p1++ = TB(y, x) ^ TB(y, x - 2);
                }
            do_scroll(0);
        }
        *p1++ = 0xFFFF;
        *p1++ = 0xFFFF;
    }
    for (int x = 0; x < 320; x++) {
        outpw(0x3c4, mtau[(x + scrl) & 3]);
        outpw(0x3ce, ((x + scrl) & 3) * 256 + 4);
        for (int y = 1; y < 184; y++) {
            uint16_t o = (uint16_t)(y * 176 + 176 * 100 + (x + scrl) / 4);
            vga::wr(o, vga::rd(o) ^ TB(y, x - 1 - 1));
            o = (uint16_t)(o + 88);
            vga::wr(o, vga::rd(o) ^ TB(y, x - 1));
        }
        do_scroll(0);
    }
    while (a <= scrl) do_scroll(0);
}

int do_scroll(int mode)
{
    if (mode == 0 && frame_count < SCRLF) { sys::idle(); return 0; }
    while (frame_count < SCRLF) sys::idle();
    frame_count -= SCRLF;
    if (mode == 1) ascrolltext(a + p * 352, dtau);
    cop_start = a / 4 + p * 88; cop_scrl = (a & 3) * 2;
    if ((a & 3) == 0) {
        outline(hzpic + (a / 4 + 86) * 4 + 784, (a / 4 + 86) + 176 * 50);
        outline(hzpic + (a / 4 + 86) * 4 + 784, (a / 4 + 86) + 176 * 50 + 88);
    }
    a += 1; p ^= 1;
    return 1;
}

void init()
{
    int b, x, y;
    static const char fonaorder0[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXabcdefghijklmnopqrstuvwxyz0123456789!?,.:\x8f\x8f()+-*='\x8f\x99";
    const char *fonaorder = fonaorder0;

    dis_partstart();
    tw_opengraph();
    init_copper();
    tw_setpalette(fade1);
    memcpy(palette, hzpic + 16, 768);

    for (int a = 0; a < 88; a++) {
        outline(hzpic + a * 4 + 784, a + 176 * 50);
        outline(hzpic + a * 4 + 784, a + 176 * 50 + 88);
    }

    for (y = 0; y < 32; y++)
        for (int a = 0; a < 1500; a++) {
            switch (font[y][a] & 3) {
            case 0x1: b = 0x40; break;
            case 0x2: b = 0x80; break;
            case 0x3: b = 0xc0; break;
            default: b = 0;
            }
            font[y][a] = b;
        }

    for (y = 0; y < 768; y += 3) {
        if (y < 64 * 3) {
            palette2[y + 0] = palette[y + 0];
            palette2[y + 1] = palette[y + 1];
            palette2[y + 2] = palette[y + 2];
        } else {
            int k = y < 128 * 3 ? 1 : y < 192 * 3 ? 2 : 3;
            for (int c = 0; c < 3; c++) {
                fade2[y + c] = palette[k * 3 + c];
                palette2[y + c] = (uint8_t)((fade2[y + c] * 63 + palette[y % (64 * 3) + c] * (63 - palette[k * 3 + c])) >> 6);
            }
        }
    }

    for (int a = 192; a < 768; a++) palette[a] = palette[a - 192];

    for (x = 0; x < 1500 && *fonaorder;) {
        while (x < 1500) {
            for (y = 0; y < 32; y++) if (font[y][x]) break;
            if (y != 32) break;
            x++;
        }
        b = x;
        while (x < 1500) {
            for (y = 0; y < 32; y++) if (font[y][x]) break;
            if (y == 32) break;
            x++;
        }
        fonap[(uint8_t)*fonaorder] = b;
        fonaw[(uint8_t)*fonaorder] = x - b;
        fonaorder++;
    }
    fonap[32] = 1500 - 20;
    fonaw[32] = 16;

    for (int a = 0; a < 768; a++) {
        textin[a] = (int16_t)((palette2[a] - palette[a]) * 256 / 64);
        textout[a] = (int16_t)((palette[a] - palette2[a]) * 256 / 64);
        picin[a] = (int16_t)((palette[a] - fade1[a]) * 256 / 128);
    }
}

void run()
{
    int aa, f;

    vga::setmode(3);   // "mov ax,3 / int 10h" (ignored inside the demo)

    init();

    while (dis_sync() < 1 && !dis_exit()) {}

    prtc(160, 120, "A");
    prtc(160, 160, "Future Crew");
    prtc(160, 200, "Production");
    dofade(fade1, fade2); wait(300); dofade(fade2, fade1); fonapois();

    while (dis_sync() < 2 && !dis_exit()) {}

    prtc(160, 160, "First Presented");
    prtc(160, 200, "at Assembly 93");
    dofade(fade1, fade2); wait(300); dofade(fade2, fade1); fonapois();

    while (dis_sync() < 3 && !dis_exit()) {}

    prtc(160, 120, "in");
    prtc(160, 160, "\x8f");
    prtc(160, 179, "\x99");
    dofade(fade1, fade2); wait(300); dofade(fade2, fade1); fonapois();

    while (dis_sync() < 4 && !dis_exit()) {}

    memcpy(fadepal, fade1, 768);
    cop_fadepal = picin;
    cop_dofade = 128;
    for (a = 1, p = 1, f = 0, frame_count = 0; cop_dofade != 0 && !dis_exit();)
        do_scroll(2);

    for (f = 60; a < 320 && !dis_exit();) {
        if (f == 0) {
            cop_fadepal = textin;
            cop_dofade = 64;
            f += 20;
        } else if (f == 50) {
            cop_fadepal = textout;
            cop_dofade = 64;
            f++;
        } else if (f > 50 && cop_dofade == 0) {
            cop_pal = palette; do_pal = 1; f++;
            memset(tbuf, 0, 186 * 320);
            switch (tptr++) {
            case 0:
                addtext(160, 50, "Graphics");
                addtext(160, 90, "Marvel");
                addtext(160, 130, "Pixel");
                ffonapois();
                break;
            case 1:
                faddtext(160, 50, "Music");
                faddtext(160, 90, "Purple Motion");
                faddtext(160, 130, "Skaven");
                ffonapois();
                break;
            case 2:
                faddtext(160, 30, "Code");
                faddtext(160, 70, "Psi");
                faddtext(160, 110, "Trug");
                faddtext(160, 148, "Wildfire");
                ffonapois();
                break;
            case 3:
                faddtext(160, 50, "Additional Design");
                faddtext(160, 90, "Abyss");
                faddtext(160, 130, "Gore");
                ffonapois();
                break;
            case 4:
                ffonapois();
                break;
            default:
                faddtext(160, 80, "BUG BUG BUG");
                faddtext(160, 130, "Timing error");
                ffonapois();
                break;
            }
            while (((a & 1) || dis_sync() < 4 + tptr) && !dis_exit() && a < 319)
                do_scroll(0);
            aa = a;
            if (aa < 320 - 12) fmaketext(aa + 16);
            f = 0;
        } else f++;
        do_scroll(1);
    }
    if (f > 63 / SCRLF) dofade(palette2, palette);
    fonapois();
    close_copper();
}

} // namespace

void part_alku()
{
    frame_count = 0; cop_pal = nullptr; do_pal = 0; cop_start = 0; cop_scrl = 0; cop_dofade = 0;
    memset(fadepal, 0, sizeof fadepal);
    memset(palette, 0, sizeof palette); memset(palette2, 0, sizeof palette2);
    memset(fade1, 0, sizeof fade1); memset(fade2, 0, sizeof fade2);
    memset(fonap, 0, sizeof fonap); memset(fonaw, 0, sizeof fonaw);
    memset(tbuf_mem, 0, sizeof tbuf_mem);
    a = p = tptr = 0;
    hzpic = data_file("ALKU_HZPIC.BIN");
    uint32_t n;
    const uint8_t *fnt = data_file("ALKU_FONT.BIN", &n);
    if (!hzpic || !fnt) return;
    memcpy(font, fnt, n < sizeof font ? n : sizeof font);
    run();
}
