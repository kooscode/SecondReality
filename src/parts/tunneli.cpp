// TUNNELI.EXE - the dot tunnel (TRUG). Port of TUNNELI/TUN10.PAS (Turbo Pascal)
#include "../core/dos.h"
#include "../core/parts.h"
#include <math.h>

namespace {

const int veke = 1060;   // frame count to exit

struct rengas { int16_t x, y; uint8_t c; };

rengas putki[103];
int16_t sinit[4097];
int16_t cosit[2049];
uint16_t sade[103];
uint16_t oldpos[7501];

// The Pascal globals pcalc, aa1, rows, aa2, sinit, cosit lie one after the
// other in the data segment. The drawing code indexes rows[] with values that
// run past either end; those reads land in aa1/aa2 (64000 = off screen) or in
// the neighbouring tables. This array reproduces that layout for rows[].
const int PCALC_W = 138 * 64 * 2;
int16_t seg_words[PCALC_W + 101 + 201 + 201 + 4097 + 2049];
int16_t *pcalc = seg_words;                         // [138][64] of {x,y}
const int ROWS0 = PCALC_W + 101;

inline uint16_t rows_at(int i)
{
    int k = ROWS0 + i;
    if (k < 0 || k >= (int)(sizeof seg_words / 2)) return 64000;
    return (uint16_t)seg_words[k];
}

void setrgb(int c, int r, int g, int b)
{
    outp(0x3c8, c);
    outp(0x3c9, r);
    outp(0x3c9, g);
    outp(0x3c9, b);
}

int frames;
void waitr()
{
    setrgb(0, 0, 0, 0);
    frames = dis_waitb();
}

} // namespace

void part_tunneli()
{
    uint32_t n1, n2;
    const uint8_t *pc = data_file("TUN_PCALC.BIN", &n1);
    const uint8_t *si = data_file("TUN_SINIT.BIN", &n2);
    if (!pc || !si) return;

    memset(putki, 0, sizeof putki);
    memset(oldpos, 0, sizeof oldpos);
    memcpy(pcalc, pc, n1 < PCALC_W * 2 ? n1 : PCALC_W * 2);
    int16_t *aa1 = seg_words + PCALC_W;
    int16_t *rows = aa1 + 101;
    int16_t *aa2 = rows + 201;
    for (int x = 0; x <= 100; x++) aa1[x] = (int16_t)64000;
    for (int x = 0; x <= 200; x++) aa2[x] = 0;
    memcpy(aa2, aa1, 101 * 2);
    for (int x = 0; x <= 200; x++) rows[x] = (int16_t)(x * 320);
    memcpy(sinit, si, 4097 * 2);
    memcpy(cosit, si + 4097 * 2, 2048 * 2);
    memcpy(aa2 + 201, sinit, sizeof sinit);
    memcpy(aa2 + 201 + 4097, cosit, sizeof cosit);

    vga::setmode(0x13);
    dis_version();

    for (int x = 0; x <= 64; x++) setrgb(64 + x, (64 - x), (64 - x), (64 - x));
    for (int x = 0; x <= 64; x++) setrgb(128 + x, (64 - x) * 3 / 4, (64 - x) * 3 / 4, (64 - x) * 3 / 4);
    setrgb(68, 0, 0, 0);
    setrgb(132, 0, 0, 0);
    setrgb(255, 0, 63, 0);

    uint16_t sx = 0, sy = 0;
    int16_t y = 0;
    uint16_t frame = 0;
    bool quit = false;

    waitr();
    for (int z = 0; z <= 100; z++) sade[z] = (uint16_t)(16384 / ((z * 7) + 95));

    do {
        waitr();
        uint16_t ry = 0;
        for (int x = 80; x >= 4; x--) {
            int16_t bx_ = (int16_t)(putki[x].x - putki[5].x);
            int16_t by = (int16_t)(putki[x].y - putki[5].y);
            uint8_t br = (uint8_t)sade[x];
            uint8_t bbc = (uint8_t)(putki[x].c + (int)(x / 1.3 + 0.5));
            const int16_t *p = pcalc + br * 128;
            if (bbc >= 64) {
                for (int cx = 0; cx < 64; cx++, p += 2) {
                    vga::wr(oldpos[ry >> 1], 0);
                    uint16_t di = (uint16_t)(p[0] + bx_);
                    if (di <= 319) {
                        int idx = (int16_t)(by + p[1]);
                        di = (uint16_t)(di + rows_at(idx));
                        vga::wr(di, bbc);
                    }
                    oldpos[ry >> 1] = di;
                    ry = (uint16_t)(ry + 2);
                }
            }
        }

        for (int sync = 1; sync <= frames; sync++) {
            putki[100].x = (int16_t)(cosit[sy & 2047] - sinit[(uint16_t)(sy * 3) & 4095] - cosit[sx & 2047]);
            putki[100].y = (int16_t)(sinit[(uint16_t)(sx * 2) & 4095] - cosit[sx & 2047] + sinit[y & 4095]);
            memmove(&putki[0], &putki[1], sizeof(rengas) * 100);
            sy++;
            sx++;
            if ((sy & 15) > 7) putki[99].c = 128;
            else putki[99].c = 64;
            if (frame >= veke - 102) putki[99].c = 0;
            if (frame == veke) quit = true;
            else frame++;
            if (dis_exit()) quit = true;
            if (quit) break;
        }
    } while (!quit);
}
