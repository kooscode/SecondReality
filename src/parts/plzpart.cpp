// PLZPART.EXE - plasma and the plasma cube (WILDF).
// Port of PLZPART/MAIN.C, PLZ.C, VECT.C, PLZFILL.C, COPPER.ASM, ASMYT.ASM,
// PLZA.ASM, SPLINE.ASM, TWEAK.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include <atomic>
#include <math.h>

extern const uint8_t plz_psini[16384];
extern const uint16_t plz_lsini4[8192];
extern const uint16_t plz_lsini16[8192];
extern const uint8_t plz_sinit[2575];
extern const int16_t plz_spline[1024];
extern const uint8_t plz_ptau[129];
extern const int16_t plz_rata[1088];

namespace {

// ======================================================================== globals (PLZ.C)
int16_t l1, l2, l3, l4, k1, k2, k3, k4;
int16_t il1, il2, il3, il4, ik1, ik2, ik3, ik4;

// ======================================================================== COPPER.ASM
std::atomic<int> frame_count;
volatile int cop_drop;
const uint8_t *volatile cop_pal;
volatile int do_pal;
volatile int cop_start;
volatile int cop_scrl;
volatile int cop_plz = 1;
uint8_t pompi;
uint8_t fadepal[768 * 2];
const int16_t *volatile cop_fadepal;
uint16_t dtau[65];

void set_lc(int v, bool bit8)
{
    outpw(0x3d4, 0x18 | ((v & 255) << 8));
    outpw(0x3d4, bit8 ? 0x1f07 : 0x0f07);
}

void copper1()
{
    outpw(0x3d4, 0x0d | ((cop_start & 255) << 8));
    outpw(0x3d4, 0x0c | (((cop_start >> 8) & 255) << 8));
    outp(0x3c0, 0x33);
    outp(0x3c0, cop_scrl);
}

void pompota()
{
    cop_scrl = 4;
    outpw(0x3d4, 0x3c18);
    outpw(0x3d4, 0x0f07);
    uint16_t ax = 0x3c18;
    pompi++;
    if (pompi & 1) {
        cop_scrl = 0;
        ax = 0x3d18;
    }
    outpw(0x3d4, ax);
}

void moveplz()
{
    k1 = (k1 - 3) & 4095; k2 = (k2 - 2) & 4095; k3 = (k3 + 1) & 4095; k4 = (k4 + 2) & 4095;
    l1 = (l1 - 1) & 4095; l2 = (l2 - 2) & 4095; l3 = (l3 + 2) & 4095; l4 = (l4 + 3) & 4095;
}

void initpparas()
{
    l1 = il1; l2 = il2; l3 = il3; l4 = il4;
    k1 = ik1; k2 = ik2; k3 = ik3; k4 = ik4;
}

void do_drop()
{
    cop_drop = cop_drop + 1;
    if (cop_drop <= 64) {
        uint16_t bx = dtau[cop_drop];
        outpw(0x3d4, 0x18 | ((bx & 255) << 8));
        uint8_t ah = (uint8_t)((((bx >> 8) << 4) & 0x10) | 0x0f);
        outpw(0x3d4, 0x07 | (ah << 8));
        return;
    }
    if (cop_drop >= 256) { cop_drop = 0; return; }
    if (!(cop_drop >= 128) && cop_drop > 64 + 32) { cop_drop = 0; return; }
    cop_pal = fadepal;
    do_pal = 1;
    if (cop_drop == 65) {
        outpw(0x3d4, 0x9018);
        outpw(0x3d4, 0x1f07);
        initpparas();
        return;
    }
    outpw(0x3d4, 0x3c18);
    outpw(0x3d4, 0x0f07);
    const int16_t *s = cop_fadepal;
    for (int i = 0; i < 768; i++) {
        unsigned v = (fadepal[i] << 8) | fadepal[i + 768];
        v += (uint16_t)s[i];
        fadepal[i + 768] = (uint8_t)v;
        fadepal[i] = (uint8_t)(v >> 8);
    }
}

void copper2()
{
    frame_count++;
    if (do_pal) {
        outp(0x3c8, 0);
        const uint8_t *p = cop_pal;
        for (int i = 0; i < 768; i++) outp(0x3c9, p[i]);
        do_pal = 0;
    }
    if (cop_plz) {
        pompota();
        moveplz();
    }
    if (cop_drop) do_drop();
}
void copper3() {}

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

// ======================================================================== TWEAK.ASM
void tw_opengraph()
{
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
    outpw(0x3d4, 0x0209);    // 134 y
    outpw(0x3d4, 0x5013);    // 640 wide
}

void tw_opengraph2()
{
    outpw(0x3c4, 0x0604);
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
    outpw(0x3d4, 0x0014);
    outpw(0x3d4, 0xe317);
    outpw(0x3d4, 0x0009);
    outpw(0x3d4, 0x3013);    // 384 wide
}

void tw_setrgbpalette(int p, int r, int g, int b)
{
    outp(0x3c8, p); outp(0x3c9, r); outp(0x3c9, g); outp(0x3c9, b);
}

// ======================================================================== ASMYT.ASM (plasma)
uint8_t poro[65536];      // code segment "poro": psini, lsini4, lsini16
int16_t pc1, pc2, pc3, pc4;

inline uint8_t PB(uint16_t a) { return poro[a]; }
inline uint16_t PW(uint16_t a) { return (uint16_t)(poro[a] | (poro[(uint16_t)(a + 1)] << 8)); }

void setplzparas(int c1, int c2, int c3, int c4) { pc1 = c1; pc2 = c2; pc3 = c3; pc4 = c4; }

void plzline(int y1, uint16_t base)
{
    uint16_t si = (uint16_t)(y1 * 2), di = si;
    for (int ccc = 0; ccc < 84; ccc++) {
        uint16_t bx = PW((uint16_t)(si + 2 * pc2 + 32768 - ccc * 8 + 80 * 8));
        uint8_t v = PB((uint16_t)(ccc * 32 + bx + pc1 + 0 + ccc * 8));
        bx = PW((uint16_t)(ccc * 64 + di + 2 * pc4 + 16384 + ccc * 32));
        v = (uint8_t)(v + PB((uint16_t)(bx + di + pc3 + 0 - ccc * 4 + 80 * 4)));
        vga::wr((uint16_t)(base + ccc), v);
    }
}

void set_plzstart(int start)
{
    outpw(0x3d4, 0x18 | ((start & 255) << 8));
    uint8_t ah = (uint8_t)((((start >> 8) << 4) & 0x10) | 0x0f);
    outpw(0x3d4, 0x07 | (ah << 8));
}

// ======================================================================== PLZ.C
const int MAXY = 280;
int16_t pals[6][768];
int curpal;
int timetable[10] = {64 * 6 * 2 - 45, 64 * 6 * 4 - 45, 64 * 6 * 5 - 45, 64 * 6 * 6 - 45, 64 * 6 * 7 + 90, 0};
int ttptr;
const int inittable[10][8] = {{1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670},
                              {1000, 2000, 4000, 4000, 1500, 2300, 3900, 1670},
                              {3500, 1000, 3000, 1000, 3500, 3300, 2900, 2670},
                              {1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670},
                              {1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670},
                              {1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670}};

void init_plz()
{
    const uint8_t *ptau = plz_ptau;
    tw_opengraph2();
    cop_start = 96 * (682 - 400);
    set_plzstart(60);
    init_copper();
    for (int a = 0; a < 256; a++) tw_setrgbpalette(a, 63, 63, 63);
    int16_t *p;
    // RGB
    p = &pals[0][3];
    for (int a = 1; a < 64; a++) *p++ = ptau[a], *p++ = ptau[0], *p++ = ptau[0];
    for (int a = 0; a < 64; a++) *p++ = ptau[63 - a], *p++ = ptau[0], *p++ = ptau[0];
    for (int a = 0; a < 64; a++) *p++ = ptau[0], *p++ = ptau[0], *p++ = ptau[a];
    for (int a = 0; a < 64; a++) *p++ = ptau[a], *p++ = ptau[0], *p++ = ptau[63 - a];
    // RB-black
    p = &pals[1][3];
    for (int a = 1; a < 64; a++) *p++ = ptau[a], *p++ = ptau[0], *p++ = ptau[0];
    for (int a = 0; a < 64; a++) *p++ = ptau[63 - a], *p++ = ptau[0], *p++ = ptau[a];
    for (int a = 0; a < 64; a++) *p++ = ptau[0], *p++ = ptau[a], *p++ = ptau[63 - a];
    for (int a = 0; a < 64; a++) *p++ = ptau[a], *p++ = ptau[63], *p++ = ptau[a];
    // RB-white
    p = &pals[3][3];
    for (int a = 1; a < 64; a++) *p++ = ptau[a], *p++ = ptau[0], *p++ = ptau[0];
    for (int a = 0; a < 64; a++) *p++ = ptau[63], *p++ = ptau[a], *p++ = ptau[a];
    for (int a = 0; a < 64; a++) *p++ = ptau[63 - a], *p++ = ptau[63 - a], *p++ = ptau[63];
    for (int a = 0; a < 64; a++) *p++ = ptau[0], *p++ = ptau[0], *p++ = ptau[63];
    // white
    p = &pals[2][3];
    for (int a = 1; a < 64; a++) *p++ = ptau[0] / 2, *p++ = ptau[0] / 2, *p++ = ptau[0] / 2;
    for (int a = 0; a < 64; a++) *p++ = ptau[a] / 2, *p++ = ptau[a] / 2, *p++ = ptau[a] / 2;
    for (int a = 0; a < 64; a++) *p++ = ptau[63 - a] / 2, *p++ = ptau[63 - a] / 2, *p++ = ptau[63 - a] / 2;
    for (int a = 0; a < 64; a++) *p++ = ptau[0] / 2, *p++ = ptau[0] / 2, *p++ = ptau[0] / 2;
    // white II
    p = &pals[4][3];
    for (int a = 1; a < 75; a++) *p++ = ptau[63 - a * 64 / 75], *p++ = ptau[63 - a * 64 / 75], *p++ = ptau[63 - a * 64 / 75];
    for (int a = 0; a < 106; a++) *p++ = 0, *p++ = 0, *p++ = 0;
    for (int a = 0; a < 75; a++) *p++ = ptau[a * 64 / 75] * 8 / 10, *p++ = ptau[a * 64 / 75] * 9 / 10, *p++ = ptau[a * 64 / 75];

    p = &pals[0][0];
    for (int a = 0; a < 768; a++, p++) *p = (int16_t)((*p - 63) * 2);
    for (int a = 768; a < 768 * 5; a++, p++) *p = (int16_t)(*p * 8);
}

void plz()
{
    while (dis_musplus() < 0 && !dis_exit()) {}
    dis_setmframe(0);

    init_plz();
    cop_drop = 128;
    cop_fadepal = pals[curpal++];

    frame_count = 0;
    while (!dis_exit()) {
        frame_count = 0;
        if (dis_getmframe() > timetable[ttptr]) {
            memset(fadepal, 0, 768);
            cop_drop = 1;
            cop_fadepal = pals[curpal++];
            ttptr++;
            il1 = inittable[ttptr][0]; il2 = inittable[ttptr][1]; il3 = inittable[ttptr][2]; il4 = inittable[ttptr][3];
            ik1 = inittable[ttptr][4]; ik2 = inittable[ttptr][5]; ik3 = inittable[ttptr][6]; ik4 = inittable[ttptr][7];
        }
        // the shipped PLZPART.EXE tests curpal==4 (PLZ.C says 5): the plasma
        // ends after the grey palette, in time for the cube's music sync
        if (curpal == 4 && cop_drop > 64) break;

        outpw(0x3c4, 0x0a02);
        setplzparas(k1, k2, k3, k4);
        for (int y = 0; y < MAXY; y += 2) plzline(y, (uint16_t)(y * 96));
        setplzparas(l1, l2, l3, l4);
        for (int y = 1; y < MAXY; y += 2) plzline(y, (uint16_t)(y * 96));

        outpw(0x3c4, 0x0502);
        setplzparas(k1, k2, k3, k4);
        for (int y = 1; y < MAXY; y += 2) plzline(y, (uint16_t)(y * 96));
        setplzparas(l1, l2, l3, l4);
        for (int y = 0; y < MAXY; y += 2) plzline(y, (uint16_t)(y * 96));
        sys::idle();
    }
    cop_drop = 0;
    frame_count = 0;
    while (frame_count == 0) sys::idle();
    set_plzstart(500);
    cop_plz = 0;
}

// ======================================================================== PLZA.ASM (cube spans)
const uint8_t start_mask[4] = {0xf, 0xe, 0xc, 0x8};
const uint8_t end_mask[4] = {0x1, 0x3, 0x7, 0xf};

// kuva1/dist1/kuva2/kuva3 in their own 64K "segments" (reads may run past 16 KB)
uint8_t kuvaseg[4][65536];      // 0=kuva1 1=kuva2 2=kuva3 3=dist1
const uint8_t *kuvataus[4];
int16_t clrtau[8][256][2];
int clrptr;

int16_t xx, yy;
int32_t yy1, xx1, yy2, xx2, ay1, ax1_, ay2, ax2_;
int32_t txx1, txy1, txx2, txy2, tax1, tay1, tax2, tay2;
uint16_t to;
const uint8_t *from;
const uint8_t *dseg;     // dist1 + dd*256
int ctau_i;                         // index into clrtau (rows may start above it)
int16_t clrdummy[2];
inline int16_t &CT(int k)
{
    int i = ctau_i + k;
    if (i < 0 || i >= 8 * 256 * 2) return clrdummy[k & 1];
    return (&clrtau[0][0][0])[i];
}

inline uint8_t texel(uint16_t bx)
{
    uint8_t si = dseg[bx];          // movzx si,[fs:bx]
    return from[(uint16_t)(bx + si)];
}

void advance_line()
{
    to = (uint16_t)(to + 160);
    yy++;
    ctau_i += 2;
    xx1 += ax1_; xx2 += ax2_;
    txy1 += tay1; txx1 += tax1;
    txy2 += tay2; txx2 += tax2;
}

void do_block(int ycount)
{
    if (ycount == 0) return;
    for (int lc = ycount; lc > 0; lc--) {
        if (yy < 0) { advance_line(); continue; }
        if (yy >= 134) return;
        uint16_t di = to;
        uint16_t ax = (uint16_t)(xx2 >> 16);
        if (!(ax > (uint16_t)CT(0))) CT(0) = (int16_t)ax;
        ax >>= 2;
        di = (uint16_t)(di + ax);
        uint16_t si = (uint16_t)(xx1 >> 16);
        if (!(si < (uint16_t)CT(1))) CT(1) = (int16_t)si;
        si >>= 2;
        int count = (int16_t)(si - ax);
        if (count < 0) { advance_line(); continue; }
        if (count == 0) {           // singlebyte
            uint16_t bx = (uint16_t)(((uint8_t)(txy2 >> 16) << 8) | (uint8_t)(txx2 >> 16));
            uint8_t cl = texel(bx);
            uint8_t m = start_mask[(uint16_t)(xx2 >> 16) & 3] & end_mask[(uint16_t)(xx1 >> 16) & 3];
            outpw(0x3c4, 0x02 | (m << 8));
            vga::wr(di, cl);
            advance_line();
            continue;
        }
        if (count == 1) {           // twobyte
            uint16_t bx = (uint16_t)(((uint8_t)(txy2 >> 16) << 8) | (uint8_t)(txx2 >> 16));
            uint8_t cl = texel(bx);
            bx = (uint16_t)(((uint8_t)(txy1 >> 16) << 8) | (uint8_t)(txx1 >> 16));
            uint8_t ch = texel(bx);
            outpw(0x3c4, 0x02 | (start_mask[(uint16_t)(xx2 >> 16) & 3] << 8));
            vga::wr(di, cl);
            outpw(0x3c4, 0x02 | (end_mask[(uint16_t)(xx1 >> 16) & 3] << 8));
            vga::wr((uint16_t)(di + 1), ch);
            advance_line();
            continue;
        }
        outpw(0x3c4, 0x0f02);
        // packed texture stepping (see PLZA.ASM)
        uint32_t ebx = ((uint32_t)(uint16_t)txy1 << 16) | (uint16_t)((uint32_t)txx1 >> 16);
        uint32_t ecx = ((uint32_t)(uint16_t)txx1 << 16) | (uint16_t)((uint32_t)ax2_ >> 16);
        int32_t esi = count;
        int32_t addy = (txy2 - txy1) / esi;
        int32_t addx = (txx2 - txx1) / esi;
        uint32_t edx = ((uint32_t)addy << 16) | ((uint32_t)addy >> 16);
        edx = (edx & 0xffff00ffu) | ((edx & 0xff) << 8);       // dh = dl (y_add int)
        uint32_t eax = ((uint32_t)addx << 16) | ((uint32_t)addx >> 16);
        edx = (edx & 0xffffff00u) | (eax & 0xff);              // dl = al (x_add int)
        if (edx & 0x80) edx = (edx & 0xffff00ffu) | (((((edx >> 8) & 0xff) - 1) & 0xff) << 8);
        ebx = (ebx & 0xffff0000u) | ((uint8_t)(txy1 >> 16) << 8) | (uint8_t)(txx1 >> 16);
        auto step = [&]() -> uint8_t {
            uint16_t bx = (uint16_t)ebx;
            uint8_t t = texel(bx);
            uint64_t c = (uint64_t)ecx + eax;
            ecx = (uint32_t)c;
            uint64_t b = (uint64_t)ebx + edx + (c >> 32);
            ebx = (uint32_t)b;
            if (b >> 32) ebx = (ebx & 0xffff00ffu) | ((((ebx >> 8) + 1) & 0xff) << 8);
            return t;
        };
        // right to left from di+count-1 down to di+1
        for (int k = count - 1; k >= 1; k--) vga::wr((uint16_t)(di + k), step());
        advance_line();
    }
}

// ======================================================================== PLZFILL.C
int sini[2000];
uint8_t pal[768];
uint8_t fpal[768];
uint16_t vmem;

void initvect()
{
    for (int a = 0; a < 1524; a++) sini[a] = (int)(sin(a / 1024.0 * 3.14159265358979323846 * 4) * 127);
    memset(pal, 0, sizeof pal);
    for (int a = 1; a < 32; a++) { pal[0 * 192 + a * 3] = 0; pal[0 * 192 + a * 3 + 1] = 0; pal[0 * 192 + a * 3 + 2] = a * 2; }
    for (int a = 0; a < 32; a++) { pal[0 * 192 + a * 3 + 32 * 3] = a * 2; pal[0 * 192 + a * 3 + 1 + 32 * 3] = a * 2; pal[0 * 192 + a * 3 + 2 + 32 * 3] = 63; }
    for (int a = 0; a < 32; a++) { pal[1 * 192 + a * 3] = a * 2; pal[1 * 192 + a * 3 + 1] = 0; pal[1 * 192 + a * 3 + 2] = 0; }
    for (int a = 0; a < 32; a++) { pal[1 * 192 + a * 3 + 32 * 3] = 63; pal[1 * 192 + a * 3 + 1 + 32 * 3] = a * 2; pal[1 * 192 + a * 3 + 2 + 32 * 3] = 0; }
    for (int a = 0; a < 32; a++) { pal[2 * 192 + a * 3] = a; pal[2 * 192 + a * 3 + 1] = 0; pal[2 * 192 + a * 3 + 2] = a * 2 / 3; }
    for (int a = 0; a < 32; a++) { pal[2 * 192 + a * 3 + 32 * 3] = 31 - a; pal[2 * 192 + a * 3 + 1 + 32 * 3] = a * 2; pal[2 * 192 + a * 3 + 2 + 32 * 3] = 21; }
    memset(kuvaseg, 0, sizeof kuvaseg);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 256; x++) {
            int v = sini[(y * 4 + sini[x * 2]) & 511] / 4 + 32;
            kuvaseg[0][y * 256 + x] = (uint8_t)v;
            kuvaseg[1][y * 256 + x] = (uint8_t)(v + 64);
            kuvaseg[2][y * 256 + x] = (uint8_t)(v + 128);
        }
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 256; x++) kuvaseg[3][y * 256 + x] = (uint8_t)(int8_t)(sini[y * 8] / 3);
    for (int a = 0; a < 8 * 256; a++) { (&clrtau[0][0][0])[a * 2] = 640; (&clrtau[0][0][0])[a * 2 + 1] = 0; }
    kuvataus[0] = kuvaseg[0]; kuvataus[1] = kuvaseg[1]; kuvataus[2] = kuvaseg[2]; kuvataus[3] = kuvaseg[0];
}

