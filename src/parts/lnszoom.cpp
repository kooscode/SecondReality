// LNS&ZOOM.EXE - picture fade-in, the bouncing lens and the rotozoomer (PSI).
// Port of LENS/MAIN.C and LENS/ASM.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include <math.h>
#include <utility>

namespace {

const uint8_t *lensex0, *lensex1, *lensex2, *lensex3, *lensex4, *lensexp, *lensexb;
uint8_t backmem[65536 + 8192];        // back (64000) followed by the linked filler
uint8_t *back = backmem;
uint8_t rotpic[65536];
uint8_t *rotpic90;
uint8_t fade[80 * 64 * 3], fade2[32 * 192 * 3];
uint8_t palette[768];
const int16_t *pathdata1, *pathdata2;
int lenswid, lenshig, lensxs, lensys;

inline uint16_t W(const uint8_t *p, int byteoff) { uint16_t v; memcpy(&v, p + byteoff, 2); return v; }

int waitb() { return dis_waitb(); }

void setpalarea(const uint8_t *p, int start, int cnt)
{
    outp(0x3c8, start);
    for (int i = 0; i < cnt * 3; i++) outp(0x3c9, p[i]);
}

// ---- ASM.ASM lens rows
void dorow(const uint8_t *lens, uint16_t u, int y, uint8_t col)
{
    uint16_t si = W(lens, y * 4);
    int cx = W(lens, y * 4 + 2);
    if ((int16_t)cx < 4) return;
    uint16_t di = (uint16_t)(u + W(lens, si));
    uint16_t bp = di;
    si += 2;
    if (bp & 1) {
        uint16_t bx = W(lens, si); si += 2;
        vga::wr(bp, back[(uint16_t)(bx + di)] | col);
        bp++;
        cx--;
    }
    for (int k = 0; k < cx; k++) {
        uint16_t bx = W(lens, si + k * 2);
        vga::wr((uint16_t)(bp + k), back[(uint16_t)(bx + di)] | col);
    }
}

void dorow2(const uint8_t *lens, uint16_t u, int y, uint8_t col)
{
    uint16_t si = W(lens, y * 4);
    int cx = W(lens, y * 4 + 2);
    if (!cx) return;
    uint16_t di = (uint16_t)(u + W(lens, si));
    uint16_t sv = di;
    int bp = si + 2;
    for (; cx; cx--, bp += 4) {
        uint16_t src = W(lens, bp + 2), dst = W(lens, bp);
        vga::wr((uint16_t)(dst + sv), back[(uint16_t)(src + di)] | col);
    }
}

void dorow3(const uint8_t *lens, uint16_t u, int y)
{
    uint16_t si = W(lens, y * 4);
    int cx = W(lens, y * 4 + 2);
    if (!cx) return;
    uint16_t di = (uint16_t)(u + W(lens, si));
    si += 2;
    for (; cx; cx--, si += 2) {
        uint16_t bx = W(lens, si);
        vga::wr((uint16_t)(bx + di), back[(uint16_t)(bx + di)]);
    }
}

void drawlens(int x0, int y0)
{
    int32_t u1 = (x0 - lensxs) + (int32_t)(y0 - lensys) * 320;
    int32_t u2 = (x0 - lensxs) + (int32_t)(y0 + lensys - 1) * 320;
    int ys = lenshig / 2, ye = lenshig - 1;
    for (int y = 0; y < ys; y++) {
        if (u1 >= 0 && u1 <= 64000) {
            dorow(lensex1, (uint16_t)u1, y, 0x40);
            dorow2(lensex2, (uint16_t)u1, y, 0x80);
            dorow2(lensex3, (uint16_t)u1, y, 0xC0);
            dorow3(lensex4, (uint16_t)u1, y);
        }
        u1 += 320;
        if (u2 >= 0 && u2 <= 64000) {
            dorow(lensex1, (uint16_t)u2, ye - y, 0x40);
            dorow2(lensex2, (uint16_t)u2, ye - y, 0x80);
            dorow2(lensex3, (uint16_t)u2, ye - y, 0xC0);
            dorow3(lensex4, (uint16_t)u2, ye - y);
        }
        u2 -= 320;
    }
}

void inittwk()
{
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, 0);
    outpw(0x3d4, 0x0009);
    outpw(0x3d4, 0x0014);
    outpw(0x3d4, 0xe317);
    outpw(0x3c4, 0x0604);
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
    outp(0x3d4, 9);
    outp(0x3d5, (vga::in(0x3d5) & ~31) | 3);   // 4x vertical
}

