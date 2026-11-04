// PANICEND.EXE - the "TV switched off" end of the first half (WILDF).
// Port of PANIC/SHUTDOWN.C (loader build), PANIC/ASMYT.ASM, PANIC/TWEAK.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include <math.h>

namespace {

void tw_putpixel(int x, int y, int color)
{
    static const uint8_t planetau[4] = {1, 2, 4, 8};
    outpw(0x3c4, 0x02 | (planetau[x & 3] << 8));
    vga::wr((uint16_t)((x >> 2) + y * 160), (uint8_t)color);
}

int tw_getpixel(int x, int y)
{
    outpw(0x3ce, 0x04 | ((x & 3) << 8));
    return vga::rd((uint16_t)(y * 160 + (x >> 2)));
}

void tw_setpalette(const uint8_t *pal)
{
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, pal[i]);
}

void tw_setrgbpalette(int p, int r, int g, int b)
{
    outp(0x3c8, p);
    outp(0x3c9, r);
    outp(0x3c9, g);
    outp(0x3c9, b);
}

void tw_setstart(int s)
{
    outpw(0x3d4, 0x0d | ((s & 255) << 8));
    outpw(0x3d4, 0x0c | (((s >> 8) & 255) << 8));
}

// rep movsw inside video memory: each word is two byte reads then two byte
// writes, so in latch mode both bytes get the latches of the second read.
void copyline(uint16_t from, uint16_t to, int count)
{
    for (int i = 0; i < count; i++) {
        uint8_t a = vga::rd(from), b = vga::rd((uint16_t)(from + 1));
        vga::wr(to, a);
        vga::wr((uint16_t)(to + 1), b);
        from = (uint16_t)(from + 2);
        to = (uint16_t)(to + 2);
    }
}

uint8_t kuvapal[768];
uint8_t fadepals[64][768];

void shutdown()
{
    int x, y, a, b;
    for (a = 0; a < 320; a++) tw_putpixel(a, 0, 0);
    memset(fadepals, 0, sizeof fadepals);
    for (a = 0; a < 64; a++)
        for (b = 3; b < 768; b++) fadepals[a][b] = (uint8_t)((a * 63 + kuvapal[b] * (64 - a)) / 64);
    for (y = 0; y < 100; y++)
        for (x = 0; x < 320; x++) tw_putpixel(x, y + 150, tw_getpixel(x + 320, y * 4));
    tw_setstart(100 * 160);
    dis_waitb();
    outpw(0x3d4, 0x4109);
    outpw(0x3ce, 0x4105);   // latch transfer mode
    outpw(0x3c4, 0x0f02);   // map mask 1111
    tw_setpalette(fadepals[3]);
    tw_setrgbpalette(0, 63, 63, 63);
    tw_setstart(0);
    dis_waitb();
    tw_setpalette(fadepals[20]);
    outpw(0x3d4, 0xa013);
    dis_waitb();
    for (a = 32; a > 2; a = a * 5 / 6) {
        dis_waitb();
        tw_setpalette(fadepals[63 - a]);
        for (b = a / 2; b <= a; b++) {
            copyline(0, (uint16_t)(200 * 160 - (b * 320)), 80);
            copyline(0, (uint16_t)(200 * 160 + (b * 320)), 80);
        }
        for (b = 0; b < a; b++)
            copyline((uint16_t)(80 + (400 * b / a) * 160), (uint16_t)(200 * 160 + (b - a / 2) * 320), 80);
    }

    copyline(0, 202 * 160, 80);
    copyline(0, 198 * 160, 80);
    outpw(0x3ce, 0x4005);
    outpw(0x3c4, 0x0f02);

    for (x = 20; x <= 160; x += 3) {
        dis_waitb();
        tw_putpixel(x, 200, 0);
        tw_putpixel(320 - x, 200, 0);
        tw_putpixel(x + 1, 200, 0);
        tw_putpixel(319 - x, 200, 0);
        tw_putpixel(x + 2, 200, 0);
        tw_putpixel(318 - x, 200, 0);
        tw_putpixel(x + 3, 200, 0);
        tw_putpixel(317 - x, 200, 0);
    }

    tw_putpixel(160, 200, 1);
    for (a = 0; a < 60; a++) {
        dis_waitb();
        b = (int)(cos(a / 120.0 * 3 * 2 * 3.1415926535) * 31.0 + 32);
        tw_setrgbpalette(1, b, b, b);
    }
    // sleep(1)
    double t = sys::now();
    while (sys::now() - t < 1.0) sys::idle();
}

} // namespace

void part_panicend()
{
    dis_partstart();
    // getpal(kuvapal)
    outp(0x3c7, 0);
    for (int i = 0; i < 768; i++) kuvapal[i] = inp(0x3c9);
    shutdown();
}
