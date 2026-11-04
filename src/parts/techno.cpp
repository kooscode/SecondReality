// TECHNO.EXE - interference circles, rotating bars and the troll picture (PSI).
// Port of TECHNO/KOE.C, KOEA.ASM, KOEB.ASM, POLYCLIP.ASM, READP.C
#include "../core/dos.h"
#include "../core/parts.h"
#include "common/readp.h"
#include "../core/music.h"
#include <utility>

extern const int16_t techno_sin1024[1024];

namespace {

const int16_t *sin1024 = techno_sin1024;
const uint8_t *circle_data, *circle2_data;

uint8_t flip8[256];
uint8_t power0[16 * 256], power1[16 * 256];

inline void plane(int p) { outpw(0x3c4, 0x0002 | (p << 8)); }
inline void vwr(uint32_t off, uint8_t v) { vga::wr(off & 0xFFFF, v); }

void outpal(int start, const uint8_t *p, int n)
{
    outp(0x3c8, start);
    for (int i = 0; i < n; i++) outp(0x3c9, p[i]);
}

// ---------------------------------------------------------------- interference
// The circle images: 8 copies of plane 2, each shifted right one more pixel.
// One contiguous block like the DOS allocation (16384 paragraphs).
uint8_t memblk[64 + 8 * 32768 + 64];
uint8_t *circles(int k) { return memblk + 64 + k * 32768; }

void bltline(const uint8_t *si, uint16_t di, int ch, int cl)
{
    for (; cl > 0; cl--, ch <<= 1, si += 40) {
        outp(0x3c4, 2);
        outp(0x3c5, ch);
        for (int i = 0; i < 40; i++) vwr(di + i, si[i]);
    }
}

void bltlinerev(const uint8_t *si, uint16_t di, int ch, int cl)
{
    for (; cl > 0; cl--, ch <<= 1, si += 40) {
        outp(0x3c4, 2);
        outp(0x3c5, ch);
        for (int k = 0; k < 40; k++) vwr(di + k, flip8[si[39 - k]]);
    }
}

// one-bit right shift of a 32000 byte bit string (skipping the first 32 bytes, as the original)
void rotate1(const uint8_t *src, uint8_t *dst)
{
    int carry = 0;
    for (int blk = 1; blk <= 997; blk++) {
        int si = blk * 32;
        for (int z = 0; z < 32; z++) {
            uint8_t v = src[si + z];
            uint8_t r = (uint8_t)((v >> 1) | (carry << 7));
            carry = v & 1;
            dst[si + z] = r;
        }
    }
}

struct Interference {
    int framecount, palanimc, scrnpos, scrnposl, scrnx, scrny, scrnrot, sinurot;
    int overrot, overx, overya, patdir;
    uint8_t sinuspower, powercnt;
    // koeb only
    int sizefade, rotspeed, palfader;
    uint8_t pal[32 * 3];
};

void init_interference(Interference &s)
{
    outpw(0x3d4, 0x2813);
    // plane 2: circle2 mirrored to 640x400
    const uint8_t *si = circle2_data;
    uint16_t di = 0, bp = 80 * 399;
    for (int c = 0; c < 200; c++) {
        bltline(si, di, 4, 1);
        bltlinerev(si, di + 40, 4, 1);
        bltline(si, bp, 4, 1);
        bltlinerev(si, bp + 40, 4, 1);
        di += 80;
        bp -= 80;
        si += 40;
    }
    outpw(0x3ce, 0x0204);
    for (int i = 0; i < 32000; i++) circles(0)[i] = vga::rd(i);
    for (int k = 0; k < 7; k++) rotate1(circles(k), circles(k + 1));
    // planes 0..2: the three plane circle picture
    si = circle_data;
    di = 0; bp = 80 * 399;
    for (int c = 0; c < 200; c++) {
        bltline(si, di, 1, 3);
        bltlinerev(si, di + 40, 1, 3);
        bltline(si, bp, 1, 3);
        bltlinerev(si, bp + 40, 1, 3);
        di += 80;
        bp -= 80;
        si += 40 * 3;
    }
    s.framecount = 0;
}

void set_pan(int v)
{
    outp(0x3c0, 0x13);
    outp(0x3c0, v);
    outp(0x3c0, 32);
}

void set_start(int pos)
{
    outpw(0x3d4, 0x0c | ((pos >> 8) & 255) << 8);
    outpw(0x3d4, 0x0d | (pos & 255) << 8);
}

void anim_palanimc(Interference &s)
{
    int si = s.palanimc + s.patdir;
    if (si < 0) si = 8 * 3 - 3;
    if (si >= 8 * 3) si = 0;
    s.palanimc = si;
}

void move_patdir(Interference &s)
{
    int bx = dis_musrow() & 7;
    if (bx == 0) s.patdir = -3;
    if (bx == 4) s.patdir = -3;
}

void calc_scrnpos(Interference &s)
{
    int ax = s.scrnx, bx = ax;
    s.scrnposl = ax & 7;
    s.scrnpos = (uint16_t)(80 * s.scrny + ((int16_t)bx >> 3));
    set_start(s.scrnpos);
}

// --- KOEA
const uint8_t a_pal2[16 * 3] = {0, 0, 0, 10, 10 * 7 / 9, 10, 20, 20 * 7 / 9, 20, 30, 30 * 7 / 9, 30, 40, 40 * 7 / 9, 40,
                                50, 50 * 7 / 9, 50, 60, 60 * 7 / 9, 60, 30, 30 * 7 / 9, 30, 0, 0, 0, 10, 10 * 7 / 9, 10,
                                20, 20 * 7 / 9, 20, 30, 30 * 7 / 9, 30, 40, 40 * 7 / 9, 40, 50, 50 * 7 / 9, 50,
                                60, 60 * 7 / 9, 60, 30, 30 * 7 / 9, 30};
const uint8_t a_pal1[16 * 3] = {30, 30 * 8 / 9, 30, 60, 60 * 8 / 9, 60, 50, 50 * 8 / 9, 50, 40, 40 * 8 / 9, 40,
                                30, 30 * 8 / 9, 30, 20, 20 * 8 / 9, 20, 10, 10 * 8 / 9, 10, 0, 0, 0,
                                30, 30 * 8 / 9, 30, 60, 60 * 8 / 9, 60, 50, 50 * 8 / 9, 50, 40, 40 * 8 / 9, 40,
                                30, 30 * 8 / 9, 30, 20, 20 * 8 / 9, 20, 10, 10 * 8 / 9, 10, 0, 0, 0};

Interference ia, ib;

void do_interference_a(Interference &s)
{
    for (;;) {
        dis_waitb();
        set_pan(s.scrnposl);
        anim_palanimc(s);
        outpal(0, a_pal1 + s.palanimc, 8 * 3);
        outpal(8, a_pal2 + s.palanimc, 8 * 3);

        plane(8);
        uint16_t di = (uint16_t)s.scrnpos;
        int si = 0;
        int bp = (s.sinurot + 7 * 2) & 2047;
        s.sinurot = bp;
        for (int cx = 0; cx < 200; cx++) {
            bp = (bp + 9 * 2) & 2047;
            int16_t bx = (int16_t)(sin1024[bp >> 1] >> 3);
            int idx = ((uint8_t)bx) | (s.sinuspower << 8);
            int16_t ax = (int16_t)((int8_t)power0[idx] - s.scrnposl + s.overx);
            const uint8_t *src = circles(7 - (ax & 7));
            int off = si + (ax >> 3) + s.overya;
            for (int z = 0; z < 44; z++) vwr((uint16_t)(di + z), src[off + z]);
            di = (uint16_t)(di + 80);
            si += 80;
        }
        move_patdir(s);
        s.scrnrot = (s.scrnrot + 5) & 1023;
        s.scrnx = (sin1024[s.scrnrot] >> 2) + 160;
        s.scrny = (sin1024[(s.scrnrot + 256) & 1023] >> 2) + 100;
        s.overrot = (s.overrot + 7) & 1023;
        s.overx = (sin1024[s.overrot] >> 2) + 160;
        s.overya = (uint16_t)(((sin1024[(s.overrot + 256) & 1023] >> 2) + 100) * 80);
        calc_scrnpos(s);
        if (s.framecount >= 70 * 5) {
            s.powercnt++;
            if (s.powercnt >= 16) {
                s.powercnt = 0;
                if (s.sinuspower < 15) s.sinuspower++;
            }
        }
        s.framecount++;
        if ((uint16_t)dis_getmframe() >= 925) return;
        if (dis_exit()) return;
    }
}

// --- KOEB
const uint8_t b_pal0[16 * 3] = {0, 30, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 30, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void mixpal(const uint8_t *si, uint8_t *di, int cx, int dx)
{
    if (dx <= 256) {
        for (; cx > 0; cx--) *di++ = (uint8_t)(((uint16_t)(*si++ * dx)) >> 8);
    } else {
        int bx = dx - 256;
        for (; cx > 0; cx--) {
            int a = *si++ + bx;
            *di++ = (uint8_t)(a < 64 ? a : 63);
        }
    }
}

void resetmode13()
{
    dis_waitb();
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, 0);
    dis_waitb();
    vga::setmode(13);      // "mov ax,13": decimal 13 = mode 0Dh, 320x200x16
    inp(0x3da);
    for (int i = 0; i < 16; i++) { outp(0x3c0, i); outp(0x3c0, i); }
    outp(0x3c0, 0x11);
    outp(0x3c0, 255);
    outp(0x3c0, 32);
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, 0);
}

