// MINVBALL.EXE - the mini vector balls (PSI). Port of DOTS/MAIN.C and DOTS/ASM.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include "common/msrand.h"
#include <utility>

extern const int16_t dots_sin1024[1024];

namespace {

const int16_t *sin1024 = dots_sin1024;
int isin(int deg) { return sin1024[deg & 1023]; }
int icos(int deg) { return sin1024[(deg + 256) & 1023]; }

struct Dot { int16_t x, y, z, old1, old2, old3, old4, yadd; };
Dot dot[1024];
int16_t gravitybottom, bpmin, bpmax, gravity, dotnum, gravityd;
uint16_t rows[200];
int16_t rotsin, rotcos;
uint8_t bgpic[65536 + 1024];
uint32_t depthtable1[128], depthtable2[128], depthtable3[128];

inline uint32_t BG32(uint16_t o) { return bgpic[o] | (bgpic[(uint16_t)(o + 1)] << 8) | (bgpic[(uint16_t)(o + 2)] << 16) | ((uint32_t)bgpic[(uint16_t)(o + 3)] << 24); }
inline void W8(uint16_t o, uint8_t v) { vga::wr(o, v); }
inline void W16(uint16_t o, uint16_t v) { vga::wr(o, (uint8_t)v); vga::wr((uint16_t)(o + 1), (uint8_t)(v >> 8)); }
inline void W32(uint16_t o, uint32_t v) { W16(o, (uint16_t)v); W16((uint16_t)(o + 2), (uint16_t)(v >> 16)); }

void restore_ball(Dot &d)
{
    uint16_t di = (uint16_t)d.old2;
    W32(di, BG32(di)); di = (uint16_t)(di + 320);
    W32(di, BG32(di)); di = (uint16_t)(di + 320);
    W32(di, BG32(di));
}

void drawdots()
{
    for (int n = 0; n < dotnum; n++) {
        Dot &d = dot[n];
        int32_t p1 = (int32_t)d.x * rotsin;
        int32_t p2 = (int32_t)d.z * rotcos;
        int16_t bp = (int16_t)((int16_t)(p2 >> 16) - (int16_t)(p1 >> 16) + 9000);
        int32_t v = (int32_t)d.x * rotcos + (int32_t)d.z * rotsin;
        v >>= 8;
        v = v + (v >> 3);
        int16_t sx = (int16_t)(bp ? v / bp : 0) + 160;
        if ((uint16_t)sx > 319) {                  // @@2: restore ball and shadow
            restore_ball(d);
            uint16_t di = (uint16_t)d.old1;
            uint32_t eax = BG32((uint16_t)(d.old2 + 640));
            d.old1 = (int16_t)(uint16_t)eax;       // (sic) the original stores AX here
            W16(di, (uint16_t)(bgpic[di] | (bgpic[(uint16_t)(di + 1)] << 8)));
            continue;
        }
        // shadow
        int16_t sy = (int16_t)((int32_t)(8 << 16) / bp) + 100;
        if ((uint16_t)sy > 199) {
            restore_ball(d);
            uint16_t di = (uint16_t)d.old1;
            uint32_t eax = BG32((uint16_t)(d.old2 + 640));
            d.old1 = (int16_t)(uint16_t)eax;
            W16(di, (uint16_t)(bgpic[di] | (bgpic[(uint16_t)(di + 1)] << 8)));
            continue;
        }
        uint16_t bx = (uint16_t)(rows[sy] + sx);
        uint16_t di = (uint16_t)d.old1;
        W16(di, (uint16_t)(bgpic[di] | (bgpic[(uint16_t)(di + 1)] << 8)));
        W16(bx, 87 + 87 * 256);
        d.old1 = (int16_t)bx;
        // ball
        d.yadd = (int16_t)(d.yadd + gravity);
        int16_t y = (int16_t)(d.y + d.yadd);
        if (y >= gravitybottom) {
            int16_t na = (int16_t)(((int16_t)(-d.yadd) * gravityd) >> 4);
            d.yadd = na;
            y = (int16_t)(y + d.yadd);
        }
        d.y = y;
        int16_t by = (int16_t)(((int32_t)y << 6) / bp) + 100;
        if ((uint16_t)by > 199) { restore_ball(d); continue; }
        bx = (uint16_t)(rows[by] + sx);
        restore_ball(d);
        int16_t b = (int16_t)(((uint16_t)bp >> 6) & ~3);
        if (b < bpmin) bpmin = b;
        if (b > bpmax) bpmax = b;
        int k = (b >> 2) & 127;
        W16((uint16_t)(bx + 1), (uint16_t)depthtable1[k]);
        W32((uint16_t)(bx + 320), depthtable2[k]);
        W16((uint16_t)(bx + 641), (uint16_t)depthtable3[k]);
        d.old2 = (int16_t)bx;
    }
}

const int cols[] = {0, 0, 0, 4, 25, 30, 8, 40, 45, 16, 55, 60};

} // namespace

