#include <atomic>
#include <stdio.h>
#include "vga.h"
#include <string.h>

namespace vga {

uint8_t plane[4][65536];
uint8_t textmem[32768];

static uint8_t misc;
static uint8_t seqr[8], seqi;
static uint8_t crtr[32], crti;
static uint8_t gcr[16], gci;
static uint8_t acr[32], aci;
static bool    acflip;          // false = next 3C0 write is an index
static uint8_t dac[768];
static int     dacw, dacr;      // component indexes (entry*3+comp)
static uint8_t dactri[3];
static uint8_t pelmask = 0xFF;
static uint8_t latch[4];
static volatile bool retrace;
static uint8_t toggle;
static bool    textlock;
static int     mode_now = 3;
static RasterHook rhook;
static uint8_t font[4096];
static bool    font_loaded;
static unsigned blinkcnt;
static uint8_t l_start_hi, l_start_lo, l_pan, l_preset;
static bool latched;

void latch_display()
{
    l_start_hi = crtr[0x0C]; l_start_lo = crtr[0x0D]; l_pan = acr[0x13]; l_preset = crtr[8];
    latched = true;
}

uint8_t seq(int i) { return seqr[i & 7]; }
uint8_t crtc(int i) { return crtr[i & 31]; }
uint8_t gc(int i) { return gcr[i & 15]; }
uint8_t attr(int i) { return acr[i & 31]; }
const uint8_t *dacram() { return dac; }
int curmode() { return mode_now; }
void lock_textmodes(bool l) { textlock = l; }

// test builds: a one line summary of the emulated VGA state
void debug_state(char *buf, int n)
{
    unsigned tnz = 0, fsum = 0, dsum = 0, pnz = 0;
    for (int i = 0; i < 4000; i++) tnz += textmem[i] != 0;
    for (int i = 0; i < 4096; i++) fsum += font[i];
    for (int i = 0; i < 768; i++) dsum += dac[i];
    for (int i = 0; i < 65536; i++) pnz += plane[0][i] != 0;
    snprintf(buf, n, "mode=%d lock=%d font=%d/%u text_nz=%u dac=%u p0_nz=%u ac10=%02x seq1=%02x seq4=%02x cr9=%02x misc=%02x pel=%02x",
             mode_now, (int)textlock, (int)font_loaded, fsum, tnz, dsum, pnz, acr[0x10], seqr[1], seqr[4], crtr[9], misc, pelmask);
}
void set_raster_hook(RasterHook h) { rhook = h; }
// The retrace pulse is short (1.2 ms) and set from the timer thread, so a
// thread polling 3DA may never run while it is up (on the Xbox a higher
// priority thread spinning with SwitchToThread does not yield to it). Each
// retrace start is therefore counted, and the first 3DA read after one
// reports "in retrace" even if the pulse has already ended.
static std::atomic<unsigned> retrace_seq{0};
static thread_local unsigned retrace_seen;
void set_retrace(bool on)
{
    // The DIS timer interrupt polled 3DA every frame, which also resets the
    // attribute controller flip-flop; parts rely on that (TECHNO writes
    // 3C0 index/data pairs without reading 3DA first).
    if (on) { retrace_seq++; acflip = false; }
    retrace = on;
}

void setpal(int i, uint8_t r, uint8_t g, uint8_t b)
{
    i &= 255;
    dac[i * 3 + 0] = r & 63; dac[i * 3 + 1] = g & 63; dac[i * 3 + 2] = b & 63;
}
void setpalblock(int start, int count, const uint8_t *rgb)
{
    for (int i = 0; i < count; i++) setpal(start + i, rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
}
void getpal(int i, uint8_t *rgb) { memcpy(rgb, dac + (i & 255) * 3, 3); }

// ---------------------------------------------------------------- mode tables
struct ModeRegs { uint8_t misc, seq[5], crtc[25], gc[9], ac[21]; };

static const ModeRegs m03 = {0x67, {0x03, 0x00, 0x03, 0x00, 0x02},
    {0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
     0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
     0x0C, 0x00, 0x0F, 0x08, 0x00}};
static const ModeRegs m0d = {0x63, {0x03, 0x09, 0x0F, 0x00, 0x06},
    {0x2D, 0x27, 0x28, 0x90, 0x2B, 0x80, 0xBF, 0x1F, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x9C, 0x8E, 0x8F, 0x14, 0x00, 0x96, 0xB9, 0xE3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
     0x01, 0x00, 0x0F, 0x00, 0x00}};
static const ModeRegs m0e = {0x63, {0x03, 0x01, 0x0F, 0x00, 0x06},
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x9C, 0x8E, 0x8F, 0x28, 0x00, 0x96, 0xB9, 0xE3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
     0x01, 0x00, 0x0F, 0x00, 0x00}};
static const ModeRegs m12 = {0xE3, {0x03, 0x01, 0x0F, 0x00, 0x06},
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
     0x01, 0x00, 0x0F, 0x00, 0x00}};
