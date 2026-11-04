// RAYSCRL.EXE - the raytraced sword over water, with the reflected scroller (TRUG).
// Port of WATER/DEMO.PAS and WATER/ROUTINES.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include "../core/music.h"

namespace {

uint8_t tmppal[769], pal[769];
uint8_t font[400 * 35 + 1];
// fbuf and the scp variable that follows it in the Pascal data segment:
// the scroll shift copies one byte past fbuf, which is scp's low byte
struct { uint8_t fbuf[158 * 34 + 1]; uint16_t scp; } v;
uint8_t tausta[65536];
const uint8_t *wat[3];
int frames;

void setrgb(int c, int r, int g, int b) { outp(0x3c8, c); outp(0x3c9, r); outp(0x3c9, g); outp(0x3c9, b); }
void waitr() { frames = dis_waitb(); }

void putrouts1(const uint8_t *pos)
{
    const uint16_t *si = (const uint16_t *)pos;
    for (int bx = 0; bx < 158 * 34; bx++) {
        uint16_t n = *si++;
        for (; n; n--) {
            uint16_t di = *si++;
            uint8_t al = v.fbuf[bx];
            if (!al) al = tausta[di];
            vga::wr(di, al);
        }
    }
}

void scr(int pos) { putrouts1(wat[pos]); }

void musinfo(int &ord, int &row, int &plus)
{
    plus = dis_musplus();
    music::Pos p = music::pos();
    ord = p.ord; row = p.row;
}

} // namespace

void part_rayscrl()
{
    uint32_t n;
    const uint8_t *bkg = data_file("RAY_TAUSTA.BIN", &n);
    const uint8_t *miek = data_file("RAY_MIEKKA.BIN");
    wat[0] = data_file("RAY_WAT1.BIN");
    wat[1] = data_file("RAY_WAT2.BIN");
    wat[2] = data_file("RAY_WAT3.BIN");
    if (!bkg || !miek || !wat[0] || !wat[1] || !wat[2]) return;
    memset(tausta, 0, sizeof tausta);
    memcpy(tausta, bkg, n < sizeof tausta ? n : sizeof tausta);
    memset(&v, 0, sizeof v);
    memset(font, 0, sizeof font);
    int x, y, ord, row, plus, co, sss = 0;

    vga::setmode(0x13);
    dis_version();
    do { musinfo(ord, row, plus); } while (plus < 0 && !dis_exit());
    co = ord;

    memcpy(pal, miek + 10, 768);
    memcpy(font, miek + 778, 400 * 34);
    for (x = 0; x <= 255; x++) setrgb(x, 0, 0, 0);
    memmove(tausta, tausta + 778, 64000);
    for (int i = 0; i < 64000; i++) vga::wr(i, tausta[i]);

    memcpy(tmppal, pal, 768);
    memset(pal, 0, 768);
    for (y = 0; y <= 63 * 2; y++) {
        waitr();
        if (y & 1)
            for (x = 0; x <= 255; x++) {
                setrgb(x, pal[x * 3 + 0], pal[x * 3 + 1], pal[x * 3 + 2]);
                for (int pf = 0; pf <= 3; pf++)
                    if (pal[x * 3 + pf] < tmppal[x * 3 + pf]) pal[x * 3 + pf]++;
            }
        scr(sss);
        if (sss == 2) sss = 0;
        else sss++;
    }

    do { musinfo(ord, row, plus); } while ((ord == co || row < 16) && !dis_exit());

    sss = 0;
    v.scp = 0;
    int quit = 0, fp = 0;
    bool fadeout = false;
    memset(tmppal, 0, 768);
    do {
        waitr();
        if (dis_musplus() == -11) fadeout = true;
        if (fadeout) {
            if (fp == 64) quit = 1;
            else fp++;
            for (x = 0; x <= 255; x++) {
                setrgb(x, pal[x * 3 + 0], pal[x * 3 + 1], pal[x * 3 + 2]);
                for (int pf = 0; pf <= 3; pf++)
                    if (pal[x * 3 + pf] > tmppal[x * 3 + pf]) pal[x * 3 + pf]--;
            }
        }
        scr(sss);
        if (sss == 2) {
            sss = 0;
            memmove(&v.fbuf[0], &v.fbuf[1], sizeof v.fbuf - 1);
            v.fbuf[sizeof v.fbuf - 1] = (uint8_t)v.scp;
            for (x = 0; x <= 33; x++) v.fbuf[158 + x * 158] = font[x * 400 + v.scp];
            if (v.scp < 390) v.scp++;
        } else sss++;
    } while (!(dis_exit() || quit == 1));
}
