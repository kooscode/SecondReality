// GLENZ.EXE - the bouncing checkerboard and the glenz vectors (PSI).
// Port of GLENZ/MAIN.C, ZOOMER.C (zoomer2), VEC.ASM, NEW.ASM, MATH.ASM,
// VID.ASM (init320x200) and the GL10.LIB helpers it uses.
#include "../core/dos.h"
#include "../core/parts.h"
#include <atomic>
#include <utility>

extern const int16_t glenz_sin1024[1024];
extern const int16_t glenz_mathsin[4501];

namespace {

const int16_t *sin1024 = glenz_sin1024;
inline int16_t sintable16(int i) { return glenz_mathsin[i]; }
inline int16_t costable16(int i) { return glenz_mathsin[900 + i]; }

// GL10.LIB setpalarea(pal,start,count): count colours
void setpalarea(const uint8_t *pal, int start, int cnt)
{
    outp(0x3c8, start);
    for (int i = 0; i < cnt * 3; i++) outp(0x3c9, pal[i]);
}

inline void vset(uint32_t off, uint8_t v) { vga::wr(off & 0xFFFF, v); }
inline void vfill(uint32_t off, uint8_t v, int n) { for (int i = 0; i < n; i++) vga::wr((off + i) & 0xFFFF, v); }

// ======================================================================== ZOOMER.C
void zoomer2()
{
    static uint8_t pal1[768], pal2[768];
    int a, b, c, y;
    int frame = 0;
    int zly, zy, zya;
    int zly2, zy2;

    outpw(0x3c4, 0x0f02);
    outp(0x3c7, 0);
    for (a = 0; a < 768; a++) pal1[a] = inp(0x3c9);

    zy = 0; zya = 0; zly = 0;
    zy2 = 0; zly2 = 0;
    frame = 0;
    for (;;) {   // while(!kbhit()): the demo loader swallows the keyboard
        if (zy == 260) break;
        zly = zy;
        zya++;
        zy += zya / 4;
        if (zy > 260) zy = 260;
        uint32_t v = zly * 80;
        for (y = zly; y <= zy; y++) { vga::fill(v & 0xFFFF, 255, 80); v += 80; }
        zly2 = zy2;
        zy2 = 125 * zy / 260;
        v = (399 - zy2) * 80;
        for (y = zly2; y <= zy2; y++) { vga::fill(v & 0xFFFF, 255, 80); v += 80; }
        c = frame;
        if (c > 32) c = 32;
        b = 32 - c;
        for (a = 0; a < 128 * 3; a++) pal2[a] = (uint8_t)((pal1[a] * b + 30 * c) >> 5);
        frame++;
        dis_waitb();
        setpalarea(pal2, 0, 128);
    }
    outp(0x3c8, 0);
    outp(0x3c9, 0);
    outp(0x3c9, 0);
    outp(0x3c9, 0);
    uint32_t v = 0;
    for (y = 0; y <= 399; y++) {
        if (y <= 274 || y >= 260) vga::fill(v & 0xFFFF, 0, 80);
        v += 80;
    }
}

// ======================================================================== VID.ASM data
int32_t projxmul, projymul;
int16_t projxadd, projyadd;
int32_t projminz;
int16_t wminx, wminy, wmaxx, wmaxy;
int32_t xadd, yadd, zadd;

void init320x200()
{
    projxmul = 256; projymul = 213; projxadd = 160; projyadd = 130;
    projminz = 128;
    wminx = 0; wminy = 0; wmaxx = 319; wmaxy = 199;
}

// ======================================================================== MATH.ASM
void checkdeg(int &v)
{
    for (;;) {
        if ((uint16_t)v < 3600) return;
        if ((int16_t)v < 0) v = (int16_t)(v + 3600);
        else v = (int16_t)(v - 3600);
    }
}

inline int16_t m15(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 15); }