static const ModeRegs m13 = {0x63, {0x03, 0x01, 0x0F, 0x00, 0x0E},
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
     0x41, 0x00, 0x0F, 0x00, 0x00}};

static void load_regs(const ModeRegs &m)
{
    misc = m.misc;
    memcpy(seqr, m.seq, 5);
    memset(crtr, 0, sizeof crtr);
    memcpy(crtr, m.crtc, 25);
    memcpy(gcr, m.gc, 9);
    memcpy(acr, m.ac, 21);
    acflip = false;
}

static void ega64_palette()
{
    for (int i = 0; i < 64; i++) {
        int r = ((i >> 2) & 1) * 42 + ((i >> 5) & 1) * 21;
        int g = ((i >> 1) & 1) * 42 + ((i >> 4) & 1) * 21;
        int b = ((i >> 0) & 1) * 42 + ((i >> 3) & 1) * 21;
        setpal(i, r, g, b);
    }
}

static void default256_palette()
{
    static const uint8_t ega16[16] = {0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63};
    for (int i = 0; i < 16; i++) {
        int e = ega16[i];
        int r = ((e >> 2) & 1) * 42 + ((e >> 5) & 1) * 21;
        int g = ((e >> 1) & 1) * 42 + ((e >> 4) & 1) * 21;
        int b = ((e >> 0) & 1) * 42 + ((e >> 3) & 1) * 21;
        setpal(i, r, g, b);
    }
    static const uint8_t gray[16] = {0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63};
    for (int i = 0; i < 16; i++) setpal(16 + i, gray[i], gray[i], gray[i]);
    static const uint8_t lv[3][3][5] = {
        {{0, 16, 31, 47, 63}, {31, 39, 47, 55, 63}, {45, 49, 54, 58, 63}},
        {{0, 7, 14, 21, 28}, {14, 17, 21, 24, 28}, {20, 22, 24, 26, 28}},
        {{0, 4, 8, 12, 16}, {8, 10, 12, 14, 16}, {11, 12, 13, 15, 16}}};
    static const uint8_t hue[24][3] = {
        {0, 0, 4}, {1, 0, 4}, {2, 0, 4}, {3, 0, 4}, {4, 0, 4}, {4, 0, 3}, {4, 0, 2}, {4, 0, 1},
        {4, 0, 0}, {4, 1, 0}, {4, 2, 0}, {4, 3, 0}, {4, 4, 0}, {3, 4, 0}, {2, 4, 0}, {1, 4, 0},
        {0, 4, 0}, {0, 4, 1}, {0, 4, 2}, {0, 4, 3}, {0, 4, 4}, {0, 3, 4}, {0, 2, 4}, {0, 1, 4}};
    int n = 32;
    for (int in = 0; in < 3; in++)
        for (int sa = 0; sa < 3; sa++)
            for (int h = 0; h < 24; h++, n++)
                setpal(n, lv[in][sa][hue[h][0]], lv[in][sa][hue[h][1]], lv[in][sa][hue[h][2]]);
    for (; n < 256; n++) setpal(n, 0, 0, 0);
}

void reset()
{
    memset(plane, 0, sizeof plane);
    memset(textmem, 0, sizeof textmem);
    memset(dac, 0, sizeof dac);
    pelmask = 0xFF;
    textlock = false;
    mode_now = -1;
    setmode(3);
}

void setmode(int mode)
{
    bool noclear = mode & 0x80;
    mode &= 0x7F;
    if (mode <= 4 && textlock) return;
    switch (mode) {
    case 0x0D: load_regs(m0d); ega64_palette(); break;
    case 0x0E: load_regs(m0e); ega64_palette(); break;
    case 0x12: load_regs(m12); ega64_palette(); break;
    case 0x13: load_regs(m13); default256_palette(); break;
    default:   load_regs(m03); ega64_palette(); mode = 3; break;
    }
    pelmask = 0xFF;
    mode_now = mode;
    if (!noclear) {
        memset(plane, 0, sizeof plane);
        if (mode == 3)
            for (int i = 0; i < 32768; i += 2) { textmem[i] = ' '; textmem[i + 1] = 7; }
    }
}

