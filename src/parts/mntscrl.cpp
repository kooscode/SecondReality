// MNTSCRL.EXE - the mountain scroller with the reflected text (TRUG).
// Port of FOREST/READ2.PAS and FOREST/ROUTINES.ASM
#include "../core/dos.h"
#include "../core/parts.h"

namespace {

const int veke = 2800;

uint8_t pal[769], fpal[769], tmppal[769];
uint8_t fbuf[640 * 31 + 1];
uint8_t font[237 * 31 + 1];
uint8_t hback[65536];
const uint8_t *posi[3];
int scp, frames;

void setrgb(int c, int r, int g, int b)
{
    outp(0x3c8, c);
    outp(0x3c9, r);
    outp(0x3c9, g);
    outp(0x3c9, b);
}

void waitr() { frames = dis_waitb(); }

// ROUTINES.ASM putrouts: for every font pixel a list of screen addresses
void putrouts(const uint8_t *pos)
{
    const uint16_t *si = (const uint16_t *)pos;
    for (int bx = 0; bx < 237 * 31; bx++) {
        uint16_t n = *si++;
        for (; n; n--) {
            uint16_t di = *si++;
            vga::wr(di, (uint8_t)(hback[di] + font[bx]));
        }
    }
}

void scr(int pos)
{
    putrouts(posi[pos]);
    if (pos == 2) {
        memmove(&font[0], &font[1], 237 * 31);
        for (int ff = 0; ff <= 30; ff++) font[ff * 237 + 236] = fbuf[ff * 640 + scp];
        if (scp < 639) scp++;
    }
}

int musplus_dx()
{
    return dis_musplus();
}

} // namespace

void part_mntscrl()
{
    uint32_t n;
    const uint8_t *hb = data_file("MNT_HBACK.BIN", &n);
    const uint8_t *o2 = data_file("O2.SCI");
    posi[0] = data_file("MNT_POS1.BIN");
    posi[1] = data_file("MNT_POS2.BIN");
    posi[2] = data_file("MNT_POS3.BIN");
    if (!hb || !o2 || !posi[0] || !posi[1] || !posi[2]) return;
    memset(hback, 0, sizeof hback);
    memcpy(hback, hb, n < sizeof hback ? n : sizeof hback);
    memset(font, 0, sizeof font);
    memset(fbuf, 0, sizeof fbuf);
    memset(pal, 0, sizeof pal); memset(fpal, 0, sizeof fpal); memset(tmppal, 0, sizeof tmppal);
    int x, y;

    vga::setmode(0x13);
    dis_version();

    for (x = 0; x <= 30; x++) memcpy(&fbuf[x * 640], o2 + x * 640 + 778, 640);
    for (x = 0; x <= (int)sizeof(fbuf) - 1; x++) if (fbuf[x] > 0) fbuf[x] += 128;
    memcpy(pal, hback + 10, 768);
    memmove(hback, hback + 778, 64000);
    for (x = 0; x <= 255; x++) setrgb(x, 0, 0, 0);
    for (int i = 0; i < 64000; i++) vga::wr(i, hback[i]);

    memcpy(tmppal, pal, 768);
    memset(tmppal, 0, 32 * 3);
    memset(tmppal + 128 * 3, 0, 32 * 3);
    memset(fpal, 0, 768);

    for (y = 0; y <= 63; y++) {
        waitr();
        for (x = 0; x <= 255; x++) setrgb(x, fpal[x * 3 + 0], fpal[x * 3 + 1], fpal[x * 3 + 2]);
        for (x = 0; x <= 255; x++)
            for (int c = 0; c < 3; c++)
                if (fpal[x * 3 + c] < tmppal[x * 3 + c]) fpal[x * 3 + c]++;
    }

    memcpy(tmppal, pal, 768);
    memcpy(fpal, pal, 768);
    memset(fpal, 0, 32 * 3);
    memset(fpal + 128 * 3, 0, 32 * 3);
    for (x = 0; x <= 255; x++) setrgb(x, fpal[x * 3 + 0], fpal[x * 3 + 1], fpal[x * 3 + 2]);

    for (x = 0; x <= 30; x++) memcpy(&font[x * 237 + 104], &fbuf[x * 640], 133);
    scp = 133;

    while (musplus_dx() < 0 && !dis_exit()) {}

    int sss = 0;
    for (y = 0; y <= 63 * 2; y++) {
        waitr();
        if (y & 1) {
            for (x = 0; x <= 176; x++) setrgb(x, fpal[x * 3 + 0], fpal[x * 3 + 1], fpal[x * 3 + 2]);
            for (x = 0; x <= 176; x++)
                for (int c = 0; c < 3; c++)
                    if (fpal[x * 3 + c] < tmppal[x * 3 + c]) fpal[x * 3 + c]++;
        }
        scr(sss);
        if (sss == 2) sss = 0;
        else sss++;
    }

    memset(tmppal, 0, 768);

    int frame = 0, quit = 0;
    bool fadeout = false;
    int fp = 0;
    do {
        waitr();
        if (musplus_dx() == -11) fadeout = true;
        if (fadeout) {
            if (fp == 64) quit = 1;
            else fp++;
            for (x = 0; x <= 255; x++) {
                setrgb(x, fpal[x * 3 + 0], fpal[x * 3 + 1], fpal[x * 3 + 2]);
                for (int c = 0; c < 3; c++)
                    if (fpal[x * 3 + c] > tmppal[x * 3 + c]) fpal[x * 3 + c]--;
            }
        }
        scr(sss);
        if (sss == 2) sss = 0;
        else sss++;
    } while (!(dis_exit() || frame == veke || quit == 1));
}
