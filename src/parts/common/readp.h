// READP.C (shared by several parts): rows of a run-length packed picture
// ("P" format: header, palette, then rows of {len, data}), and ASM.ASM lineblit.
#pragma once
#include "../../core/dos.h"

namespace readp_ns {

struct st_readp { int16_t magic, wid, hig, cols, add; };

inline void readp(uint8_t *dest, int row, const uint8_t *src)
{
    st_readp hdr;
    memcpy(&hdr, src, sizeof hdr);
    if (row == -1) {
        memcpy(dest, src + 16, hdr.cols * 3);
        return;
    }
    if (row >= hdr.hig) return;
    src += hdr.add * 16;
    while (row) {
        int16_t l;
        memcpy(&l, src, 2);
        src += (uint16_t)l;
        src += 2;
        row--;
    }
    int16_t bytes;
    memcpy(&bytes, src, 2);
    src += 2;
    const uint8_t *end = src + (uint16_t)bytes;
    while (src < end) {
        uint8_t al = *src++;
        if (al & 0x80) {
            uint8_t ah = al & 0x7f;
            al = *src++;
            do { *dest++ = al; } while (--ah);   // ah=0 means 256 (dec/jnz)
        } else *dest++ = al;
    }
}

// one 320 pixel row to 80 bytes of unchained memory, plane by plane
inline void lineblit(uint32_t dest, const uint8_t *src)
{
    for (int zpl = 0; zpl < 4; zpl++) {
        outpw(0x3c4, 0x02 | (0x100 << zpl));
        for (int z = 0; z < 80; z++) vga::wr((dest + z) & 0xFFFF, src[z * 4 + zpl]);
    }
}

// ASM.ASM setpalarea: note the original count (cx = cnt*2 + start)
inline void setpalarea(const uint8_t *pal, int start, int cnt)
{
    outp(0x3c8, start);
    int cx = (uint16_t)(cnt * 2 + start);
    for (int i = 0; i < cx; i++) outp(0x3c9, pal[i]);
}

} // namespace readp_ns