void part_minvball()
{
    static uint8_t pal[768], pal2[768];
    static int dottaul[1024];
    MsRand rnd;
    int frame = 0, rota = -1 * 64, rot = 0, rots = 0;
    int a, b, c, d, i, j = 0, grav, gravd, f = 0, dropper, repeat;
    memset(dot, 0, sizeof dot);
    gravitybottom = 8000; bpmin = 30000; bpmax = -30000; gravity = 0; gravityd = 16;
    rotsin = rotcos = 0;

    dis_partstart();
    dotnum = 512;
    for (a = 0; a < dotnum; a++) dottaul[a] = a;
    for (a = 0; a < 500; a++) {
        b = rnd() % dotnum; c = rnd() % dotnum;
        d = dottaul[b]; dottaul[b] = dottaul[c]; dottaul[c] = d;
    }
    dropper = 22000;
    for (a = 0; a < dotnum; a++) {
        dot[a].x = 0; dot[a].y = (int16_t)(2560 - dropper); dot[a].z = 0; dot[a].yadd = 0;
    }
    grav = 3; gravd = 13; gravitybottom = 8105; i = -1;
    for (a = 0; a < 500; a++) {
        b = rnd() % dotnum; c = rnd() % dotnum;
        std::swap(dot[b].x, dot[c].x); std::swap(dot[b].y, dot[c].y); std::swap(dot[b].z, dot[c].z);
    }
    for (a = 0; a < 200; a++) rows[a] = (uint16_t)(a * 320);
    vga::setmode(0x13);
    outp(0x3c8, 0);
    for (a = 0; a < 16; a++)
        for (b = 0; b < 4; b++) {
            c = 100 + a * 9;
            outp(0x3c9, cols[b * 3 + 0]);
            outp(0x3c9, cols[b * 3 + 1] * c / 256);
            outp(0x3c9, cols[b * 3 + 2] * c / 256);
        }
    outp(0x3c8, 255); outp(0x3c9, 31); outp(0x3c9, 0); outp(0x3c9, 15);
    outp(0x3c8, 64);
    for (a = 0; a < 100; a++) {
        c = 64 - 256 / (a + 4);
        c = c * c / 64;
        outp(0x3c9, c / 4); outp(0x3c9, c / 4); outp(0x3c9, c / 4);
    }
    outp(0x3c7, 0);
    for (a = 0; a < 768; a++) pal[a] = inp(0x3c9);
    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, 0);
    for (a = 0; a < 100; a++) vga::fill((100 + a) * 320, a + 64, 320);
    for (a = 0; a < 128; a++) {
        c = a - (43 + 20) / 2;
        c = c * 3 / 4;
        c += 8;
        if (c < 0) c = 0;
        else if (c > 15) c = 15;
        c = 15 - c;
        depthtable1[a] = 0x202 + 0x04040404u * c;
        depthtable2[a] = 0x02030302 + 0x04040404u * c;
        depthtable3[a] = 0x202 + 0x04040404u * c;
    }
    memset(bgpic, 0, sizeof bgpic);
    for (a = 0; a < 64000; a++) bgpic[a] = vga::rd(a);
    for (b = 64; b >= 0; b--) {
        for (c = 0; c < 768; c++) { a = pal[c] - b; if (a < 0) a = 0; pal2[c] = a; }
        dis_waitb();
        dis_waitb();
        outp(0x3c8, 0);
        for (c = 0; c < 768; c++) outp(0x3c9, pal2[c]);
    }

    while (!dis_exit() && frame < 2450) {
        repeat = dis_waitb();
        if (frame > 2300) { outp(0x3c8, 0); for (c = 0; c < 768; c++) outp(0x3c9, pal2[c]); }
        a = dis_musplus();
        if (a > -4 && a < 0) break;
        while (repeat--) {
            frame++;
            if (frame == 500) f = 0;
            i = dottaul[j];
            j++; j %= dotnum;
            if (frame < 500) {
                dot[i].x = (int16_t)(isin(f * 11) * 40);
                dot[i].y = (int16_t)(icos(f * 13) * 10 - dropper);
                dot[i].z = (int16_t)(isin(f * 17) * 40);
                dot[i].yadd = 0;
            } else if (frame < 900) {
                dot[i].x = (int16_t)(icos(f * 15) * 55);
                dot[i].y = (int16_t)dropper;
                dot[i].z = (int16_t)(isin(f * 15) * 55);
                dot[i].yadd = -260;
            } else if (frame < 1700) {
                a = sin1024[frame & 1023] / 8;
                dot[i].x = (int16_t)(icos(f * 66) * a);
                dot[i].y = 8000;
                dot[i].z = (int16_t)(isin(f * 66) * a);
                dot[i].yadd = -300;
            } else if (frame < 2360) {
                dot[i].x = (int16_t)(rnd() - 16384);
                dot[i].y = (int16_t)(8000 - rnd() / 2);
                dot[i].z = (int16_t)(rnd() - 16384);
                dot[i].yadd = 0;
                if (frame > 1900 && !(frame & 31) && grav > 0) grav--;
            } else if (frame < 2400) {
                a = frame - 2360;
                for (b = 0; b < 768; b += 3) {
                    c = pal[b + 0] + a * 3; if (c > 63) c = 63; pal2[b + 0] = c;
                    c = pal[b + 1] + a * 3; if (c > 63) c = 63; pal2[b + 1] = c;
                    c = pal[b + 2] + a * 4; if (c > 63) c = 63; pal2[b + 2] = c;
                }
            } else if (frame < 2440) {
                a = frame - 2400;
                for (b = 0; b < 768; b += 3) {
                    c = 63 - a * 2;
                    if (c < 0) c = 0;
                    pal2[b + 0] = pal2[b + 1] = pal2[b + 2] = c;
                }
            }
            if (dropper > 4000) dropper -= 100;
            rotcos = (int16_t)(icos(rot) * 64);
            rotsin = (int16_t)(isin(rot) * 64);
            rots += 2;
            if (frame > 1900) {
                rot += rota / 64;
                rota--;
            } else rot = isin(rots);
            f++;
            gravity = (int16_t)grav;
            gravityd = (int16_t)gravd;
        }
        drawdots();
    }
}