// ---------------------------------------------------------------- port I/O
void out(uint16_t port, uint8_t v)
{
    switch (port) {
    case 0x3C0:
        if (!acflip) aci = v & 0x3F;
        else if ((aci & 0x1F) < 21) acr[aci & 0x1F] = v;
        acflip = !acflip;
        break;
    case 0x3C2: misc = v; break;
    case 0x3C4: seqi = v & 7; break;
    case 0x3C5: seqr[seqi] = v; break;
    case 0x3C6: pelmask = v; break;
    case 0x3C7: dacr = (v & 255) * 3; break;
    case 0x3C8: dacw = (v & 255) * 3; break;
    case 0x3C9:
        // the DAC commits a colour once all three components have arrived
        dactri[dacw % 3] = v & 63;
        if (dacw % 3 == 2) memcpy(dac + dacw - 2, dactri, 3);
        dacw = (dacw + 1) % 768;
        break;
    case 0x3CE: gci = v & 15; break;
    case 0x3CF: gcr[gci] = v; break;
    case 0x3D4: crti = v & 31; break;
    case 0x3D5:
        if ((crtr[0x11] & 0x80) && crti <= 7) {
            if (crti == 7) crtr[7] = (crtr[7] & ~0x10) | (v & 0x10);
            break;
        }
        crtr[crti] = v;
        break;
    default: break;
    }
}

void outw(uint16_t port, uint16_t v)
{
    out(port, (uint8_t)v);
    out(port + 1, (uint8_t)(v >> 8));
}

uint8_t in(uint16_t port)
{
    switch (port) {
    case 0x3C1: return acr[aci & 0x1F];
    case 0x3C4: return seqi;
    case 0x3C5: return seqr[seqi];
    case 0x3C6: return pelmask;
    case 0x3C8: return (uint8_t)(dacw / 3);
    case 0x3C9: { uint8_t r = dac[dacr]; dacr = (dacr + 1) % 768; return r; }
    case 0x3CC: return misc;
    case 0x3CE: return gci;
    case 0x3CF: return gcr[gci];
    case 0x3D4: return crti;
    case 0x3D5: return crtr[crti];
    case 0x3DA: {
        acflip = false;
        toggle ^= 1;
        unsigned seq = retrace_seq.load();
        bool r = retrace || seq != retrace_seen;
        retrace_seen = seq;
        return (r ? 8 | 1 : 0) | toggle;
    }
    default: return 0xFF;
    }
}

// ---------------------------------------------------------------- memory
static inline uint8_t rotate(uint8_t v, int n) { n &= 7; return (uint8_t)((v >> n) | (v << ((8 - n) & 7))); }

static inline uint8_t alu(uint8_t v, uint8_t l)
{
    switch ((gcr[3] >> 3) & 3) {
    case 1: return v & l;
    case 2: return v | l;
    case 3: return v ^ l;
    default: return v;
    }
}

uint8_t rd(uint32_t off)
{
    off &= 0xFFFF;
    if (seqr[4] & 8) {
        uint32_t a = off & 0xFFFC;
        for (int p = 0; p < 4; p++) latch[p] = plane[p][a];
        return plane[off & 3][a];
    }
    for (int p = 0; p < 4; p++) latch[p] = plane[p][off];
    if (gcr[5] & 8) {  // read mode 1: colour compare
        uint8_t res = 0xFF;
        for (int p = 0; p < 4; p++) {
            if (!(gcr[7] & (1 << p))) continue;
            uint8_t want = (gcr[2] >> p) & 1 ? 0xFF : 0x00;
            res &= ~(latch[p] ^ want);
        }
        return res;
    }
    return latch[gcr[4] & 3];
}

