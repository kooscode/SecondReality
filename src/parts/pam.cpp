// PAM.EXE - the Praxis explosion (WILDF/TRUG). Port of PAM/OUTTAA.C,
// PAM/ASMYT.ASM (RLE frame unpacker), PAM/COPPER.ASM, PAM/TWEAK.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include <atomic>

namespace {

std::atomic<int> frame_count;
const uint8_t *volatile cop_pal;
volatile int do_pal;

void copper2()
{
    frame_count++;
    if (do_pal) {
        outp(0x3c8, 0);
        const uint8_t *p = cop_pal;
        for (int i = 0; i < 768; i++) outp(0x3c9, p[i]);
        do_pal = 0;
    }
}
void copper3() {}

void init_copper()
{
    dis_setcopper(1, copper3);
    dis_setcopper(2, copper2);
    dis_setcopper(0, copper3);
}
void close_copper()
{
    dis_setcopper(0, nullptr);
    dis_setcopper(1, nullptr);
    dis_setcopper(2, nullptr);
}

void tw_opengraph()
{
    vga::setmode(0x13);
    outpw(0x3c4, 0x0604);   // chain4 off
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
    outpw(0x3d4, 0x0014);   // crtc long off
    outpw(0x3d4, 0xe317);   // crtc byte on
}

void tw_setpalette(const uint8_t *pal)
{
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, pal[i]);
}

void tw_setstart(int s)
{
    outpw(0x3d4, 0x0d | ((s & 255) << 8));
    outpw(0x3d4, 0x0c | (((s >> 8) & 255) << 8));
}

// ---- ASMYT.ASM
const uint8_t *anim;
uint32_t aseg;          // byte offset of the next frame (paragraph aligned)

const uint8_t ntaulu[4] = {0x1, 0x3, 0x7, 0xf};
const uint8_t taulu[4] = {0xf, 0xe, 0xc, 0x8};
const uint8_t ptaulu[4] = {1, 2, 4, 8};

inline void mask(int m) { outpw(0x3c4, 0x02 | (m << 8)); }

void ulosta_frame(uint32_t base)
{
    const uint8_t *s = anim + aseg;
    uint32_t si = 0;
    uint16_t di = 0;
    auto put = [&](uint16_t off, uint8_t c) { vga::wr((base + off) & 0xFFFF, c); };
    int8_t al = (int8_t)s[si++];
    for (;;) {
        if (al == 0) break;
        if (al < 0) {                    // skip
            di = (uint16_t)(di - al);
            al = (int8_t)s[si++];
            continue;
        }
        // outtaa
        al--;
        if (al == 0) {                   // single
            mask(ptaulu[di & 3]);
            put(di >> 2, s[si++]);
            di++;
        } else {
            int16_t ax = al;
            int bx = ((di & 3) + ax) >> 2;
            if (bx == 0) {               // samebyte
                uint8_t cl = taulu[di & 3];
                di = (uint16_t)(di + ax);
                cl &= ntaulu[di & 3];
                mask(cl);
                put(di >> 2, s[si++]);
                di++;
            } else if (bx == 1) {        // twobytes
                uint8_t cl = taulu[di & 3];
                di = (uint16_t)(di + ax);
                uint8_t ch = ntaulu[di & 3];
                uint8_t c = s[si];
                mask(cl);
                put((uint16_t)((di >> 2) - 1), c);
                mask(ch);
                put(di >> 2, c);
                di++;
                si++;
            } else {                     // long run
                uint16_t cx = (uint16_t)ax;
                mask(taulu[di & 3]);
                uint8_t c = s[si];
                uint16_t b = di >> 2;
                put(b, c);
                uint16_t n = (uint16_t)((((uint16_t)(di + cx)) >> 2) - b - 1);
                uint16_t d = (uint16_t)((di >> 2) + 1);
                mask(0x0f);
                for (uint16_t i = 0; i < n; i++) put((uint16_t)(d + i), c);
                di = (uint16_t)(di + cx);
                mask(ntaulu[di & 3]);
                put(di >> 2, s[si++]);
                di++;
            }
        }
        al = (int8_t)s[si++];
    }
    // over: the next frame starts at the next paragraph
    aseg += (si + 15) & ~15u;
}

uint8_t pal[768 * 64];

const int wfade[100] = {63, 32, 16, 8, 4, 2, 1, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        1, 2, 4, 6, 9, 14, 20, 28, 37, 46,
                        56, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

} // namespace

void part_pam()
{
    anim = data_file("PAM_ANI.BIN");
    const uint8_t *p0 = data_file("PAM_PAL.BIN");
    if (!anim || !p0) return;
    memcpy(pal, p0, 768);
    frame_count = 0; do_pal = 0; cop_pal = pal;
    aseg = 0;

    int a, f = 0, p = 0;
    dis_partstart();

    for (a = 1; a < 64; a++)
        for (int b = 0; b < 768; b++) pal[a * 768 + b] = (uint8_t)((63 * a + (64 - a) * pal[b]) / 64);

    while (dis_sync() < 10 && !dis_exit()) {}
    wait_vr();
    tw_setpalette(&pal[768 * 63]);
    tw_opengraph();
    init_copper();
    frame_count = 0;
    while (!dis_exit() && f++ < 45) {
        while (frame_count < 4) sys::idle();
        frame_count = 0;
        if (f <= 40) {
            if (p) {
                p = 0;
                ulosta_frame(0x4000);
                tw_setstart(16384);
            } else {
                p = 1;
                ulosta_frame(0x0000);
                tw_setstart(0);
            }
        }
        cop_pal = &pal[768 * wfade[f]];
        do_pal = 1;
    }
    close_copper();
}