// cmatrix_yxz: note the original stores roty at [si+0] and rotx at [si+2], so
// the "X" rotation uses roty and the "Y" rotation uses rotx.
void cmatrix_yxz(int rotx, int roty, int rotz, int16_t *m)
{
    int a0 = (int16_t)roty, a1 = (int16_t)rotx, a2 = (int16_t)rotz;
    checkdeg(a0); checkdeg(a1); checkdeg(a2);
    int16_t rxsin = sintable16(a0), rxcos = costable16(a0);
    int16_t rysin = sintable16(a1), rycos = costable16(a1);
    int16_t rzsin = sintable16(a2), rzcos = costable16(a2);
    int16_t dx, cx;
    m[7] = m15(rysin, rzsin);
    m[0] = m15(rycos, rzcos);
    dx = m15(m[0], rxsin);
    m[7] = (int16_t)(m[7] - dx);
    cx = m15(rxsin, rysin);
    m[0] = (int16_t)(m[0] - m15(rzsin, cx));
    m[1] = m15(rzcos, cx);
    dx = m15(rycos, rzsin);
    m[1] = (int16_t)(m[1] + dx);
    m[6] = m15(rxsin, dx);
    m[6] = (int16_t)(m[6] + m15(rysin, rzcos));
    m[3] = (int16_t)-m15(rxcos, rzsin);
    m[4] = m15(rxcos, rzcos);
    m[2] = (int16_t)-m15(rxcos, rysin);
    m[8] = m15(rxcos, rycos);
    m[5] = rxsin;
}

// ======================================================================== VEC.ASM
int32_t mtr[9];
uint8_t lightshift;

void csetmatrix(const int16_t *m, int32_t x, int32_t y, int32_t z)
{
    xadd = x; yadd = y; zadd = z;
    for (int i = 0; i < 9; i++) mtr[i] = m[i];
}

// point lists: [count (low word of a long)] then x,y,z longs
void crotlist(int32_t *dest, const int32_t *src)
{
    int n = (uint16_t)src[0];
    int old = (uint16_t)dest[0];
    dest[0] = (dest[0] & ~0xffff) | (uint16_t)(old + n);
    int32_t *d = dest + 1 + old * 3;
    const int32_t *s = src + 1;
    for (int i = 0; i < n; i++, s += 3, d += 3) {
        int64_t x = (int64_t)mtr[0] * s[0] + (int64_t)mtr[1] * s[1] + (int64_t)mtr[2] * s[2];
        int64_t y = (int64_t)mtr[3] * s[0] + (int64_t)mtr[4] * s[1] + (int64_t)mtr[5] * s[2];
        int64_t z = (int64_t)mtr[6] * s[0] + (int64_t)mtr[7] * s[1] + (int64_t)mtr[8] * s[2];
        d[0] = (int32_t)(x >> 15) + xadd;
        d[1] = (int32_t)(y >> 15) + yadd;
        d[2] = (int32_t)(z >> 15) + zadd;
    }
}

// projected list: [count] then 12 byte entries {x,y,flags,pad (words), z (long)}
struct PP { int16_t x, y, f, pad; int32_t z; };

void cprojlist(uint8_t *dest, const int32_t *src)
{
    int n = (uint16_t)src[0];
    uint16_t old;
    memcpy(&old, dest, 2);
    uint16_t nn = (uint16_t)(old + n);
    memcpy(dest, &nn, 2);
    PP *d = (PP *)(dest + 4) + old;
    const int32_t *s = src + 1;
    for (int i = 0; i < n; i++, s += 3, d++) {
        int32_t z = s[2], y = s[1], x = s[0];
        int bp = 0;
        d->z = z;
        if (z < projminz) { z = projminz; bp |= 16; }
        int16_t ay = (int16_t)((int16_t)(int32_t)(((int64_t)y * projymul) / z) + projyadd);
        if (ay > wmaxy) bp |= 8;
        if (ay < wminy) bp |= 4;
        d->y = ay;
        int16_t ax = (int16_t)((int16_t)(int32_t)(((int64_t)projxmul * x) / z) + projxadd);
        if (ax > wmaxx) bp |= 2;
        if (ax < wminx) bp |= 1;
        d->x = ax;
        d->f = (int16_t)bp;
    }
}

void ccliplist(int32_t *list)
{
    int n = (uint16_t)list[0];
    int32_t *s = list + 1;
    for (int i = 0; i < n; i++, s += 3)
        if (s[1] >= 1500) s[1] = 1500;
}

// ---- polygon lists: [count][color][x,y]... ; 0 ends
int16_t polylist[256];
int16_t backpal_dummy;
uint8_t backpal[16 * 3];
uint8_t rolcol[256], rolused[256];

