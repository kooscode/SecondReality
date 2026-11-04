// Port of the Demo Interrupt Server (DIS/DISINT.ASM) and the U2 loader's
// "copper simulator" timer interrupt (MAIN/COPPER.ASM).
#include "dis.h"
#include "sys.h"
#include "vga.h"
#include "music.h"
#include <string.h>

namespace dis {
volatile int copperframecount;
volatile uint32_t longframecount;
volatile int exitflag;
static volatile int passmuscode;
static volatile bool ctrlwasdown;
static uint8_t msgarea[4][64];
static void (*volatile copper_int[3])(void);
static volatile bool int0busy;

void init()
{
    memset(msgarea, 0, sizeof msgarea);
    for (auto &c : copper_int) c = nullptr;
}

bool aborted() { return ctrlwasdown; }
void clear_abort() { ctrlwasdown = false; }

static bool ctrldown()
{
    if (ctrlwasdown) return true;
    if (sys::ctrl_down()) { ctrlwasdown = true; return true; }
    return false;
}

void checkkeys()
{
    sys::poll_input();
    if (ctrldown()) { exitflag = 1; return; }
    int k = sys::peekkey();
    if (!k) return;
    // In the final demo the loader swallowed the keyboard (only ESC, mapped
    // to ctrl_down, got through). KEY_SKIP / KEY_FF are debug helpers.
    if (k == sys::KEY_FF) {
        music::Pos p = music::pos();
        music::gotosong(p.ord + 1, 0);
    }
    if (k == sys::KEY_SKIP) exitflag = 1;
    passmuscode = 1;
}

void part_begin()
{
    exitflag = 0;
    passmuscode = 0;
}

void part_end()
{
    for (auto &c : copper_int) c = nullptr;
}

// Retrace. Order as in copper_end: int1 (just before retrace), then the
// retrace itself (the display latches its state: we capture the frame), then
// int2 (in the retrace).
void frame_tick()
{
    if (copper_int[1]) copper_int[1]();
    vga::latch_display();               // start address / panning latch at retrace start
    vga::set_retrace(true);
    copperframecount = copperframecount + 1;
    longframecount = longframecount + 1;
    if (copper_int[2]) copper_int[2]();
    // palette (and memory) changes made during the retrace are visible in the
    // frame that follows it
    vga::Frame *f = sys::begin_frame();
    if (f) {
        vga::render(*f);
        sys::end_frame(f);
    }
}

// Display start (~scanline 25): copper_doit
void frame_top()
{
    vga::set_retrace(false);
    if (!int0busy) {
        int0busy = true;
        if (copper_int[0]) copper_int[0]();
        int0busy = false;
    }
    if (sys::ctrl_down()) ctrlwasdown = true;
}

} // namespace dis

using namespace dis;

int dis_version(void)
{
    passmuscode = 0;
    exitflag = 0;
    return 0x100;
}

int dis_indemo(void) { return 1; }
void dis_partstart(void) { dis_version(); }

int dis_waitb(void)
{
    checkkeys();
    int a = copperframecount;
    while (copperframecount == a) sys::idle();
    int r = copperframecount;
    copperframecount = 0;
    return r;
}

int dis_exit(void)
{
    checkkeys();
    return exitflag;
}

void *dis_msgarea(int n) { return msgarea[n & 3]; }

static void muscode(int &ax, int &bx, int &dx)
{
    checkkeys();
    sys::idle();
    music::Pos p = music::pos();
    ax = p.zinfo;
    bx = p.row;
    dx = -32;
    switch (p.zplus) {
    case 0: break;
    case 1:
    plus_coming:
        dx = bx - 64;
        if (dx < -32) dx = -32;
        break;
    case 2:
    plus_passed:
        dx = bx;
        if (dx >= 32) dx = -32;
        break;
    default:
        if (bx > 32) goto plus_coming;
        goto plus_passed;
    }
}

int dis_muscode(int code)
{
    (void)code;
    int ax, bx, dx;
    muscode(ax, bx, dx);
    return ax;
}

int dis_musplus(void)
{
    int ax, bx, dx;
    muscode(ax, bx, dx);
    return dx;
}

int dis_musrow(void)
{
    int ax, bx, dx;
    muscode(ax, bx, dx);
    return bx;
}

void dis_setcopper(int n, void (*routine)(void))
{
    if (n >= 0 && n <= 2) copper_int[n] = routine;
}

void dis_setmframe(int frame) { music::set_frame(frame); }
int dis_getmframe(void) { sys::idle(); return (int16_t)music::pos().frame; }

int dis_sync(void)
{
    static const uint16_t tab[][2] = {
        {0x0000, 0}, {0x0200, 1}, {0x0300, 2}, {0x032f, 3}, {0x042f, 4}, {0x052f, 5}, {0x062f, 6},
        {0x072f, 7}, {0x082f, 8}, {0x0900, 9}, {0x0d00, 10}, {0x3d00, 1}, {0x3f00, 2}, {0x4100, 3},
        {0x4200, 4}};
    sys::idle();
    music::Pos p = music::pos();
    uint16_t dx = (uint16_t)(((p.ord & 255) << 8) | (p.row & 255));
    int n = sizeof tab / sizeof tab[0];
    int i = 0;
    for (; i < n; i++)
        if (dx <= tab[i][0]) break;
    if (i == 0) return 0;
    return tab[i - 1][1];
}