void do_interference_b(Interference &s)
{
    for (;;) {
        dis_waitb();
        set_pan(s.scrnposl);
        outpal(0, s.pal, 16 * 3);
        anim_palanimc(s);
        s.palfader += 2;
        if (s.palfader >= 512) s.palfader = 512;
        mixpal(b_pal0 + s.palanimc, s.pal, 8 * 3, s.palfader);
        mixpal(b_pal0 + s.palanimc, s.pal + 8 * 3, 8 * 3, s.palfader);
        outpal(0, s.pal, 16 * 3);
        move_patdir(s);
        s.scrnrot = (s.scrnrot + 5) & 1023;
        if (s.framecount >= 64) s.rotspeed++;
        s.scrnx = (int16_t)(((int32_t)sin1024[s.scrnrot] * (int16_t)s.sizefade) >> 16) + 160;
        s.scrny = (int16_t)(((int32_t)sin1024[(s.scrnrot + 256) & 1023] * (int16_t)s.sizefade) >> 16) + 100;
        s.overrot = (s.overrot + s.rotspeed) & 1023;
        s.overx = (int16_t)(((int32_t)(sin1024[s.overrot] >> 2) * (int16_t)s.sizefade) >> 16) + 160;
        s.overya = (uint16_t)(((int16_t)(((int32_t)(sin1024[(s.overrot + 256) & 1023] >> 2) * (int16_t)s.sizefade) >> 16) + 100) * 80);
        calc_scrnpos(s);
        s.framecount++;
        if (s.framecount == 256) return;
        if (dis_exit()) return;
    }
}