// ---- rotozoomer: 160x100 samples, two pixels per sample, into the 4x stretched mode
void rotate(int x, int y, int xa, int ya)
{
    int32_t xpos = (int32_t)((uint32_t)(uint16_t)x << 16);
    int32_t ypos = (int32_t)((uint32_t)(uint16_t)y << 16);
    int32_t ebx = (int32_t)(int16_t)ya << 6;
    int32_t eax = (int32_t)(int16_t)xa << 6;
    const uint8_t *tex = rotpic;
    int32_t ecx = eax < 0 ? -eax : eax, edx = ebx < 0 ? -ebx : ebx;
    if (ecx > edx) {
        tex = rotpic90;
        std::swap(eax, ebx);
        eax = -eax;
        int32_t nx = -ypos, ny = xpos;
        xpos = nx; ypos = ny;
    }
    int32_t xadd = eax, yadd = ebx;

    // per pixel steps: u += yadd, v -= xadd (8.16 in byte+word)
    uint16_t cx = (uint16_t)yadd, dx = (uint16_t)xadd;
    uint8_t bl = (uint8_t)(yadd >> 16), bh = (uint8_t)(xadd >> 16);
    bh = (uint8_t)-bh;
    bool borrow = dx != 0;
    dx = (uint16_t)-dx;
    bh = (uint8_t)(bh - borrow);
    uint16_t si = 0, di = 0;
    uint8_t al = 0, ah = 0;
    uint16_t samp[160];
    for (int k = 0; k < 160; k++) {
        uint32_t s = (uint32_t)si + cx;
        si = (uint16_t)s;
        al = (uint8_t)(al + bl + (s >> 16));
        uint32_t d = (uint32_t)di + dx;
        di = (uint16_t)d;
        ah = (uint8_t)(ah + bh + (d >> 16));
        samp[k] = (uint16_t)(al | (ah << 8));
    }

    xadd = (int32_t)((uint32_t)307 * (uint32_t)xadd) >> 8;
    yadd = (int32_t)((uint32_t)307 * (uint32_t)yadd) >> 8;

    uint16_t dst = 0;
    for (int row = 0; row < 100; row++, dst += 80) {
        ypos += yadd;
        uint32_t eb = (uint32_t)ypos >> 8;
        xpos += xadd;
        uint32_t ea = (uint32_t)xpos >> 16;
        uint16_t base = (uint16_t)((eb & 0xff00) | (ea & 0xff));
        outpw(0x3c4, 0x0302);
        for (int g = 0; g < 40; g++) {
            vga::wr((uint16_t)(dst + 2 * g), tex[(uint16_t)(base + samp[4 * g])]);
            vga::wr((uint16_t)(dst + 2 * g + 1), tex[(uint16_t)(base + samp[4 * g + 2])]);
        }
        outpw(0x3c4, 0x0c02);
        for (int g = 0; g < 40; g++) {
            vga::wr((uint16_t)(dst + 2 * g), tex[(uint16_t)(base + samp[4 * g + 1])]);
            vga::wr((uint16_t)(dst + 2 * g + 1), tex[(uint16_t)(base + samp[4 * g + 3])]);
        }
    }
}

// ---- MAIN.C
int firfade1[200], firfade2[200], firfade1a[200], firfade2a[200];

void part1()
{
    int a, b, c, frame = 0;
    for (b = 0; b < 200; b++) {
        a = b;
        firfade1a[b] = (19 + a / 5 + 4) & (~7);
        firfade2a[b] = (-(19 + (199 - a) / 5 + 4)) & (~7);
        firfade1[b] = 170 * 64 + (100 - b) * 50;
        firfade2[b] = 170 * 64 + (100 - b) * 50;
    }
    // "if(dis_musplus>-30)" compares the function's address: always true
    while (!dis_exit() && dis_musplus() < -6) {}
    dis_waitb();
    dis_setmframe(0);
    while (!dis_exit() && frame < 300) {
        if (frame < 80) {
            for (c = 0; c < 6; c++) {
                uint16_t cp = 0;    // same offset in vram and in back
                for (int y = 0; y < 200; y++) {
                    int x = (int16_t)firfade1[y] >> 6;
                    vga::wr((uint16_t)(cp + x), back[(uint16_t)(cp + x)]);
                    x = (int16_t)firfade2[y] >> 6;
                    vga::wr((uint16_t)(cp + x), back[(uint16_t)(cp + x)]);
                    firfade1[y] = (int16_t)(firfade1[y] + firfade1a[y]);
                    firfade2[y] = (int16_t)(firfade2[y] + firfade2a[y]);
                    cp = (uint16_t)(cp + 320);
                }
            }
        }
        a = waitb();
        frame += a;
    }
}

void part2()
{
    int x, y, a, frame = 0, uframe = 0;
    while (!dis_exit() && uframe < 715) {
        if (uframe < 96) {
            a = (uframe - 32) / 2;
            if (a < 0) a = 0;
            setpalarea(fade2 + a * 3 * 192, 64, 192);
        }
        x = pathdata1[frame * 2 + 0];
        y = pathdata1[frame * 2 + 1];
        drawlens(x, y);
        a = waitb();
        uframe += a;
        if (a > 3) a = 3;
        frame += a;
    }
    while (!dis_exit() && uframe < 720) uframe += waitb();
}