// returns hidden (carry) and the cross product in dx:ax
bool checkhiddenbx(const int16_t *v, int16_t &ax, int16_t &dx)
{
    int32_t a = (int32_t)(int16_t)(v[0] - v[2]) * (int16_t)(v[1] - v[5]);
    int32_t b = (int32_t)(int16_t)(v[1] - v[3]) * (int16_t)(v[0] - v[4]);
    int32_t r = (int32_t)((uint32_t)a - (uint32_t)b);
    ax = (int16_t)r;
    dx = (int16_t)(r >> 16);
    return dx < 0;
}

inline uint16_t shrd16(uint16_t ax, uint16_t dx, int n) { return (uint16_t)((ax >> n) | (dx << (16 - n))); }

void demo_glz2(int16_t *pl, bool hidden, int16_t, int16_t)
{
    if (hidden) return;
    uint8_t al = (uint8_t)pl[-1];
    al = (al >> 1) & 1;
    pl[-1] = (int16_t)((pl[-1] & 0xff00) | al);
}

void demo_glz(int16_t *pl, bool hidden, int16_t ax, int16_t dx)
{
    if (hidden) {
        uint16_t c = (uint16_t)pl[-1];
        uint8_t b = (uint8_t)c;
        uint8_t al = rolcol[b];
        rolcol[b] = 0;
        rolused[al] = 0;
        al = (uint8_t)c;
        al = ((al >> 1) & 1) << 2;
        pl[-1] = (int16_t)((c & 0xff00) | al);
        return;
    }
    uint8_t cl = lightshift;
    uint16_t a;
    if (cl == 9) {
        cl = 7;
        a = shrd16((uint16_t)ax, (uint16_t)dx, 7);
    } else {
        uint16_t a1 = shrd16((uint16_t)ax, (uint16_t)dx, 8);
        uint16_t a2 = shrd16(a1, (uint16_t)dx, 1);
        a = (uint16_t)(a2 + a1);
    }
    int16_t s = (int16_t)a;
    if (s < 0) s = 0;
    if (s > 63) s = 63;
    uint8_t ah = (uint8_t)s;
    uint8_t b = (uint8_t)pl[-1];
    uint8_t al = rolcol[b];
    int bp = 0;
    if (al == 0) {
        bp = 2;
        for (int cx = 15; cx > 0; cx--) {
            if (rolused[bp] == 0) break;
            bp += 2;
        }
        al = (uint8_t)bp;
        ah = 0;                      // mov ax,bp
        rolcol[b] = al;
        rolused[bp] = 1;
    }
    int16_t orig = pl[-1];
    al <<= 3;
    pl[-1] = (int16_t)((pl[-1] & 0xff00) | al);
    outp(0x3c8, al);
    uint8_t R, G, B;
    if (orig & 2) { B = ah; ah >>= 1; G = ah; R = cl; }
    else { R = G = B = ah; }
    for (int z = 0; z < 16; z++) {
        uint8_t r = (uint8_t)(R + (backpal[z * 3 + 0] >> 2));
        uint8_t g = (uint8_t)(G + (backpal[z * 3 + 1] >> 2));
        uint8_t bb = (uint8_t)(B + (backpal[z * 3 + 2] >> 2));
        outp(0x3c9, r < 64 ? r : 63);
        outp(0x3c9, g < 64 ? g : 63);
        outp(0x3c9, bb < 64 ? bb : 63);
    }
}

typedef void (*demofn)(int16_t *, bool, int16_t, int16_t);
const demofn demomodes[3] = {demo_glz, demo_glz, demo_glz2};
demofn demomode0 = demo_glz;

void ceasypolylist(int16_t *out, const int16_t *polys, const uint8_t *points3)
{
    const PP *pts = (const PP *)(points3 + 4);
    int16_t *di = out;
    int bp = -1;
    const int16_t *si = polys;
    for (;;) {
        int16_t n = *si++;
        if (n == 0) break;
        di++;                    // count, filled below
        *di++ = *si++;           // color
        int16_t *cnt = di;
        for (int k = 0; k < n; k++) {
            int ax = *si++;
            if (ax != bp) {      // adddot
                bp = ax;
                *di++ = pts[ax].x;
                *di++ = pts[ax].y;
            }
        }
        if (cnt[0] == di[-2] && cnt[1] == di[-1]) di -= 2;
        cnt[-2] = (int16_t)((di - cnt) / 2);
        int16_t ax, dx;
        bool h = checkhiddenbx(cnt, ax, dx);
        demomode0(cnt, h, ax, dx);
    }
    *di = 0;
}