void dointerference2()
{
    memset(&ib, 0, sizeof ib);
    ib.patdir = -3;
    resetmode13();
    init_interference(ib);
    do_interference_b(ib);
}

// ---------------------------------------------------------------- bars (KOEA)
uint16_t rows40[200];
uint16_t blit16t[256];
uint8_t vbuf[8192 + 64];

void blitinit()
{
    for (int i = 0; i < 200; i++) rows40[i] = (uint16_t)(i * 40);
    for (int al = 0; al < 256; al++) {
        uint8_t dh = 255, dl = (uint8_t)al, ah = 0;
        for (int k = 0; k < 8; k++) {
            bool c = dl & 0x80;
            dl <<= 1;
            if (c) ah ^= dh;
            dh >>= 1;
        }
        uint8_t hi = (uint8_t)(((ah >> 1) | (ah << 7)) & 0x80);
        blit16t[al] = (uint16_t)(ah | (hi << 8));
    }
}

// XOR fill the edge buffer into one plane of video memory
void blit16(const uint8_t *si, uint32_t di, int dstride)
{
    for (int y = 0; y < 200; y++, si += 40, di += dstride) {
        uint8_t dh = 0;
        for (int z = 0; z < 40; z += 2) {
            uint16_t ax = blit16t[si[z] ^ dh];
            uint16_t dx = blit16t[si[z + 1] ^ (ax >> 8)];
            vwr(di + z, (uint8_t)ax);
            vwr(di + z + 1, (uint8_t)dx);
            dh = (uint8_t)(dx >> 8);
        }
    }
}

