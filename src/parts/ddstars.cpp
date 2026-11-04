// DDSTARS.EXE - the hidden star field part (PSI). Port of DDSTARS/KOE.ASM and STARS.ASM.
// 320x200 16 colour mode shown as 400 lines: stars on the even lines (top half
// mirrored to the bottom), text pictures rising on the odd lines. The original
// keeps a ring of 64 star buffers in EMS that are never cleared.
#include "../core/dos.h"
#include "../core/parts.h"

namespace {

const int STARS = 512, STARS2 = 1024;
const int BUFSZ = 8192;

uint8_t ems[128 * BUFSZ];       // 32*2 pages of 16 KB
const uint8_t *textpic;

uint16_t starvram;              // segment
uint16_t emmpage4, starlimit, startxtp0, starframe;
int16_t startxtopen, startxtclose;
uint8_t starpalfade;
int16_t nostar1, nostar2;
uint16_t rows1[200];
struct Star { uint16_t z; int16_t x, y, pad; } star[STARS2];
int16_t muldivx[256], muldivy[256];
uint32_t seed;

// segment registers that point at EMS buffers are modelled as buffer indices,
// a value >= 0xa000 is video memory
uint8_t *esbuf, *fsbuf;
uint16_t es_vram;               // the video page es holds when not an EMS buffer
bool es_is_vram;

uint16_t random16()
{
    uint64_t p = (uint64_t)0x343fd * seed;
    seed = (uint32_t)p + 0x269ec3;
    return (uint16_t)(p >> 32);
}

uint8_t *fetch4ax(uint16_t n) { return ems + (uint32_t)(n & 127) * BUFSZ; }

void clearsbu() { memset(ems, 0, sizeof ems); }

void starfetch0()
{
    emmpage4 = (emmpage4 + 1) & 63;
    esbuf = fetch4ax(emmpage4);
    es_is_vram = false;
}

void starfetch1() { fsbuf = fetch4ax((emmpage4 + 17) & 63); }

int16_t projz(int16_t v, int16_t m)
{
    int32_t p = (int32_t)v * m;
    return (int16_t)(p >> 14);      // shld dx,ax,2
}

void plotstars(int count, bool both)
{
    Star *s = star;
    for (int bp = count; bp; bp--, s++) {
        unsigned bl = s->z & 0xff;
        bool cf = bl < 2;
        bl = (bl - 2) & 0xff;
        s->z = (s->z & 0xff00) | bl;
        if (cf) {
            s->x = (int16_t)((random16() & 1023) - 512);
            s->y = (int16_t)((random16() & 1023) - 512);
            continue;
        }
        if (bp < starlimit) continue;
        uint16_t dy = (uint16_t)(projz(s->y, muldivy[bl]) + 100);
        if (dy > 99) continue;
        uint16_t dx = (uint16_t)(projz(s->x, muldivx[bl]) + 160);
        if (dx > 319) continue;
        int cl = dx & 7;
        unsigned bx = rows1[dy] + (dx >> 3);
        uint8_t ch = 0x80 >> cl;
        uint8_t z = (uint8_t)s->z;
        if (z >= 180) {
            esbuf[bx] |= ch;
            if (both) fsbuf[bx] |= ch;
        } else if (z >= 110) {
            esbuf[bx + 4096] |= ch;
            if (both) fsbuf[bx + 4096] |= ch;
        } else {
            esbuf[bx] |= ch;
            esbuf[bx + 4096] |= ch;
            if (both) { fsbuf[bx] |= ch; fsbuf[bx + 4096] |= ch; }
        }
    }
}

void staradd()
{
    if (starlimit) starlimit--;
    plotstars(STARS, false);
}

void staradd2()
{
    starfetch1();
    if (starlimit) starlimit -= 4;
    plotstars(STARS2, true);
}

// es:[di] store of n bytes through the current map mask
void eswrite(uint16_t di, const uint8_t *src, int n)
{
    if (es_is_vram) {
        uint32_t base = (uint32_t)(es_vram - 0xa000) * 16;
        for (int i = 0; i < n; i++) vga::wr((uint16_t)(base + (uint16_t)(di + i)), src ? src[i] : 0);
    } else {
        for (int i = 0; i < n; i++) esbuf[(uint16_t)(di + i) % BUFSZ] = src ? src[i] : 0;   // never reached in practice
    }
}

void risetext()
{
    int16_t ax = startxtopen;
    if (ax < 99) startxtopen = ++ax;
    int16_t dx = startxtclose;
    if (dx > 0) startxtclose = --dx;
    if (dx < ax) ax = dx;
    if (ax <= 0) return;
    if (ax <= 1) ax = 2;
    uint16_t di = (uint16_t)(150 - ax);
    nostar2 = ax;
    di = (uint16_t)(80 * di);
    nostar1 = (int16_t)di;
    const uint8_t *si = textpic + 0x40 + startxtp0;
    int cx = ax - 1;

    di = (uint16_t)(di - 40);
    outpw(0x3c4, 0x0f02);
    eswrite(di, nullptr, 40);
    di += 80;
    if (--cx == 0) {
        outpw(0x3c4, 0x0f02);
        eswrite(di, nullptr, 80);
        return;
    }
    outpw(0x3c4, 0x0f02);
    eswrite(di, nullptr, 40);
    di += 80;
    if (--cx == 0) return;
    if (--cx != 0) {
        outpw(0x3ce, 0x0400);       // set/reset: plane 2 on, plane 3 off
        outpw(0x3ce, 0x0c01);
        for (;;) {
            outpw(0x3c4, 0x0d02);
            eswrite(di, si, 40);
            si += 40;
            outpw(0x3c4, 0x0e02);
            eswrite(di, si, 40);
            si += 40;
            di += 80;
            if (--cx == 0) break;
        }
    }
    outpw(0x3ce, 0x0001);
    outpw(0x3c4, 0x0f02);
    eswrite(di, nullptr, 40);
    di += 80;
    di--;
    nostar2 = (int16_t)di;
}

void setac16()
{
    inp(0x3da);
    for (int i = 0; i < 16; i++) {
        outp(0x3c0, i);
        outp(0x3c0, i);
    }
}

void resetmode13()
{
    vga::setmode(0x0d);         // "mov ax,13": decimal, so mode 0Dh
    setac16();
    outp(0x3c0, 0x11);
    outp(0x3c0, 255);
    outp(0x3c0, 32);
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, 0);
}