struct P2 { int x, y; };

void edge(P2 *pnts, const P2 *txt, int s, int d, int32_t &axx, int32_t &xx_, int32_t &tx_, int32_t &ty_, int32_t &tax, int32_t &tay)
{
    int dx = pnts[d].x - pnts[s].x;
    int dy = pnts[d].y - pnts[s].y;
    if (dy == 0) dy++;
    axx = (int32_t)(65536L * dx / dy);
    xx_ = ((int32_t)pnts[s].x << 16) + 0x8000;
    tx_ = ((int32_t)txt[s].x << 16) + 0x8000;
    ty_ = ((int32_t)txt[s].y << 16) + 0x8000;
    tax = (int32_t)(65536L * (txt[d].x - txt[s].x) / dy);
    tay = (int32_t)(65536L * (txt[d].y - txt[s].y) / dy);
}

void do_poly(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int color, int dd)
{
    P2 pnts[4], txt[4] = {{64, 4}, {190, 4}, {190, 60}, {64, 60}};
    int n, m, s1, s2, d1, d2;
    dd = (dd + 1) & 63;
    pnts[0] = {x1, y1}; pnts[1] = {x2, y2}; pnts[2] = {x3, y3}; pnts[3] = {x4, y4};
    for (n = 0, m = 1; m < 4; m++) if (pnts[m].y < pnts[n].y) n = m;
    s1 = n; s2 = n; d1 = (s1 + 1) & 3; d2 = (s2 - 1) & 3;
    edge(pnts, txt, s1, d1, ax1_, xx1, txx1, txy1, tax1, tay1);
    edge(pnts, txt, s2, d2, ax2_, xx2, txx2, txy2, tax2, tay2);
    yy = (int16_t)pnts[s1].y;
    from = kuvataus[color & 3];
    to = (uint16_t)(vmem + yy * 160);
    dseg = kuvaseg[3] + dd * 256;
    ctau_i = clrptr * 512 + yy * 2;
    for (n = 0; n < 4;) {
        if (pnts[d1].y < pnts[d2].y) m = pnts[d1].y;
        else m = pnts[d2].y;
        do_block(m - yy);
        yy = (int16_t)m;
        if (pnts[d1].y == pnts[d2].y) {
            s1 = d1; d1 = (s1 + 1) & 3;
            s2 = d2; d2 = (s2 - 1) & 3; n += 2;
            edge(pnts, txt, s1, d1, ax1_, xx1, txx1, txy1, tax1, tay1);
            edge(pnts, txt, s2, d2, ax2_, xx2, txx2, txy2, tax2, tay2);
        } else if (pnts[d1].y < pnts[d2].y) {
            s1 = d1; d1 = (s1 + 1) & 3; n++;
            edge(pnts, txt, s1, d1, ax1_, xx1, txx1, txy1, tax1, tay1);
        } else {
            s2 = d2; d2 = (s2 - 1) & 3; n++;
            edge(pnts, txt, s2, d2, ax2_, xx2, txx2, txy2, tax2, tay2);
        }
    }
}