// one XOR edge into vbuf (ax,bx)-(dx,cx)
void drawline(int ax, int bx, int dx, int cx)
{
    bx = (int16_t)bx;
    if (bx == cx) return;
    if (bx > cx) { std::swap(bx, cx); std::swap(ax, dx); }
    int h = cx - bx;
    int di = h;
    if (bx < 0 || bx >= 200) return;
    int si = rows40[bx];
    if (!h) return;
    si += (uint16_t)ax >> 3;
    int bit = ax & 7;
    int cnt = h;
    int counter = -(int)((unsigned)di >> 1);
    if (ax >= dx) {
        int xc = ax - dx;
        for (;;) {
            vbuf[si & 8191] ^= (uint8_t)(0x80 >> bit);
            si += 40;
            if (--cnt == 0) return;
            counter += xc;
            if (counter < 0) continue;
            do {
                if (--bit < 0) { bit = 7; si--; }
                counter -= di;
            } while (counter >= 0);
        }
    } else {
        int xc = dx - ax;
        for (;;) {
            vbuf[si & 8191] ^= (uint8_t)(0x80 >> bit);
            si += 40;
            if (--cnt == 0) return;
            counter += xc;
            if (counter < 0) continue;
            do {
                if (++bit > 7) { bit = 0; si++; }
                counter -= di;
            } while (counter >= 0);
        }
    }
}

// POLYCLIP.ASM
const int16_t WMINY = 0, WMAXY = 199, WMINX = 0, WMAXX = 319;
int16_t clip_x1, clip_y1, clip_x2, clip_y2;

void clipmacro(int16_t &v1, int16_t v2, int16_t &w1, int16_t w2, int16_t wl)
{
    int16_t cx = (int16_t)(w2 - w1);
    if (cx == 0) { w1 = wl; return; }
    int16_t bp = (int16_t)(wl - w1);
    int16_t ax = (int16_t)(v2 - v1);
    ax = (int16_t)(((int32_t)ax * bp) / cx);
    v1 = (int16_t)(ax + v1);
    w1 = wl;
}

int cliplinex()
{
    int bl = 0, bh = 0;
    if (clip_x1 < WMINX) bl |= 1;
    if (clip_x1 > WMAXX) bl |= 2;
    if (clip_x2 < WMINX) bh |= 1;
    if (clip_x2 > WMAXX) bh |= 2;
    if (bl & bh) return 1;
    if (bl & 1) clipmacro(clip_y1, clip_y2, clip_x1, clip_x2, WMINX);
    if (bl & 2) clipmacro(clip_y1, clip_y2, clip_x1, clip_x2, WMAXX);
    if (bh & 1) clipmacro(clip_y2, clip_y1, clip_x2, clip_x1, WMINX);
    if (bh & 2) clipmacro(clip_y2, clip_y1, clip_x2, clip_x1, WMAXX);
    return 0;
}

int clipliney()
{
    int bl = 0, bh = 0;
    if (clip_y1 < WMINY) bl |= 4;
    if (clip_y1 > WMAXY) bl |= 8;
    if (clip_y2 < WMINY) bh |= 4;
    if (clip_y2 > WMAXY) bh |= 8;
    if (bl & bh) return 1;
    if (bl & 4) clipmacro(clip_x1, clip_x2, clip_y1, clip_y2, WMINY);
    if (bl & 8) clipmacro(clip_x1, clip_x2, clip_y1, clip_y2, WMAXY);
    if (bh & 4) clipmacro(clip_x2, clip_x1, clip_y2, clip_y1, WMINY);
    if (bh & 8) clipmacro(clip_x2, clip_x1, clip_y2, clip_y1, WMAXY);
    return 0;
}