void part3()
{
    int x, y, xa, ya, a, frame = 0;
    rotpic90 = back;
    for (x = 0; x < 256; x++)
        for (y = 0; y < 256; y++) rotpic90[x + y * 256] = rotpic[y + (255 - x) * 256];
    waitb();
    setpalarea(fade + 64 * 64 * 3, 0, 64);
    inittwk();
    frame = 0;
    while (!dis_exit() && frame < 2000) {
        if (dis_musplus() > -4) break;
        x = pathdata2[frame * 4 + 0];
        y = pathdata2[frame * 4 + 1];
        xa = pathdata2[frame * 4 + 2];
        ya = pathdata2[frame * 4 + 3];
        rotate(x, y, xa, ya);
        frame += waitb();
        if (frame > 2000 - 128) {
            a = frame - (2000 - 128);
            a /= 2;
            if (a > 63) a = 63;
            setpalarea(fade + a * 64 * 3, 0, 64);
        }
        if (frame < 16) setpalarea(fade + (64 + frame) * 64 * 3, 0, 64);
    }
    for (a = 0; a < 768; a++) palette[a] = 63;
    setpalarea(palette, 0, 256);
}

} // namespace

void part_lnszoom()
{
    uint32_t nf;
    lensex0 = data_file("LENS_EX0.BIN");
    lensex1 = data_file("LENS_EX1.BIN");
    lensex2 = data_file("LENS_EX2.BIN");
    lensex3 = data_file("LENS_EX3.BIN");
    lensex4 = data_file("LENS_EX4.BIN");
    lensexp = data_file("LENS_EXP.BIN");
    lensexb = data_file("LENS_EXB.BIN");
    const uint8_t *filler = data_file("LENS_FILLER.BIN", &nf);
    if (!lensex0 || !lensex1 || !lensex2 || !lensex3 || !lensex4 || !lensexp || !lensexb || !filler) return;

    int a, r, g, b, c, i, x, y;
    dis_partstart();
    vga::setmode(0x13);
    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, 0);
    a = (int16_t)W(lensexp, 2);
    pathdata1 = (const int16_t *)(lensexp + 4);
    pathdata2 = (const int16_t *)(lensexp + 4 + 2 * a);
    memcpy(palette, lensexb + 16, 768);
    memset(backmem, 0, sizeof backmem);
    memcpy(back, lensexb + 784, 64000);
    memcpy(back + 64000, filler, nf);
    memcpy(back + 64000, back + 64000 - 1536, 1536);
    lenswid = (int16_t)W(lensex0, 0);
    lenshig = (int16_t)W(lensex0, 2);
    const uint8_t *cp = lensex0 + 4;
    lensxs = lenswid / 2;
    lensys = lenshig / 2;
    for (i = 1; i < 4; i++) {
        r = (int8_t)*cp++;
        g = (int8_t)*cp++;
        b = (int8_t)*cp++;
        for (a = 0; a < 64 * 3; a += 3) {
            c = r + palette[a + 0]; if (c > 63) c = 63; palette[a + i * 64 * 3 + 0] = c;
            c = g + palette[a + 1]; if (c > 63) c = 63; palette[a + i * 64 * 3 + 1] = c;
            c = b + palette[a + 2]; if (c > 63) c = 63; palette[a + i * 64 * 3 + 2] = c;
        }
    }
    uint8_t *fp = fade;
    for (x = 0; x < 64; x++)
        for (y = 0; y < 64 * 3; y++) *fp++ = (uint8_t)((palette[y] * (63 - x) + x * 63) / 63);
    for (x = 0; x < 16; x++)
        for (y = 0; y < 64 * 3; y++) {
            a = palette[y] + (15 - x) * 5;
            if (a > 63) a = 63;
            *fp++ = (uint8_t)a;
        }
    fp = fade2;
    for (x = 0; x < 32; x++)
        for (y = 64 * 3; y < 256 * 3; y++) {
            a = y % (64 * 3);
            a = (palette[y] - palette[a] * (31 - x) / 31);
            *fp++ = (uint8_t)a;
        }
    for (x = 0; x < 256; x++)
        for (y = 0; y < 256; y++) {
            a = y * 10 / 11 - 36 / 2;
            if (a < 0 || a > 199) a = 0;
            rotpic[x + y * 256] = back[x + 32 + a * 320];
        }

    waitb();
    setpalarea(palette, 0, 256);

    if (!dis_exit()) part1();
    while (!dis_exit() && dis_musplus() < -20) {}
    dis_waitb();
    if (!dis_exit()) part2();
    if (!dis_exit()) part3();
}