uint8_t clear_ch;      // CH left over from the clear loop (written by the mask pass)

void do_clear(uint16_t vm, int16_t *otau, int16_t *ntau)
{
    outpw(0x3c4, 0x0f02);
    uint16_t dx = vm;
    dx = (dx & 0xff00) | vga::rd(0);     // "mov dl,[es:0]" (fill latches)
    for (int ycnt = 134; ycnt >= 0; ycnt--, otau += 2, ntau += 2, dx = (uint16_t)(dx + 160)) {
        if (otau[0] != 640) {
            int di = (uint16_t)otau[0] >> 2;
            int cx = ((uint16_t)ntau[0] >> 2) - di;
            if (cx >= 0) { vga::fill((uint16_t)(di + dx), 0, cx); cx = 0; }
            clear_ch = (uint8_t)((uint16_t)cx >> 8);
            di = (uint16_t)ntau[1] >> 2;
            cx = ((uint16_t)otau[1] >> 2) - di;
            if (cx >= 0) { vga::fill((uint16_t)(di + dx + 1), 0, cx); cx = 0; }
            clear_ch = (uint8_t)((uint16_t)cx >> 8);
        }
        otau[0] = 640;
        otau[1] = 0;
    }
}

void do_clear_masks(uint16_t vm, const int16_t *ntau)
{
    uint16_t di = vm;
    for (int i = 0; i < 134; i++, ntau += 2, di = (uint16_t)(di + 160)) {
        uint16_t bx = (uint16_t)(ntau[0] - 1);
        outpw(0x3c4, 0x02 | (end_mask[bx & 3] << 8));
        vga::wr((uint16_t)((bx >> 2) + di), clear_ch);
        bx = (uint16_t)(ntau[1] + 1);
        outpw(0x3c4, 0x02 | (start_mask[bx & 3] << 8));
        vga::wr((uint16_t)((bx >> 2) + di), clear_ch);
    }
}