struct XY { int16_t x, y; };
inline uint32_t pk(XY v) { return (uint16_t)v.x | ((uint32_t)(uint16_t)v.y << 16); }

int polyisides, polysides;
XY polyixy[16], polyxy[40], clipxy2[40];

void clipanypoly()
{
    // only the polygon (4 sides) case is used by TECHNO
    int n = polyisides;
    auto pass = [](const XY *in, int cnt, XY *out, bool ydir) -> int {
        uint32_t edx = 0x80008000u;
        int bx = 0;
        for (int i = 0; i < cnt; i++) {
            XY a = in[(i - 1 + cnt) % cnt], b = in[i];
            clip_x1 = a.x; clip_y1 = a.y; clip_x2 = b.x; clip_y2 = b.y;
            int r = ydir ? clipliney() : cliplinex();
            if (r) continue;
            XY p1 = {clip_x1, clip_y1}, p2 = {clip_x2, clip_y2};
            if (pk(p1) != edx) { out[bx++] = p1; edx = pk(p1); }
            if (pk(p2) != edx) { out[bx++] = p2; edx = pk(p2); }
        }
        if (bx && pk(out[0]) == edx) bx--;
        return bx;
    };
    int c = pass(polyixy, n, clipxy2, true);
    polysides = c;
    if (c <= 2) {
        if (c == 0) return;
        clip_x1 = clipxy2[0].x; clip_y1 = clipxy2[0].y;
        clip_x2 = clipxy2[1].x; clip_y2 = clipxy2[1].y;
        if (clipliney() || cliplinex()) { polysides = 0; return; }
        polyxy[0] = {clip_x1, clip_y1};
        polyxy[1] = {clip_x2, clip_y2};
        polysides = pk(polyxy[0]) == pk(polyxy[1]) ? 1 : 2;
        return;
    }
    polysides = pass(clipxy2, c, polyxy, false);
}

void asmbox(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    polyixy[0] = {(int16_t)x1, (int16_t)y1};
    polyixy[1] = {(int16_t)x2, (int16_t)y2};
    polyixy[2] = {(int16_t)x3, (int16_t)y3};
    polyixy[3] = {(int16_t)x4, (int16_t)y4};
    polyisides = 4;
    clipanypoly();
    int di = polysides;
    if (!di) return;
    int i = 0;
    for (di--; di > 0; di--, i++) drawline(polyxy[i].x, polyxy[i].y, polyxy[i + 1].x, polyxy[i + 1].y);
    drawline(polyxy[i].x, polyxy[i].y, polyxy[0].x, polyxy[0].y);
}

// ---------------------------------------------------------------- KOE.C
uint8_t palette[768];
uint8_t palfade[16 * 768];
uint8_t rowbuf[640 + 256];
uint8_t pal[16 * 16 * 3];
uint32_t vram;
int pl = 1, plv = 0;
int curpal = 0;
int lasta = 0;

int waitborder()
{
    int a = dis_musrow();
    if (a != lasta) {
        lasta = a;
        if ((a & 7) == 7) curpal = 15;
    }
    int r = dis_waitb();
    if (r > 10) r = 10;
    if (r < 0) r = 1;
    const uint8_t *p = pal + 16 * 3 * curpal;
    outp(0x3c8, 0);
    for (a = 0; a < 16 * 3; a++) outp(0x3c9, *p++);
    if (curpal) curpal--;
    return r;
}

int fpal1[16 * 3];
int *flash(int i)
{
    int pal2[16 * 3];
    if (i == -2) {
    } else if (i == -1) {
        outp(0x3c7, 0);
        for (int a = 0; a < 16 * 3; a++) fpal1[a] = inp(0x3c9);
    } else {
        int j = 256 - i;
        for (int a = 0; a < 16 * 3; a++) pal2[a] = (fpal1[a] * j + 63 * i) >> 8;
        dis_waitb();
        outp(0x3c8, 0);
        for (int a = 0; a < 16 * 3; a++) outp(0x3c9, pal2[a]);
    }
    return fpal1;
}