void wr(uint32_t off, uint8_t v)
{
    off &= 0xFFFF;
    uint8_t mask = seqr[2];
    if (seqr[4] & 8) {
        int p = off & 3;
        if (mask & (1 << p)) plane[p][off & 0xFFFC] = v;
        return;
    }
    switch (gcr[5] & 3) {
    case 0: {
        uint8_t d = rotate(v, gcr[3] & 7);
        uint8_t bm = gcr[8];
        for (int p = 0; p < 4; p++) {
            if (!(mask & (1 << p))) continue;
            uint8_t x = (gcr[1] & (1 << p)) ? ((gcr[0] & (1 << p)) ? 0xFF : 0x00) : d;
            x = alu(x, latch[p]);
            plane[p][off] = (x & bm) | (latch[p] & ~bm);
        }
        break;
    }
    case 1:
        for (int p = 0; p < 4; p++)
            if (mask & (1 << p)) plane[p][off] = latch[p];
        break;
    case 2: {
        uint8_t bm = gcr[8];
        for (int p = 0; p < 4; p++) {
            if (!(mask & (1 << p))) continue;
            uint8_t x = (v & (1 << p)) ? 0xFF : 0x00;
            x = alu(x, latch[p]);
            plane[p][off] = (x & bm) | (latch[p] & ~bm);
        }
        break;
    }
    case 3: {
        uint8_t bm = rotate(v, gcr[3] & 7) & gcr[8];
        for (int p = 0; p < 4; p++) {
            if (!(mask & (1 << p))) continue;
            uint8_t x = (gcr[0] & (1 << p)) ? 0xFF : 0x00;
            x = alu(x, latch[p]);
            plane[p][off] = (x & bm) | (latch[p] & ~bm);
        }
        break;
    }
    }
}

void fill(uint32_t off, uint8_t v, uint32_t n)
{
    // Fast path for the common unchained "all defaults" case.
    if (!(seqr[4] & 8) && (gcr[5] & 3) == 0 && gcr[1] == 0 && (gcr[3] & 0x1F) == 0 && gcr[8] == 0xFF) {
        for (int p = 0; p < 4; p++) {
            if (!(seqr[2] & (1 << p))) continue;
            uint32_t o = off & 0xFFFF, left = n;
            while (left) {
                uint32_t run = 0x10000 - o;
                if (run > left) run = left;
                memset(&plane[p][o], v, run);
                left -= run; o = (o + run) & 0xFFFF;
            }
        }
        return;
    }
    for (uint32_t i = 0; i < n; i++) wr(off + i, v);
}

void copy_in(uint32_t off, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) wr(off + i, src[i]);
}