void clear()
{
    int16_t *otau = &clrtau[(clrptr - 3) & 7][0][0], *ntau = &clrtau[clrptr][0][0];
    clrptr = (clrptr + 1) & 7;
    do_clear(vmem, otau, ntau);
    do_clear_masks(vmem, ntau);
}

// ======================================================================== VECT.C
int16_t sinit_w[1288];
inline int SINIT(int i) { return sinit_w[i & 1023]; }
inline int KOSINIT(int i) { return sinit_w[(i & 1023) + 256]; }

struct Pt { int x, y, z, xx, yy, zz, xxx, yyy; };
struct Pg { int p1, p2, p3, p4, p5, p6, n, color; };
Pt points[8];
const int ptsrc[8][3] = {{125, 125, 125}, {125, -125, 125}, {-125, -125, 125}, {-125, 125, 125},
                         {125, 125, -125}, {125, -125, -125}, {-125, -125, -125}, {-125, 125, -125}};
const Pg faces[6] = {{1, 2, 3, 0, 0, 0, 0, 0}, {7, 6, 5, 4, 0, 0, 0, 0}, {0, 4, 5, 1, 0, 0, 0, 1},
                     {1, 5, 6, 2, 0, 0, 0, 2}, {2, 6, 7, 3, 0, 0, 0, 1}, {3, 7, 4, 0, 0, 0, 0, 2}};