void boxes(int hx, int hy, int vx, int vy, int wx, int wy)
{
    for (int c = -10; c < 11; c += 2) {
        int cx = vx * c * 2, cy = vy * c * 2;
        int x1 = (-hx - vx + cx) / 16 + wx, y1 = (-hy - vy + cy) / 16 + wy;
        int x2 = (-hx + vx + cx) / 16 + wx, y2 = (-hy + vy + cy) / 16 + wy;
        int x3 = (+hx + vx + cx) / 16 + wx, y3 = (+hy + vy + cy) / 16 + wy;
        int x4 = (+hx - vx + cx) / 16 + wx, y4 = (+hy - vy + cy) / 16 + wy;
        asmbox(x1, y1, x2, y2, x3, y3, x4, y4);
    }
}

void flip_pages(int step, int base)
{
    int a = plv * 0x20;
    outpw(0x3d4, 0x0c | (a & 255) << 8);
    plv += step;
    plv &= 7;
    vram = 0x2000 * plv + base;
    if (!plv) {
        pl <<= 1;
        if (pl > 15) pl = 1;
    }
}

void doit1(int count)
{
    int rot = 45, vma = 0, vm = 50;
    waitborder();
    plv = 0; pl = 1;
    while (!dis_exit() && count > 0) {
        count -= waitborder();
        memset(vbuf, 0, 8000);
        int hx = sin1024[(rot + 0) & 1023] * 16 * 6 / 5;
        int hy = sin1024[(rot + 256) & 1023] * 16;
        int vx = sin1024[(rot + 256) & 1023] * 6 / 5;
        int vy = sin1024[(rot + 512) & 1023];
        vx = vx * vm / 100;
        vy = vy * vm / 100;
        boxes(hx, hy, vx, vy, 160, 100);
        rot += 2;
        vm += vma;
        if (vm < 25) { vm -= vma; vma = -vma; }
        vma--;
        plane(pl);
        blit16(vbuf, vram, 40);
        flip_pages(1, 0);
    }
}

void doit2(int count)
{
    int rot = 50, rota = 10, vma = 0, vm = 100 * 64;
    waitborder();
    plv = 0; pl = 1;
    while (!dis_exit() && count > 0) {
        count -= waitborder();
        memset(vbuf, 0, 8000);
        int hx = sin1024[(rot + 0) & 1023] * 16 * 6 / 5;
        int hy = sin1024[(rot + 256) & 1023] * 16;
        int vx = sin1024[(rot + 256) & 1023] * 6 / 5;
        int vy = sin1024[(rot + 512) & 1023];
        vx = vx * (vm / 64) / 100;
        vy = vy * (vm / 64) / 100;
        boxes(hx, hy, vx, vy, 160, 100);
        rot += rota / 10;
        vm += vma;
        if (vm < 0) { vm -= vma; vma = -vma; }
        vma--;
        rota++;
        plane(pl);
        blit16(vbuf, vram, 40);
        flip_pages(1, 0);
    }
}

void pan2(int xpos, int div)
{
    int a = xpos / div;
    outpw(0x3d4, 0x0d | (a & 255) << 8);
}

