// 3DSINFLD.EXE - the 3D sinus field voxel landscape (PSI).
// Port of COMAN/MAIN.C, COMAN/ASM.ASM and the generated ray loop THELOOP.INC
#include "../core/dos.h"
#include "../core/parts.h"

struct ComanStep { int16_t j, sina, k, l; uint32_t h; int16_t dbl; };
extern const ComanStep coman_steps[];
extern const int coman_nsteps;
extern const int16_t coman_sin1024[1024];

namespace {

const int16_t *sin1024 = coman_sin1024;
uint8_t wave1[65536], wave2[65536];
uint8_t vbuf[65536];
uint8_t palette[768];

inline int16_t WV(const uint8_t *w, uint16_t o) { return (int16_t)(w[o] | (w[(uint16_t)(o + 1)] << 8)); }

// THELOOP.INC: casts one column of rays through the wave field, bottom up.
// Registers are emulated with their x86 widths since the original packs a
// 16.16 value into a 32 bit register with its halves swapped.
void theloop(uint16_t si, uint16_t di, uint16_t xs1, uint16_t ys1, uint16_t bp)
{
    uint16_t xs2 = (uint16_t)(xs1 * 2), ys2 = (uint16_t)(ys1 * 2);
    uint32_t eax = 0, ecx = 0xEC00FFFAu;
    const uint32_t ST3 = 0x1E000000u, ST1 = 0x0A000000u;   // wflip(3*2560), wflip(2560)
    auto ax = [&]() { return (int16_t)(uint16_t)eax; };
    auto add32 = [](uint32_t &r, uint32_t v, unsigned cin) -> unsigned {
        uint64_t s = (uint64_t)r + v + cin;
        r = (uint32_t)s;
        return (unsigned)(s >> 32);
    };
    auto adc_lo16 = [](uint32_t &r, uint16_t v, unsigned cin) -> unsigned {
        uint32_t s = (uint32_t)(uint16_t)r + v + cin;
        r = (r & 0xffff0000u) | (uint16_t)s;
        return s >> 16;
    };
    for (int n = 0; n < coman_nsteps; n++) {
        const ComanStep &st = coman_steps[n];
        si = (uint16_t)(si + (st.sina == 2 ? xs2 : xs1));
        int16_t bx = WV(wave1, si);
        di = (uint16_t)(di + (st.sina == 2 ? ys2 : ys1));
        bx = (int16_t)(bx + WV(wave2, di));
        bx = (int16_t)(bx + st.k);
        unsigned cf;
        if (!(ax() >= bx)) {
            uint8_t dl = (uint8_t)((uint16_t)(bx + st.l)) >> 1;
            for (;;) {
                // hit
                cf = add32(eax, st.h, 0); adc_lo16(eax, 0, cf);
                vbuf[bp] = dl;
                if (ax() >= bx) { cf = (uint16_t)ax() < (uint16_t)bx; goto seek1a; }
                cf = add32(eax, st.h, 0); adc_lo16(eax, 0, cf);
                vbuf[(uint16_t)(bp - 160)] = dl;
                if (ax() >= bx) { cf = (uint16_t)ax() < (uint16_t)bx; goto seek2a; }
                cf = add32(eax, st.h, 0); adc_lo16(eax, 0, cf);
                vbuf[(uint16_t)(bp - 320)] = dl;
                cf = add32(ecx, ST3, 0); adc_lo16(ecx, 0, cf);
                bp = (uint16_t)(bp - 160 * 3);
                if (ax() >= bx) goto seeko;
                continue;
            seek2a:
                bp = (uint16_t)(bp - 160);
                cf = add32(ecx, ST1, 0);
            seek1a:
                cf = add32(ecx, ST1, cf);
                adc_lo16(ecx, 0, cf);
                bp = (uint16_t)(bp - 160);
                break;
            }
        }
    seeko:
        cf = add32(eax, ecx, 0);
        if (st.dbl) cf = add32(eax, ecx, cf);
        adc_lo16(eax, 0xffff, cf);
    }
}

void docol(int xw, int yw, int xa, int ya, int b)
{
    theloop((uint16_t)(xw & ~1), (uint16_t)(yw & ~1), (uint16_t)(xa & ~1), (uint16_t)(ya & ~1), (uint16_t)b);
}

void docopy(uint16_t seg)
{
    uint16_t base = (uint16_t)((seg - 0xa000) * 16);
    outpw(0x3c4, 0x0f02);
    uint16_t di = (uint16_t)(base + 52 * 80);
    vga::fill(di, 0, 18 * 80);
    di = (uint16_t)(di + 18 * 80);
    uint16_t si = 60 * 160;
    for (int cx = 0; cx < 140; cx++, si = (uint16_t)(si + 160), di = (uint16_t)(di + 80)) {
        outpw(0x3c4, 0x0302);
        for (int z = 0; z < 80; z++) vga::wr((uint16_t)(di + z), vbuf[(uint16_t)(si + z)]);
        outpw(0x3c4, 0x0c02);
        for (int z = 0; z < 80; z++) vga::wr((uint16_t)(di + z), vbuf[(uint16_t)(si + z + 80)]);
    }
    memset(vbuf + 68 * 160, 0, 30 * 160);
}

void setpalarea(const uint8_t *p, int start, int cnt)
{
    outp(0x3c8, start);
    for (int i = 0; i < cnt * 3; i++) outp(0x3c9, p[i]);
}

void inittwk_c()
{
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, 0);
    outpw(0x3d4, 0x0014);
    outpw(0x3d4, 0xe317);
    outpw(0x3c4, 0x0604);
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
}

