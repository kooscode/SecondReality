// CRED.EXE - the credits screens (WILDFIRE). Port of CREDITS/MAIN.C and TWEAK.ASM
#include "../core/dos.h"
#include "../core/parts.h"

namespace {

const int FONAY = 32;
const int PICSIZE = 16784;      // 16 byte header, 768 byte palette, 160x100 pixels
uint8_t pics[21][PICSIZE];
const uint8_t *font;            // font[FONAY][1500]
int fonap[256], fonaw[256];

// TWEAK.ASM
void tw_opengraph()
{
    vga::setmode(0x13);
    outpw(0x3c4, 0x0604);
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
    outpw(0x3d4, 0x0014);
    outpw(0x3d4, 0xe317);
    outpw(0x3d4, 0x5013);       // 640 wide
    outpw(0x3d4, 0x0109);
    outpw(0x3d4, 0x6418);       // line compare
    outpw(0x3d4, 0x0f07);
    outp(0x3c0, 0x30);
    outp(0x3c0, 0x61);
}

void tw_clrscr()
{
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 0x4000);
}

void tw_putpixel(int x, int y, int color)
{
    outpw(0x3c4, 0x02 | (0x100 << (x & 3)));
    vga::wr((uint16_t)((x >> 2) + y * 80), (uint8_t)color);
}

void tw_setpalette(const uint8_t *pal)
{
    outp(0x3c8, 0);
    for (int i = 0; i < 0x300; i++) outp(0x3c9, pal[i]);
}

void tw_setstart(int start)
{
    outpw(0x3d4, 0x0d | ((start & 0xff) << 8));
    outpw(0x3d4, 0x0c | (start & 0xff00));
}

void tw_setsplit(int start)
{
    outpw(0x3d4, 0x18 | ((start & 0xff) << 8));
    outpw(0x3d4, 0x07 | ((((start >> 8) << 4) & 0x10) | 0x0f) << 8);
}

void prt(int x, int y, const char *txt)
{
    int x2w, x2, y2, y2w = y + FONAY, sx, d;
    while (*txt) {
        x2w = fonaw[(uint8_t)*txt] + x;
        sx = fonap[(uint8_t)*txt];
        for (x2 = x; x2 < x2w; x2++) {
            for (y2 = y; y2 < y2w; y2++) {
                d = (int8_t)font[(y2 - y) * 1500 + sx];
                tw_putpixel(x2, y2, d);
            }
            sx++;
        }
        x = x2 + 2;
        txt++;
    }
}

const char *prtc(int x, int y, const char *txt)
{
    int w = 0;
    const char *t = txt;
    while (*t) w += fonaw[(uint8_t)*t++] + 2;
    prt(x - w / 2, y, txt);
    return t + 1;
}

void pan(int yy)
{
    tw_setstart(160 * 200 + (yy / 4));
    outp(0x3c0, 0x33);
    outp(0x3c0, (yy & 3) << 1);
}

void screenin(uint8_t *picbase, const char *text)
{
    int a, x, y, yy, v;

    tw_setsplit(400);
    tw_clrscr();
    tw_setstart(160 * 200);
    dis_waitb();
    tw_setpalette(picbase + 16);
    const uint8_t *pic = picbase + 784;

    y = 16;
    while (*(text = prtc(160, y, text))) y += FONAY + 10;

    for (x = 0; x < 160; x++)
        for (y = 0; y < 100; y++) tw_putpixel(400 + x, 400 + y * 2, (uint8_t)(pic[y * 160 + x] + 16));

    for (y = 200 * 128; y > 0; y = (int)((int32_t)y * 12 / 13)) {
        dis_waitb();
        tw_setsplit(y / 128 + 200);
        yy = 320 - y / 80;
        pan(yy);
    }

    for (a = 0; a < 200 && !dis_exit(); a++) dis_waitb();

    for (y = 0, v = 0; y < 128 * 200; y = y + v, v += 15) {
        dis_waitb();
        tw_setsplit(y / 128 + 200);
        yy = 320 + y / 80;
        pan(yy);
    }
}

void init()
{
    int x, y, a, b;
    const char *fonaorder = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?!:,.\"()+-";
    memset(fonap, 0, sizeof fonap);
    memset(fonaw, 0, sizeof fonaw);
    for (x = 0; x < 1500 && *fonaorder;) {
        while (x < 1500) {
            for (y = 0; y < FONAY; y++) if (font[y * 1500 + x]) break;
            if (y != FONAY) break;
            x++;
        }
        b = x;
        while (x < 1500) {
            for (y = 0; y < FONAY; y++) if (font[y * 1500 + x]) break;
            if (y == FONAY) break;
            x++;
        }
        fonap[(uint8_t)*fonaorder] = b;
        fonaw[(uint8_t)*fonaorder] = x - b;
        fonaorder++;
    }
    fonap[32] = 1500 - 32;
    fonaw[32] = 8;

    for (int p = 0; p < 21; p++) {
        uint8_t *pic = pics[p];
        memmove(&pic[16 * 3 + 16], &pic[16], 768 - 16 * 3);
        for (a = 0; a < 10; a++) pic[a * 3 + 0 + 16] = pic[a * 3 + 1 + 16] = pic[a * 3 + 2 + 16] = (uint8_t)(7 * a);
    }
}

} // namespace

void part_cred()
{
    uint32_t n1, n2, nf;
    const uint8_t *p1 = data_file("CRED_PICS1.BIN", &n1);
    const uint8_t *p2 = data_file("CRED_PICS2.BIN", &n2);
    font = data_file("CRED_FONT.BIN", &nf);
    if (!p1 || !p2 || !font || n1 < 12u * PICSIZE || n2 < 9u * PICSIZE) return;
    memcpy(pics[0], p1, 12 * PICSIZE);
    memcpy(pics[12], p2, 9 * PICSIZE);

    // pic1..pic10b, pic11..pic18
    enum { P1, P2, P3, P4, P5, P5B, P6, P7, P8, P9, P10, P10B, P11, P12, P13, P14, P14B, P15, P16, P17, P18 };
    static const struct { int pic; const char *text; } screens[] = {
        {P1,   "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0"},
        {P2,   "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - PSI\0"},
        {P3,   "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0ANIMATION - TRUG\0"},
        {P4,   "\0GRAPHICS - PIXEL\0"},
        {P5,   "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P5B,  "\0MUSIC - PURPLE MOTION\0CODE - TRUG\0"},
        {P6,   "\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P7,   "\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P8,   "\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0"},
        {P9,   "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - TRUG\0RENDERING - TRUG\0"},
        {P10,  "GRAPHICS - PIXEL, SKAVEN\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P10B, "GRAPHICS - PIXEL, SKAVEN\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P11,  "\0MUSIC - PURPLE MOTION\0CODE - WILDFIRE\0"},
        {P12,  "\0MUSIC - PURPLE MOTION\0CODE - WILDFIRE\0"},
        {P13,  "\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P14,  "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - TRUG\0RENDERING - TRUG\0"},
        {P14B, "\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P15,  "GRAPHICS - MARVEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0"},
        {P16,  "\0MUSIC - SKAVEN\0CODE - PSI\0"},
        {P17,  "\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0"},
        {P18,  "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - WILDFIRE\0"},
    };

    dis_partstart();
    init();
    tw_opengraph();
    tw_setstart(160 * 200);
    for (auto &s : screens)
        if (!dis_exit()) screenin(pics[s.pic], s.text);
}