void init_stars()
{
    vga::setmode(0x0d);
    setac16();
    outp(0x3c0, 0x20);
    outp(0x3d4, 9);
    outp(0x3d5, vga::in(0x3d5) & 127);
    outpw(0x3c4, 0x0f02);       // (map mask as left by the mode set)
    vga::fill(0, 0, 65536);

    clearsbu();
    for (int i = 0; i < 200; i++) rows1[i] = (uint16_t)(i * 40);

    for (int cx = STARS2, i = 0; cx; cx--, i++) {
        star[i].z = (uint8_t)(cx - 1);
        star[i].x = (int16_t)((random16() & 1023) - 512);
        star[i].y = (int16_t)((random16() & 1023) - 512);
        star[i].pad = 0;
    }
    for (int i = 0; i < 256; i++) {
        muldivy[i] = (int16_t)((uint16_t)((108u << 16) / (150 + 4 * i)) >> 1);
        muldivx[i] = (int16_t)((uint16_t)((144u << 16) / (150 + 4 * i)) >> 1);
    }
    starlimit = STARS;
    starpalfade = 0;
    startxtopen = -9999;
    startxtclose = 10000;
    for (int i = 0; i < 100; i++) {
        starfetch0();
        staradd();
    }
}

void copyrows(uint16_t page, const uint8_t *src, int sstep, uint16_t di)
{
    uint32_t base = (uint32_t)(page - 0xa000) * 16;
    for (int r = 0; r < 100; r++, src += sstep, di += 80)
        for (int i = 0; i < 40; i++) vga::wr((uint16_t)(base + di + i), src[i]);
}

void do_stars()
{
    for (;;) {
        if (starvram == 0xa000) {
            starvram = 0xa400;
            outpw(0x3d4, 0x000c);
        } else {
            starvram = 0xa000;
            outpw(0x3d4, 0x400c);
        }
        dis_waitb();

        if (starpalfade <= 32) {
            unsigned bl = (unsigned)(++starpalfade) << 3;
            if (bl > 255) bl = 255;
            auto sc = [&](int v) { return (uint8_t)((v * bl) >> 8); };
            outp(0x3c8, 0);
            static const uint8_t c4[12] = {0, 0, 0, 10, 20, 35, 20, 30, 45, 30, 40, 60};
            outp(0x3c9, 0); outp(0x3c9, 0); outp(0x3c9, 0);
            outp(0x3c9, sc(25 * 70 / 100)); outp(0x3c9, sc(31 * 70 / 100)); outp(0x3c9, sc(38 * 70 / 100));
            outp(0x3c9, sc(45 * 56 / 100)); outp(0x3c9, sc(58 * 56 / 100)); outp(0x3c9, sc(69 * 56 / 100));
            outp(0x3c9, sc(67 * 64 / 100)); outp(0x3c9, sc(84 * 64 / 100)); outp(0x3c9, sc(99 * 64 / 100));
            for (uint8_t v : c4) outp(0x3c9, v);
        }

        uint16_t f = ++starframe;
        if (f == 1200) { startxtp0 = 80; startxtopen = -256; startxtclose = 1500; }
        if (f == 3200) { startxtp0 = 101 * 80; startxtopen = -256; startxtclose = 1500; }
        if (f == 1500) starlimit = STARS2;

        risetext();

        starfetch0();
        if (starframe > 1200) staradd2();
        else if (starframe <= 900) staradd();

        const uint8_t *ds = esbuf;
        es_vram = starvram;
        es_is_vram = true;
        outpw(0x3c4, 0x0102);
        copyrows(starvram, ds, 40, 0);
        outpw(0x3c4, 0x0202);
        copyrows(starvram, ds + 4096, 40, 0);

        ds = fetch4ax((emmpage4 + 32) & 63);
        outpw(0x3c4, 0x0102);
        copyrows(starvram, ds + 99 * 40, -40, 200 * 40);
        outpw(0x3c4, 0x0202);
        copyrows(starvram, ds + 99 * 40 + 4096, -40, 200 * 40);

        if (dis_exit()) return;
    }
}

} // namespace

void part_ddstars()
{
    textpic = data_file("DD_TEXTPIC.BIN");
    if (!textpic) return;
    starvram = 0xa000;
    emmpage4 = starlimit = startxtp0 = starframe = 0;
    startxtopen = startxtclose = 0;
    starpalfade = 0;
    nostar1 = 200; nostar2 = 199;
    seed = 0;
    esbuf = fsbuf = ems;
    es_is_vram = false;

    dis_partstart();
    resetmode13();
    init_stars();
    do_stars();
}