int ptodraw[256];
int polys;
int light_src[6], lls[6];
int cxx, cxy, cxz, cyx, cyy, cyz, czx, czy, czz;
int kx, ky, kz, dis_, tx, ty;
int ls_kx, ls_ky, ls_kz, ls_x, ls_y, ls_z;
int page, frames;

void getspl(int position)
{
    uint16_t di = (uint16_t)position;
    int entry = di >> 8;
    int d = di & 255;
    int16_t out[8];
    for (int c = 0; c < 8; c++) {
        auto P = [&](int k) { int idx = (entry + k) * 8 + c; return idx < 1088 ? plz_rata[idx] : 0; };
        int32_t s = (int32_t)P(3) * plz_spline[d] + (int32_t)P(2) * plz_spline[d + 256] +
                    (int32_t)P(1) * plz_spline[d + 512] + (int32_t)P(0) * plz_spline[d + 768];
        out[c] = (int16_t)(s >> 15);
    }
    tx = out[0]; ty = out[1]; dis_ = out[2]; kx = out[3]; ky = out[4]; kz = out[5]; ls_kx = out[6]; ls_ky = out[7];
}

inline int32_t M(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }

void count_const()
{
    int32_t SX = SINIT(kx), SY = SINIT(ky), SZ = SINIT(kz);
    int32_t CX = KOSINIT(kx), CY = KOSINIT(ky), CZ = KOSINIT(kz);
    cxx = (int16_t)(M(CY, CZ) >> 22);
    cxy = (int16_t)(M(CY, SZ) >> 22);
    cxz = (int16_t)(-SY >> 7);
    cyx = (int16_t)((int32_t)((uint32_t)M((M(SX, CZ) + 16384) >> 15, SY) - (uint32_t)M(CX, SZ)) >> 22);
    cyy = (int16_t)((int32_t)((uint32_t)M((M(SX, SY) + 16384) >> 15, SZ) + (uint32_t)M(CX, CZ)) >> 22);
    cyz = (int16_t)(M(CY, SX) >> 22);
    czx = (int16_t)((int32_t)((uint32_t)M((M(CX, CZ) + 16384) >> 15, SY) + (uint32_t)M(SX, SZ)) >> 22);
    czy = (int16_t)((int32_t)((uint32_t)M((M(CX, SY) + 16384) >> 15, SZ) - (uint32_t)M(SX, CZ)) >> 22);
    czz = (int16_t)(M(CY, CX) >> 22);
}