// ======================================================================== NEW.ASM
const int NROWS = 256, MAXLINES = 512;
struct NE { int32_t x; int16_t y1, y2; uint16_t color; int next; int32_t dx; };
NE ne[MAXLINES + 8];
int nep[4096];            // row heads (1-based indexes into ne, 0 = none)
int nec;
uint16_t ndp0, ndp;
uint8_t newdata1[65536 + 16];
uint8_t bgpic[65536];
int nl[MAXLINES * 2 + 8];

inline uint16_t ND16(uint16_t o) { uint16_t v; memcpy(&v, newdata1 + o, 2); return v; }
inline void ND16w(uint16_t o, uint16_t v) { memcpy(newdata1 + o, &v, 2); }

void ng_init()
{
    nec = 1;
    ndp0 ^= 0x8000;
    ndp = ndp0;
    memset(nep, 0, sizeof nep);
    if (ND16(0) == 0) ND16w(0, 0xffff);
    if (ND16(0x8000) == 0) ND16w(0x8000, 0xffff);
}

void ng_polys(const int16_t *di)
{
    int bp = nec;
    for (;;) {
        int cx = di[0];
        if (cx == 0) break;
        uint16_t color = (uint16_t)di[1];
        const int16_t *first = di + 2;
        const int16_t *si = di;
        const int16_t *d2 = di + 2;
        for (; cx > 0; cx--) {
            si += 2;
            d2 += 2;
            if (cx == 1) d2 = first;
            if (bp > MAXLINES) continue;
            NE &e = ne[bp];
            e.color = color;
            int16_t bx = si[1], cy = d2[1], ax, dx;
            if (bx > cy) { std::swap(bx, cy); ax = d2[0]; dx = si[0]; }
            else { ax = si[0]; dx = d2[0]; }
            e.y1 = bx; e.y2 = cy;
            e.x = (int32_t)((uint32_t)(uint16_t)ax << 16);
            int32_t num = (int32_t)((uint32_t)(uint16_t)(dx - ax) << 16);
            int16_t h = (int16_t)(cy - bx);
            if (h == 0) continue;
            e.dx = (int32_t)((int64_t)num / (uint16_t)h);
            if (bx < 0) {
                if (e.y2 <= 0) continue;
                e.x += (int32_t)((uint32_t)e.dx * (uint32_t)(uint16_t)(-bx));
                bx = 0;
                e.y1 = 0;
            }
            int head = nep[bx & 4095];
            if (!head) {
                nep[bx & 4095] = bp;
                e.next = 0;
            } else {
                bool dup = false;
                for (int s = head; s; s = ne[s].next) {
                    if (e.y2 == ne[s].y2 && e.x == ne[s].x && e.dx == ne[s].dx) {
                        ne[s].color ^= (uint8_t)e.color;
                        dup = true;
                        break;
                    }
                }
                if (dup) continue;
                nep[bx & 4095] = bp;
                e.next = head;
            }
            bp++;
        }
        di = si + 2;
    }
    nec = bp;
}

void ng_pass2()
{
    int nlc = 0;              // active list length
    uint16_t bp = ndp;
    uint16_t yrowad = 0;
    for (int yrow = 0; yrow < 200; yrow++, yrowad += 320) {
        for (int b = nep[yrow]; b; b = ne[b].next) nl[nlc++] = b;
        for (int k = 1; k < nlc; k++) {      // insertion sort by x
            int i = nl[k];
            int32_t x = ne[i].x;
            int j = k - 1;
            while (j >= 0 && x < ne[nl[j]].x) { nl[j + 1] = nl[j]; j--; }
            nl[j + 1] = i;
        }
        uint16_t cx = 0x8000;
        int d = 0;
        for (int s = 0; s < nlc; s++) {
            int b = nl[s];
            if ((int16_t)yrow >= ne[b].y2) continue;
            nl[d++] = b;
            int32_t eax = ne[b].x;
            ne[b].x += ne[b].dx;
            int16_t ax = (int16_t)((uint32_t)eax >> 16);
            if (ax > 319) ax = 319;
            if (ax < 1) ax = 1;
            if (cx == (uint16_t)ax) {
                ND16w((uint16_t)(bp - 2), ND16((uint16_t)(bp - 2)) ^ ne[b].color);
            } else {
                ND16w(bp, (uint16_t)(ax + yrowad));
                ND16w((uint16_t)(bp + 2), ne[b].color);
                bp = (uint16_t)(bp + 4);
                cx = ne[b].color;       // (sic) the original keeps the colour, not x
            }
        }
        nlc = d;
    }
    ND16w(bp, 63999); ND16w((uint16_t)(bp + 2), 0); bp = (uint16_t)(bp + 4);
    ND16w(bp, 0xffff); ND16w((uint16_t)(bp + 2), 0); bp = (uint16_t)(bp + 4);
    ndp = bp;
}

