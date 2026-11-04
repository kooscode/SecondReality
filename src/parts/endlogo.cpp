// ENDLOGO.EXE - the closing Future Crew picture (PSI). Port of END/END.C, END/ASM.ASM
#include "../core/dos.h"
#include "../core/parts.h"
#include "common/readp.h"

using namespace readp_ns;

void part_endlogo()
{
    static uint8_t pal2[768], palette[768], rowbuf[640 + 256];
    const uint8_t *pic = data_file("END_PIC.BIN");
    if (!pic) return;
    memset(pal2, 0, sizeof pal2);
    memset(palette, 0, sizeof palette);
    int a, c, y;
    dis_partstart();
    inp(0x3da);         // the copper's retrace polling leaves the 3C0 flip-flop at index
    outp(0x3c0, 0x11);
    outp(0x3c0, 255);
    outp(0x3c0, 0x20);
    dis_waitb();
    outp(0x3c8, 0);
    for (a = 0; a < 768 - 3; a++) outp(0x3c9, 63);
    outpw(0x3d4, 0x000c);
    outpw(0x3d4, 0x000d);
    outp(0x3d4, 9);
    outp(0x3d5, vga::in(0x3d5) & ~0x80 & ~31);
    outp(0x3c0, 0x11);
    outp(0x3c0, 0);
    outp(0x3c0, 32);
    dis_waitb();
    outp(0x3c8, 0);
    for (a = 0; a < 768 - 3; a++) outp(0x3c9, 63);
    for (a = 0; a < 32; a++) dis_waitb();

    readp(palette, -1, pic);
    for (y = 0; y < 400; y++) {
        readp(rowbuf, y, pic);
        lineblit((uint32_t)y * 80U, rowbuf);
    }

    for (c = 0; c <= 128; c++) {
        for (a = 0; a < 768 - 3; a++) pal2[a] = (uint8_t)(((128 - c) * 63 + palette[a] * c) / 128);
        dis_waitb();
        setpalarea(pal2, 0, 255);
    }
    for (a = 0; a < 5000 && !dis_exit(); a++) {
        dis_waitb();
        if (dis_musplus() > -16) break;
    }
    for (c = 63; c >= 0; c--) {
        for (a = 0; a < 768 - 3; a++) pal2[a] = (uint8_t)((palette[a] * c) / 64);
        dis_waitb();
        setpalarea(pal2, 0, 255);
    }
}