void rotate()
{
    for (int a = 0; a < 8; a++) {
        int16_t x = (int16_t)ptsrc[a][0], y = (int16_t)ptsrc[a][1], z = (int16_t)ptsrc[a][2];
        Pt &p = points[a];
        int16_t xx_ = (int16_t)(((int16_t)((int16_t)(x * cxx) >> 1) + (int16_t)((int16_t)(y * cxy) >> 1) + (int16_t)((int16_t)(z * cxz) >> 1)) >> 7) + tx;
        int16_t yy_ = (int16_t)(((int16_t)((int16_t)(x * cyx) >> 1) + (int16_t)((int16_t)(y * cyy) >> 1) + (int16_t)((int16_t)(z * cyz) >> 1)) >> 7) + ty;
        int16_t zz_ = (int16_t)(((int16_t)((int16_t)(x * czx) >> 1) + (int16_t)((int16_t)(y * czy) >> 1) + (int16_t)((int16_t)(z * czz) >> 1)) >> 7) + dis_;
        p.xx = xx_; p.yy = yy_; p.zz = zz_;
        p.xxx = zz_ ? (int16_t)((xx_ * 256L) / zz_ + 160 + 160) : 0;
        p.yyy = zz_ ? (int16_t)((yy_ * 142L) / zz_ + 66) : 0;
    }
}