// ---------------------------------------------------------------- rendering
static inline uint32_t dac_rgb(int i)
{
    const uint8_t *c = dac + i * 3;
    uint32_t r = (c[0] << 2) | (c[0] >> 4), g = (c[1] << 2) | (c[1] >> 4), b = (c[2] << 2) | (c[2] >> 4);
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t memaddr(uint32_t ma, bool dword, bool word)
{
    if (dword) return (ma << 2) & 0xFFFF;
    if (word) {
        uint32_t hi = (crtr[0x17] & 0x20) ? (ma >> 15) & 1 : (ma >> 13) & 1;
        return ((ma << 1) | hi) & 0xFFFF;
    }
    return ma & 0xFFFF;
}

void set_font(const uint8_t *f) { memcpy(font, f, 4096); font_loaded = true; }

void render(Frame &f)
{
    blinkcnt++;
    int vdisp = crtr[0x12] | ((crtr[7] & 2) << 7) | ((crtr[7] & 0x40) << 3);
    vdisp++;
    if (vdisp > 480) vdisp = 480;
    if (vdisp < 1) vdisp = 1;
    f.w = 640;
    f.h = vdisp;

    for (int i = 0; i < 256; i++) f.pal[i] = dac_rgb(i);

    bool graphics = gcr[6] & 1;
    uint8_t line[1024];
    uint32_t rowaddr = 0;
    int rowscan = 0;
    bool dsub = false;
    bool first = true;

    for (int y = 0; y < vdisp; y++) {
        if (rhook) rhook(y);
        // registers are re-read every line so raster hooks can change them
        int hchars = crtr[1] + 1;
        int maxscan = crtr[9] & 0x1F;
        bool dscan = crtr[9] & 0x80;
        int linecomp = crtr[0x18] | ((crtr[7] & 0x10) << 4) | ((crtr[9] & 0x40) << 3);
        uint32_t offset = crtr[0x13] * 2;
        bool dword = crtr[0x14] & 0x40;
        bool word = !(crtr[0x17] & 0x40);
        if (first) {
            uint8_t hi = latched ? l_start_hi : crtr[0x0C], lo = latched ? l_start_lo : crtr[0x0D];
            uint8_t pr = latched ? l_preset : crtr[8];
            rowaddr = ((hi << 8) | lo) + ((pr >> 5) & 3);
            rowscan = pr & 0x1F;
            first = false;
        }
        bool split = false;
        if (y == linecomp) { rowaddr = 0; rowscan = 0; dsub = false; }
        if (y >= linecomp) split = true;
        int pan = (latched && !rhook ? l_pan : acr[0x13]) & 0x0F;
        if (split && (acr[0x10] & 0x20)) pan = 0;

        uint8_t *out = f.pix + y * 640;
        if (!graphics) {
            // ---- text mode
            int ch = maxscan + 1;
            for (int c = 0; c < hchars && c < 80; c++) {
                uint32_t a = ((rowaddr + c) * 2) & 0x7FFF;
                uint8_t chr = textmem[a], at = textmem[(a + 1) & 0x7FFF];
                uint8_t bits = (rowscan < 16) ? font[chr * 16 + rowscan] : 0;
                (void)ch;
                int fg = at & 15, bg = (at >> 4) & 15;
                if (acr[0x10] & 8) {
                    bg &= 7;
                    if ((at & 0x80) && (blinkcnt & 16)) bits = 0;
                }
                uint8_t fgi = acr[fg] & 0x3F, bgi = acr[bg] & 0x3F;
                if (acr[0x10] & 0x80) { fgi = (fgi & 15) | ((acr[0x14] & 3) << 4); bgi = (bgi & 15) | ((acr[0x14] & 3) << 4); }
                fgi |= (acr[0x14] & 0x0C) << 4; bgi |= (acr[0x14] & 0x0C) << 4;
                for (int b = 0; b < 8; b++) out[c * 8 + b] = ((bits << b) & 0x80 ? fgi : bgi) & pelmask;
            }
            for (int x = hchars * 8; x < 640; x++) out[x] = 0;
        } else if (acr[0x10] & 0x40) {
            // ---- 256 colour: each character clock yields 4 pixels, 2 dots each
            int npix = (hchars + 1) * 4;
            if (npix > 1000) npix = 1000;
            for (int c = 0; c <= hchars && c * 4 < npix; c++) {
                uint32_t m = memaddr(rowaddr + c, dword, word);
                line[c * 4 + 0] = plane[0][m];
                line[c * 4 + 1] = plane[1][m];
                line[c * 4 + 2] = plane[2][m];
                line[c * 4 + 3] = plane[3][m];
            }
            int sh = (pan >> 1) & 3;
            int w = hchars * 4;
            if (w > 320) w = 320;
            for (int x = 0; x < w; x++) {
                uint8_t v = line[x + sh] & pelmask;
                out[x * 2] = out[x * 2 + 1] = v;
            }
            for (int x = w * 2; x < 640; x++) out[x] = 0;
        } else {
            // ---- 16 colour planar
            bool half = seqr[1] & 8;
            int npix = (hchars + 1) * 8;
            if (npix > 1000) npix = 1000;
            for (int c = 0; c <= hchars && c * 8 < npix; c++) {
                uint32_t m = memaddr(rowaddr + c, dword, word);
                uint8_t p0 = plane[0][m], p1 = plane[1][m], p2 = plane[2][m], p3 = plane[3][m];
                for (int b = 0; b < 8; b++) {
                    int s = 7 - b;
                    uint8_t col = ((p0 >> s) & 1) | (((p1 >> s) & 1) << 1) | (((p2 >> s) & 1) << 2) | (((p3 >> s) & 1) << 3);
                    col &= acr[0x12] & 15;
                    uint8_t v = acr[col] & 0x3F;
                    if (acr[0x10] & 0x80) v = (v & 15) | ((acr[0x14] & 3) << 4);
                    v |= (acr[0x14] & 0x0C) << 4;
                    line[c * 8 + b] = v & pelmask;
                }
            }
            int sh = pan & 7;
            if (half) {
                int w = hchars * 8;
                if (w > 320) w = 320;
                for (int x = 0; x < w; x++) out[x * 2] = out[x * 2 + 1] = line[x + sh];
                for (int x = w * 2; x < 640; x++) out[x] = 0;
            } else {
                int w = hchars * 8;
                if (w > 640) w = 640;
                for (int x = 0; x < w; x++) out[x] = line[x + sh];
                for (int x = w; x < 640; x++) out[x] = 0;
            }
        }

        // advance the row counters
        if (dscan) {
            dsub = !dsub;
            if (dsub) continue;
        }
        if (++rowscan > maxscan) {
            rowscan = 0;
            rowaddr += offset;
        }
    }
}

} // namespace vga