void ng_pass3()
{
    uint16_t di = 0;
    uint16_t si = ndp0, bx = ndp0 ^ 0x8000;
    uint8_t al = 0, ah = 0;
    uint16_t dxp = ND16(bx), dxc = ND16((uint16_t)(bx + 2)); bx = (uint16_t)(bx + 4);
    uint16_t cxp = ND16(si), cxc = ND16((uint16_t)(si + 2)); si = (uint16_t)(si + 4);
    auto fill = [&](uint16_t to) {
        for (uint16_t p = di; p < to; p++) vga::wr(p, al | bgpic[p]);
    };
    for (int guard = 0; guard < 200000; guard++) {
        if (dxp < cxp || (dxp == cxp && cxp != 0xffff)) {
            // last list event first (or both at the same position)
            if (al != ah) fill(dxp);
            di = dxp;
            ah ^= (uint8_t)dxc;
            dxp = ND16(bx); dxc = ND16((uint16_t)(bx + 2)); bx = (uint16_t)(bx + 4);
        } else if (dxp == cxp) {
            return;              // both lists ended (0xffff)
        } else {
            if (al != ah) fill(cxp);
            di = cxp;
            al ^= (uint8_t)cxc;
            cxp = ND16(si); cxc = ND16((uint16_t)(si + 2)); si = (uint16_t)(si + 4);
        }
    }
}

void cglenzinit() { ng_init(); }
void cglenzpolylist(const int16_t *pl) { ng_polys(pl); }
void cglenzdone() { ng_pass2(); ng_pass3(); }

// ======================================================================== MAIN.C data
#define ZZZ 50
#define QQQ 99
#define PT14(a, b) \
    -(a)*ZZZ, -(a)*ZZZ, -(a)*ZZZ, (a)*ZZZ, -(a)*ZZZ, -(a)*ZZZ, (a)*ZZZ, (a)*ZZZ, -(a)*ZZZ, -(a)*ZZZ, (a)*ZZZ, -(a)*ZZZ, \
    -(a)*ZZZ, -(a)*ZZZ, (a)*ZZZ, (a)*ZZZ, -(a)*ZZZ, (a)*ZZZ, (a)*ZZZ, (a)*ZZZ, (a)*ZZZ, -(a)*ZZZ, (a)*ZZZ, (a)*ZZZ
int32_t points[64] = {14, PT14(100, 0),
                      0 * ZZZ, 0 * ZZZ, -170 * ZZZ, 0 * ZZZ, 0 * ZZZ, 170 * ZZZ, 170 * ZZZ, 0 * ZZZ, 0 * ZZZ,
                      -170 * ZZZ, 0 * ZZZ, 0 * ZZZ, 0 * ZZZ, 170 * ZZZ, 0 * ZZZ, 0 * ZZZ, -170 * ZZZ, 0 * ZZZ};
int32_t pointsb[64] = {14,
                       -60 * QQQ, -60 * QQQ, -60 * QQQ, 60 * QQQ, -60 * QQQ, -60 * QQQ, 60 * QQQ, 60 * QQQ, -60 * QQQ,
                       -60 * QQQ, 60 * QQQ, -60 * QQQ, -60 * QQQ, -60 * QQQ, 60 * QQQ, 60 * QQQ, -60 * QQQ, 60 * QQQ,
                       60 * QQQ, 60 * QQQ, 60 * QQQ, -60 * QQQ, 60 * QQQ, 60 * QQQ,
                       0, 0, -105 * QQQ, 0, 0, 105 * QQQ, 105 * QQQ, 0, 0, -105 * QQQ, 0, 0, 0, 105 * QQQ, 0, 0, -105 * QQQ, 0};
int32_t points2[256];
int32_t points2b[256];
uint8_t points3[256 * 2 * 2];