void shadepal(uint8_t *fp, const uint8_t *pp, int shd)
{
    for (int i = 0; i < 192; i++) fp[i] = (uint8_t)(((uint16_t)(pp[i] * (uint8_t)shd)) >> 6);
}

void sort_faces()
{
    int a = 0, p = 0;
    while (a < 6) {
        const Pg &f = faces[a];
        int32_t x = points[f.p1].xx, y = points[f.p1].yy, z = points[f.p1].zz;
        int32_t ax = points[f.p2].xx - x, ay = points[f.p2].yy - y, az = points[f.p2].zz - z;
        int32_t bx = points[f.p3].xx - x, by = points[f.p3].yy - y, bz = points[f.p3].zz - z;
        int32_t nx = ay * bz - az * by;
        int32_t ny = az * bx - ax * bz;
        int32_t nz = ax * by - ay * bx;
        int32_t s = -x * nx + -y * ny + -z * nz;
        if (s > 0) { a++; continue; }
        s = (ls_x * nx + ls_y * ny + ls_z * nz) / 250000 + 32;
        light_src[p] = (int16_t)s;
        int c = f.color;
        if (lls[p] != light_src[p]) {
            shadepal(&fpal[c * 64 * 3], &pal[c * 64 * 3], light_src[p]);
            lls[p] = light_src[p];
        }
        ptodraw[p++] = a++;
    }
    polys = p;
}