void doit3(int count, const uint8_t *picdata)
{
    int rot = 45, rota = 10, rot2 = 0;
    int a, c, y, wx, wy, vma = 0, vm = 100 * 64, xpos = 320, xposa = 0;
    int ripple, ripplep, repeat = 1;
    waitborder();
    outp(0x3c8, 0);
    for (a = 0; a < 16 * 3; a++) outp(0x3c9, 0);
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
    outpw(0x3d4, 0x0d + 256 * 40);
    outpw(0x3d4, 0x0c + 256 * 0);
    outpw(0x3d4, 0x13 + 256 * 40);
    vram = 40;
    plv = 0; pl = 1;
    while (!dis_exit() && count > 0) {
        a = dis_getmframe();
        if (a > 2520) {
            if (xpos != 0) xpos = 0;
            else break;
        }
        if (count < 333) {
            while (repeat--) {
                xpos -= xposa / 4;
                if (xpos < 0) xpos = 0;
                else xposa++;
            }
            if (xpos == 0) break;
        }
        if (rot2 < 32) {
            wx = sin1024[(rot2 + 0) & 1023] * rot2 / 8 + 160;
            wy = sin1024[(rot2 + 256) & 1023] * rot2 / 8 + 100;
        } else {
            wx = sin1024[(rot2 + 0) & 1023] / 4 + 160;
            wy = sin1024[(rot2 + 256) & 1023] / 4 + 100;
        }
        rot2 += 17;
        pan2(xpos, 8);
        count -= (repeat = waitborder());
        set_pan(xpos & 7);
        memset(vbuf, 0, 8000);
        int hx = sin1024[(rot + 0) & 1023] * 16 * 6 / 5;
        int hy = sin1024[(rot + 256) & 1023] * 16;
        int vx = sin1024[(rot + 256) & 1023] * 6 / 5;
        int vy = sin1024[(rot + 512) & 1023];
        vx = vx * (vm / 64) / 100;
        vy = vy * (vm / 64) / 100;
        boxes(hx, hy, vx, vy, wx, wy);
        rot += rota / 10;
        vm += vma;
        if (vm < 0) { vm -= vma; vma = -vma; }
        vma--;
        rota++;
        plane(pl);
        blit16(vbuf, vram, 80);
        flip_pages(2, 40);
    }

    vga::setmode(0x13);
    // inittwk (KOEA version clears the palette)
    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, 0);
    outpw(0x3d4, 0x0009);
    outpw(0x3d4, 0x0014);
    outpw(0x3d4, 0xe317);
    outpw(0x3c4, 0x0604);
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
    outpw(0x3d4, 0x13 + 256 * 80);
    vram = 0;

    if (dis_exit()) return;

    dis_waitb();
    dis_waitb();
    dis_waitb();

    using namespace readp_ns;
    readp(palette, -1, picdata);
    for (y = 0; y < 400; y++) {
        readp(rowbuf, y, picdata);
        lineblit(80U + (unsigned)y * 160U, rowbuf);
    }

    uint8_t *p = palfade;
    for (y = 0; y < 16; y++) {
        int x = (45 - y * 3);
        for (a = 0; a < 768; a++) {
            c = palette[a] + x;
            if (c > 63) c = 63;
            *p++ = (uint8_t)c;
        }
    }

    dis_waitb();
    setpalarea(palette, 0, 256);

    while (!dis_exit()) {
        dis::checkkeys();
        sys::idle();
        music::Pos mp = music::pos();
        int b = mp.row;
        a = mp.ord;
        if (a > 35 || (a == 35 && b > 48)) break;
    }
    count = 300;
    xposa = 0; xpos = 0;
    while (!dis_exit() && count > 0) {
        if (xpos == 320) break;
        xpos += xposa / 4;
        if (xpos > 320) xpos = 320;
        else xposa++;
        pan2(xpos, 4);
        count -= dis_waitb();
        set_pan((xpos & 3) * 2);
    }
    count = 50; c = 0;
    ripple = 0; ripplep = 8;
    while (!dis_exit() && count > 0) {
        if (ripplep > 1023) ripplep = 1024;
        else ripplep = ripplep * 5 / 4;
        xpos = 320 + sin1024[ripple & 1023] / ripplep;
        ripple += ripplep + 100;
        pan2(xpos, 4);
        count -= dis_waitb();
        set_pan((xpos & 3) * 2);
        if (c < 16) {
            setpalarea(palfade + c * 768, 0, 256);
            c++;
        }
    }
    setpalarea(palette, 0, 256);
    count = 420; xpos = 320;
    while (!dis_exit() && count > 0) {
        a = dis_musplus();
        if (a > -6 && a < 16) break;
        pan2(xpos, 4);
        count -= dis_waitb();
        set_pan((xpos & 3) * 2);
    }
}