const int16_t epolys[] = {
    3, 0x4002, 0, 1, 8, 3, 0x4004, 1, 2, 8, 3, 0x4006, 2, 3, 8, 3, 0x4008, 3, 0, 8,
    3, 0x400a, 2, 1, 10, 3, 0x400c, 1, 5, 10, 3, 0x400e, 5, 6, 10, 3, 0x4010, 6, 2, 10,
    3, 0x4012, 2, 6, 12, 3, 0x4014, 6, 7, 12, 3, 0x4016, 7, 3, 12, 3, 0x4018, 3, 2, 12,
    3, 0x401a, 0, 3, 11, 3, 0x401c, 3, 7, 11, 3, 0x401e, 7, 4, 11, 3, 0x4020, 4, 0, 11,
    3, 0x4022, 5, 1, 13, 3, 0x4024, 1, 0, 13, 3, 0x4026, 0, 4, 13, 3, 0x4028, 4, 5, 13,
    3, 0x402a, 5, 4, 9, 3, 0x402c, 4, 7, 9, 3, 0x402e, 7, 6, 9, 3, 0x4030, 6, 5, 9,
    0};
const int16_t epolysb[] = {
    3, 0x4004, 0, 1, 8, 3, 0x4002, 1, 2, 8, 3, 0x4004, 2, 3, 8, 3, 0x4002, 3, 0, 8,
    3, 0x4004, 2, 1, 10, 3, 0x4002, 1, 5, 10, 3, 0x4004, 5, 6, 10, 3, 0x4002, 6, 2, 10,
    3, 0x4004, 2, 6, 12, 3, 0x4002, 6, 7, 12, 3, 0x4004, 7, 3, 12, 3, 0x4002, 3, 2, 12,
    3, 0x4004, 0, 3, 11, 3, 0x4002, 3, 7, 11, 3, 0x4004, 7, 4, 11, 3, 0x4002, 4, 0, 11,
    3, 0x4004, 5, 1, 13, 3, 0x4002, 1, 0, 13, 3, 0x4004, 0, 4, 13, 3, 0x4002, 4, 5, 13,
    3, 0x4004, 5, 4, 9, 3, 0x4002, 4, 7, 9, 3, 0x4004, 7, 6, 9, 3, 0x4002, 6, 5, 9,
    0};

int16_t matrix[9];
uint8_t pal[768];
uint8_t tmppal[768];
std::atomic<int> repeat_;

void copper()
{
    repeat_++;
    outp(0x3c8, 0);
    for (int a = 0; a < 16 * 3; a++) outp(0x3c9, pal[a]);
}

void waitb()
{
    while (!(inp(0x3da) & 8)) {}
    while ((inp(0x3da) & 8)) {}
}