void calculate()
{
    getspl(4 * 256 + frames * 4);
    kx &= 1023; ky &= 1023; kz &= 1023;
    ls_kx &= 1023; ls_ky &= 1023;
    ls_y = KOSINIT(ls_kx) >> 8;
    ls_x = (int16_t)((SINIT(ls_kx) >> 8) * (SINIT(ls_ky) >> 8)) >> 7;
    ls_z = (int16_t)((SINIT(ls_kx) >> 8) * (KOSINIT(ls_ky) >> 8)) >> 7;
    count_const();
    rotate();
    sort_faces();
}

void draw()
{
    for (int a = 0; a < polys; a++) {
        const Pg &f = faces[ptodraw[a]];
        int sh = (page & 1) * 2;
        do_poly(points[f.p1].xxx + sh, points[f.p1].yyy, points[f.p2].xxx + sh, points[f.p2].yyy,
                points[f.p3].xxx + sh, points[f.p3].yyy, points[f.p4].xxx + sh, points[f.p4].yyy, f.color, frames & 63);
    }
}

void swappage()
{
    page = (page + 1) % 6;
    static const uint16_t vm[6] = {0x0000, 0x5500, 0xaa00, 0x0000, 0x5500, 0xaa00};
    static const uint16_t cs[6] = {0xaa00 + 40, 0x0000 + 40, 0x5500 + 40, 0xaa00 + 40, 0x0000 + 40, 0x5500 + 40};
    static const int sc[6] = {4, 0, 4, 0, 4, 0};
    vmem = vm[page];
    cop_start = cs[page];
    cop_scrl = sc[page];
}

void vect()
{
    tw_opengraph();
    while (dis_musplus() < 13 && !dis_exit()) {}
    frame_count = 0;
    while (!dis_exit()) {
        int a = dis_musplus();
        if (a >= -4 && a < 0) break;
        swappage();
        while (frame_count < 1) sys::idle();
        frames += frame_count;
        frame_count = 0;
        cop_pal = fpal;
        do_pal = 1;
        calculate();
        draw();
        clear();
    }
}

} // namespace

void part_plzpart()
{
    memset(poro, 0, sizeof poro);
    memcpy(poro, plz_psini, 16384);
    for (int i = 0; i < 8192; i++) {
        poro[16384 + i * 2] = (uint8_t)plz_lsini4[i];
        poro[16384 + i * 2 + 1] = (uint8_t)(plz_lsini4[i] >> 8);
        poro[32768 + i * 2] = (uint8_t)plz_lsini16[i];
        poro[32768 + i * 2 + 1] = (uint8_t)(plz_lsini16[i] >> 8);
    }
    for (int i = 0; i < 1287; i++) sinit_w[i] = (int16_t)(plz_sinit[i * 2] | (plz_sinit[i * 2 + 1] << 8));
    for (int c = 0; c <= 64; c++) dtau[c] = (uint16_t)(c * c / 4 * 43 / 128 + 60);

    l1 = 1000; l2 = 2000; l3 = 3000; l4 = 4000;
    k1 = 3500; k2 = 2300; k3 = 3900; k4 = 3670;
    il1 = 1000; il2 = 2000; il3 = 3000; il4 = 4000;
    ik1 = 3500; ik2 = 2300; ik3 = 3900; ik4 = 3670;
    frame_count = 0; cop_drop = 0; cop_pal = fadepal; do_pal = 0; cop_start = 0; cop_scrl = 0; cop_plz = 1;
    pompi = 0;
    memset(fadepal, 0, sizeof fadepal);
    memset(pals, 0, sizeof pals);
    curpal = 0; ttptr = 0;
    memset(fpal, 0, sizeof fpal);
    memset(light_src, 0, sizeof light_src); memset(lls, 0, sizeof lls);
    kx = ky = kz = 0; dis_ = 320; tx = 0; ty = -50;
    ls_kx = ls_ky = ls_kz = ls_x = ls_y = 0; ls_z = 128;
    page = 0; frames = 0; clrptr = 0; vmem = 0;

    dis_partstart();
    init_copper();
    initvect();
    plz();
    vect();
    close_copper();
}