void doit()
{
    int startrise = 160, frepeat = 1;
    int a, x, y, xa, ya, xw, yw, r;
    int rot2 = 0, rot = 0, rsin, rcos, xwav = 0, ywav = 0;
    int rsin2, rcos2;
    int frame = 0;
    int flip = 0;
    dis_waitb();
    while (!dis_exit() && dis_musplus() < 0) {}
    while (!dis_exit() && frame < 4444) {
        a = dis_musplus();
        if (a > -8 && a < 0) break;
        while (frepeat--) {
            if (a > -30 && a < 0) {
                if (startrise < 160) startrise += 1;
            }
            if (frame < 400 && startrise > 0) {
                if (frame == 4) {
                    dis_waitb();
                    setpalarea(palette, 0, 256);
                }
                if (startrise > 0) startrise -= 1;
            }
        }
        rot2 += 4;
        rot += (sin1024[rot2 & 1023]) / 15;
        r = (int16_t)rot >> 3;
        rsin = sin1024[r & 1023];
        rcos = sin1024[(r + 256) & 1023];
        rsin2 = sin1024[(r + 177) & 1023];
        rcos2 = sin1024[(r + 177 + 256) & 1023];
        xw = xwav; yw = ywav;
        for (a = 0; a < 160; a++) {
            x = (a - 80);
            y = 160;
            xa = (int16_t)(((int32_t)x * rcos + (int32_t)y * rsin) / 256);
            ya = (int16_t)(((int32_t)y * rcos2 - (int32_t)x * rsin2) / 256);
            int b = (a & 1) * 80 + (a >> 1) + 199 * 160;
            docol(xw, yw, xa, ya, b);
            if (a == 80) {
                xwav = (int16_t)(xwav + xa * 4);
                ywav = (int16_t)(ywav + ya * 4);
            }
        }
        flip++;
        if (flip > 1) flip = 0;
        if (flip == 0) {
            outp(0x3d4, 0x0c);
            outp(0x3d5, 0x80);
            frame += (frepeat = dis_waitb());
            docopy((uint16_t)(0xa000 + 5 * startrise));
        } else {
            outp(0x3d4, 0x0c);
            outp(0x3d5, 0x00);
            frame += (frepeat = dis_waitb());
            docopy((uint16_t)(0xa800 + 5 * startrise));
        }
    }
}

} // namespace

void part_3dsinfld()
{
    uint32_t n1, n2;
    const uint8_t *w1 = data_file("COMAN_W1.BIN", &n1);
    const uint8_t *w2 = data_file("COMAN_W2.BIN", &n2);
    if (!w1 || !w2) return;
    memcpy(wave1, w1, n1 < 65536 ? n1 : 65536);
    memcpy(wave2, w2, n2 < 65536 ? n2 : 65536);
    memset(vbuf, 0, sizeof vbuf);
    memset(palette, 0, sizeof palette);
    int a, b;
    unsigned uc;
    dis_partstart();
    vga::setmode(0x13);
    for (a = 0; a < 256; a++) {
        uc = (223 - a * 22 / 26) * 3;
        b = (230 - a) / 4;
        b += sin1024[a * 4 & 1023] / 32;
        if (b < 0) b = 0;
        if (b > 63) b = 63;
        palette[(uc + 1) % 768] = b;
        b = (255 - a) / 3;
        if (b > 63) b = 63;
        palette[(uc + 2) % 768] = b;
        b = a - 220;
        if (b < 0) b = -b;
        if (b > 40) b = 40;
        b = 40 - b;
        palette[uc % 768] = b / 3;
    }
    for (a = 0; a < 768 - 16 * 3; a++) {
        b = palette[a];
        b = b * 9 / 6;
        if (b > 63) b = 63;
        palette[a] = b;
    }
    for (a = 0; a < 24; a++) {
        uc = (255 - a) * 3;
        b = a - 4;
        if (b < 0) b = 0;
        palette[uc + 0] = b / 2;
        palette[uc + 1] = 0;
        palette[uc + 2] = 0;
    }
    palette[0] = palette[1] = palette[2] = 0;
    // colours 240..255 came from past the end of the 16 byte combg[] array
    // (the background picture object was not linked in); not used on screen
    for (int x = (256 - 16) * 3; x < 768; x++) palette[x] = 0;
    dis_waitb();
    setpalarea(palette, 0, 256);
    inittwk_c();
    vga::fill(0, 0, 64000);
    dis_waitb();
    setpalarea(palette, 0, 256);
    doit();
}