void run(const uint8_t *fc)
{
    int a, b, c, y, rx, ry, rz, r, g, zpos = 7500, y1, y2, ypos, yposa;
    int ya, yy, boingm = 6, boingd = 7;
    int jello = 0, jelloa = 0;
    int xscale = 120, yscale = 120, zscale = 120, bscale = 0;
    int oxp = 0, oyp = 0, ozp = 0;
    int oxb = 0, oyb = 0, ozb = 0;
    int frame = 0;
    const uint8_t *fcrow[100], *fcrow2[16];

    dis_partstart();

    while (!dis_exit() && dis_musplus() < -19) {}
    dis_setmframe(0);

    zoomer2();

    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65535);
    vga::setmode(0x13);
    init320x200();   // testasm(): setvmode(0) + VIDINIT

    for (a = 0; a < 100; a++) fcrow[a] = fc + 768 + 16 + a * 320;
    for (a = 0; a < 16; a++) fcrow2[a] = fc + 768 + 16 + a * 320 + 100 * 320;

    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, 0);
    outp(0x3c8, 0);
    for (a = 0; a < 16 * 3; a++) outp(0x3c9, fc[a + 16]);
    yy = 0; ya = 0;
    while (!dis_exit()) {
        ya++; yy += ya;
        if (yy > 48 * 16) {
            yy -= ya;
            ya = -ya * 2 / 3;
            if (ya > -4 && ya < 4) break;
        }
        y = yy / 16;
        y1 = 130 + y / 2;
        y2 = 130 + y * 3 / 2;
        if (y2 != y1) b = 25600 / (y2 - y1);
        else b = 0;
        int ry2;
        uint32_t pd = (y1 - 4) * 320;
        for (ry2 = y1 - 4; ry2 < y1; ry2++, pd += 320) {
            if (ry2 > 199) continue;
            vfill(pd, 0, 320);
        }
        for (c = 0, ry2 = y1; ry2 < y2; ry2++, pd += 320, c += b) {
            if (ry2 > 199) continue;
            for (int x = 0; x < 320; x++) vset(pd + x, fcrow[c / 256][x]);
        }
        for (c = 0; c < 16; c++, pd += 320, ry2++) {
            if (ry2 > 199) continue;
            if (c > 7) vfill(pd, 0, 320);
            else for (int x = 0; x < 320; x++) vset(pd + x, fcrow2[c][x]);
        }
        waitb();
    }

    while (!dis_exit() && dis_getmframe() < 300) {}
    dis_waitb();

    for (a = 0; a < 16; a++) memcpy(backpal + a * 3, fc + 0x10 + a * 3, 3);
    uint8_t *pp = tmppal;
    for (a = 0; a < 256; a++) {
        if (a < 16) b = a;
        else b = a & 7;
        r = backpal[b * 3 + 0];
        g = backpal[b * 3 + 1];
        b = backpal[b * 3 + 2];
        if ((a & 8) && a > 15) { r += 16; g += 16; b += 16; }
        if (r > 63) r = 63;
        if (g > 63) g = 63;
        if (b > 63) b = 63;
        *pp++ = r; *pp++ = g; *pp++ = b;
    }
    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, tmppal[a]);
    lightshift = 9;
    rx = ry = rz = 0;
    ypos = -9000; yposa = 0;
    dis_waitb();
    for (int i = 0; i < 64000; i++) bgpic[i] = vga::rd(i);

    while (!dis_exit() && dis_getmframe() < 333) {}

    memcpy(pal, backpal, 16 * 3);
    dis_partstart();
    dis_waitb();

    outp(0x3c7, 0);
    for (a = 0; a < 16 * 3; a++) pal[a] = inp(0x3c9);
    dis_setcopper(0, copper);
    while (frame < 7000 && !dis_exit()) {
        a = dis_musplus();
        if (a < 0 && a > -16) break;

        int rep = dis_waitb();
        outp(0x3c8, 0);
        for (a = 0; a < 16 * 3; a++) outp(0x3c9, pal[a]);

        while (rep--) {
            frame++;
            rx += 32; ry += 7;
            rx %= 3 * 3600; ry %= 3 * 3600; rz %= 3 * 3600;

            if (frame > 900) {
                a = frame - 900;
                b = frame - 900; if (b > 50) b = 50;
                oxp = sin1024[(a * 3) & 1023] * b / 10;
                oyp = sin1024[(a * 5) & 1023] * b / 10;
                ozp = (sin1024[(a * 4) & 1023] / 2 + 128) * b / 16;
                if (frame > 1800) {
                    a = frame - 1800 + 64;
                    if (a > 1024) a = 1024;
                    oxb = (int16_t)((int32_t)(-sin1024[(a * 6) & 1023]) * (int32_t)a / 40L);
                    oyb = (int16_t)((int32_t)(-sin1024[(a * 7) & 1023]) * (int32_t)a / 40L);
                    ozb = (int16_t)((int32_t)(sin1024[(a * 8) & 1023] + 128) * (int32_t)a / 40L);
                } else {
                    oxb = -sin1024[(a * 6) & 1023];
                    oyb = -sin1024[(a * 7) & 1023];
                    ozb = sin1024[(a * 8) & 1023] + 128;
                }
                b = 1800 - frame;
                if (b < 0) {
                    if (b < -99) b = -99;
                    oyp = (int16_t)(oyp - b * b / 2);
                }
            }

            if (frame > 800) {
                if (frame > 1220 + 789) {
                    if (xscale > 0) xscale -= 1;
                    if (yscale > 0) yscale -= 1;
                    if (zscale > 0) zscale -= 1;
                    if (bscale > 0) bscale -= 1;
                } else if (frame > 1400 + 789) {
                    if (bscale > 0) bscale -= 8;
                    if (bscale < 0) bscale = 0;
                } else {
                    if (bscale < 180) bscale += 2;
                    else bscale = 180;
                }
                if (bscale > xscale) lightshift = 10;
            } else {
                if (frame < 640 + 70) {
                    yposa += 31;
                    ypos += yposa / 40;
                    if (ypos > -300) {
                        ypos -= yposa / 40;
                        yposa = -yposa * boingm / boingd;
                        boingm += 2; boingd++;
                    }
                    if (ypos > -900 && yposa > 0) {
                        jello = (ypos + 900) * 5 / 3;
                        jelloa = 0;
                    }
                } else {
                    if (ypos > -2800) ypos -= 16;
                    else if (ypos < -2800) ypos += 16;
                }
                yscale = xscale = 120 + jello / 30;
                zscale = 120 - jello / 30;
                a = jello;
                jello += jelloa;
                if ((a < 0 && jello > 0) || (a > 0 && jello < 0)) jelloa = jelloa * 5 / 6;
                a = jello / 20;
                jelloa -= a;
            }

            if (frame > 1280 + 789) {
                b = 1280 + 789 + 64 - frame;
                if (b < 0) b = 0;
                for (a = 0; a < 16 * 3; a++) pal[a] = (uint8_t)(backpal[a] * b / 64);
            } else if (frame > 700) {
                if (frame < 765) {
                    b = 764 - frame;
                    if (b < 0) b = 0;
                    for (a = 0; a < 16 * 3; a++) pal[a] = (uint8_t)(backpal[a] * b / 64);
                } else if (frame < 790) {
                    y = 150 + (frame - 765) * 2;
                    memset(bgpic + y * 320, 0, 640);
                    vfill(y * 320, 0, 640);
                    if (frame > 785)
                        for (a = 0; a < 16; a++) {
                            r = g = b = 0;
                            if (a & 1) r += 10;
                            if (a & 2) r += 30;
                            if (a & 4) r += 20;
                            if (a & 8) { r += 16; g += 16; b += 16; }
                            if (r > 63) r = 63;
                            if (g > 63) g = 63;
                            if (b > 63) b = 63;
                            backpal[a * 3 + 0] = r;
                            backpal[a * 3 + 1] = g;
                            backpal[a * 3 + 2] = b;
                        }
                } else if (frame < 795) {
                    memcpy(pal, backpal, 16 * 3);
                }
            }
        }

        cglenzinit();

        if (xscale > 4) {
            demomode0 = demomodes[1];
            cmatrix_yxz(rx, ry, rz, matrix);
            csetmatrix(matrix, 0, 0, 0);
            points2b[0] = 0; crotlist(points2b, points);
            matrix[0] = (int16_t)(xscale * 64); matrix[1] = 0; matrix[2] = 0;
            matrix[3] = 0; matrix[4] = (int16_t)(yscale * 64); matrix[5] = 0;
            matrix[6] = 0; matrix[7] = 0; matrix[8] = (int16_t)(zscale * 64);
            csetmatrix(matrix, 0 + oxp, ypos + 1500 + oyp, zpos + ozp);
            points2[0] = 0; crotlist(points2, points2b);
            if (frame < 800) ccliplist(points2);
            memset(points3, 0, 4); cprojlist(points3, points2);
            ceasypolylist(polylist, epolys, points3);
            cglenzpolylist(polylist);
        }

        if (frame > 800 && bscale > 4) {
            demomode0 = demomodes[2];
            cmatrix_yxz(3600 - rx / 3, 3600 - ry / 3, 3600 - rz / 3, matrix);
            csetmatrix(matrix, 0, 0, 0);
            points2b[0] = 0; crotlist(points2b, pointsb);
            matrix[0] = (int16_t)(bscale * 64); matrix[1] = 0; matrix[2] = 0;
            matrix[3] = 0; matrix[4] = (int16_t)(bscale * 64); matrix[5] = 0;
            matrix[6] = 0; matrix[7] = 0; matrix[8] = (int16_t)(bscale * 64);
            csetmatrix(matrix, 0 + oxb, ypos + 1500 + oyb, zpos + ozb);
            points2[0] = 0; crotlist(points2, points2b);
            memset(points3, 0, 4); cprojlist(points3, points2);
            ceasypolylist(polylist, epolysb, points3);
            cglenzpolylist(polylist);
        }

        cglenzdone();
    }
    dis_setcopper(0, nullptr);
}

} // namespace

void part_glenz()
{
    const uint8_t *fc = data_file("GLENZ_FC.BIN");
    if (!fc) return;
    for (int a = 0; a < 16; a++) { backpal[a * 3] = backpal[a * 3 + 1] = backpal[a * 3 + 2] = 16; }
    memset(rolcol, 0, sizeof rolcol);
    memset(rolused, 0, sizeof rolused);
    memset(newdata1, 0, sizeof newdata1);
    memset(bgpic, 0, sizeof bgpic);
    ndp0 = ndp = 0;
    lightshift = 0;
    repeat_ = 0;
    run(fc);
}