void run(const uint8_t *troll)
{
    int a, b, c, x, y;
    uint8_t *p;
    dis_partstart();

    for (c = 0; c < 16; c++) {
        p = pal + 3 * 16 * c;
        for (a = 0; a < 16; a++) {
            x = 0;
            if (a & 1) x++;
            if (a & 2) x++;
            if (a & 4) x++;
            if (a & 8) x++;
            static const int lv[5][3] = {{0, 0, 0}, {38 * 64 / 111, 33 * 64 / 111, 44 * 64 / 111},
                                         {52 * 64 / 111, 45 * 64 / 111, 58 * 64 / 111},
                                         {67 * 64 / 111, 61 * 64 / 111, 73 * 64 / 111},
                                         {83 * 64 / 111, 77 * 64 / 111, 89 * 64 / 111}};
            int v0 = lv[x][0] * (10 + c * 9 / 9) / 10; if (v0 > 63) v0 = 63;
            int v1 = lv[x][1] * (10 + c * 7 / 9) / 10; if (v1 > 63) v1 = 63;
            int v2 = lv[x][2] * (10 + c * 5 / 9) / 10; if (v2 > 63) v2 = 63;
            *p++ = (uint8_t)v0; *p++ = (uint8_t)v1; *p++ = (uint8_t)v2;
        }
    }

    blitinit();

    for (int t = 0; t < 2; t++) {
        p = t ? power1 : power0;
        for (b = 0; b < 16; b++)
            for (c = 0; c < 256; c++) {
                if (b == 15) *p++ = (uint8_t)c;
                else {
                    a = c > 127 ? c - 256 : c;
                    *p++ = (uint8_t)(int8_t)(a * b / 15);
                }
            }
    }

    while (!dis_exit() && dis_musplus() < -4) {}
    dis_setmframe(0);

    dointerference2();

    memset(&ia, 0, sizeof ia);
    ia.overrot = 211;
    init_interference(ia);

    if (!dis_exit()) {
        do_interference_a(ia);

        flash(-1);
        flash(32);
        flash(64);
        flash(192);
        flash(256);

        outpw(0x3d4, 0x0013 + 256 * 20);
        outpw(0x3d4, 0x000d);
        outpw(0x3d4, 0x000c);
        inp(0x3da);
        outp(0x3c0, 0x13);
        outp(0x3c0, 0);
        outp(0x3c0, 0x20);
        outpw(0x3c4, 0x0f02);
        vga::fill(0, 0, 64000);
        outpw(0x3c4, 0x0f02);
        vga::fill(0, 255, 8000);
        int *ip = flash(-2);
        for (a = 0; a < 16; a++) {
            ip[a * 3 + 0] = a * 6 / 2;
            ip[a * 3 + 1] = a * 7 / 2;
            ip[a * 3 + 2] = a * 8 / 2;
        }

        while (!dis_exit() && dis_musplus() < -3) {}

        while (!dis_exit()) {
            a = dis_musrow() & 7;
            if (a == 7) break;
        }

        for (b = 0; b < 4 && !dis_exit(); b++) {
            int zly, zy, zya;
            zy = 0; zya = 0; zly = 0;
            for (a = 256; a > -400; a -= 32) {
                zly = zy;
                zya++;
                zy += zya;
                uint32_t v = zly * 40;
                for (y = zly; y < zy; y++) {
                    vga::fill((v + b * 10) & 0xFFFF, 0, 10);
                    v += 40;
                }
                if (a >= 0) flash(a);
                else flash(0);
            }
            while (!dis_exit()) {
                a = dis_musrow() & 7;
                if (a == 7) break;
            }
            flash(-1);
            flash(32);
            flash(64);
            flash(192);
            flash(256);
        }
    }
    dis_partstart();
    doit1(70 * 6);
    doit2(70 * 12);
    doit3(70 * 14, troll);
}

} // namespace

void part_techno()
{
    circle_data = data_file("TECH_CIRCLE.BIN");
    circle2_data = data_file("TECH_CIRCLE2.BIN");
    const uint8_t *troll = data_file("TROLL.UP");
    if (!circle_data || !circle2_data || !troll) return;
    for (int i = 0; i < 256; i++) {
        int r = 0;
        for (int b = 0; b < 8; b++) if (i & (1 << b)) r |= 0x80 >> b;
        flip8[i] = (uint8_t)r;
    }
    memset(memblk, 0, sizeof memblk);
    pl = 1; plv = 0; curpal = 0; lasta = 0; vram = 0;
    run(troll);
}
