// Port of MAIN/U2.ASM: the Second Reality loader. Runs the setup menu, starts
// the music and executes the parts in order, then prints the end screen.
#include "dos.h"
#include "music.h"
#include "parts.h"
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>

// ------------------------------------------------------------ data files
struct Blob { uint8_t *p; uint32_t n; };
static std::map<std::string, Blob> cache;

uint8_t *data_file(const char *name, uint32_t *size)
{
    auto it = cache.find(name);
    if (it == cache.end()) {
        Blob b;
        b.p = sys::load_file(name, &b.n);
        if (!b.p) {
            sys::log("missing data file %s", name);
            b.n = 0;
        }
        it = cache.emplace(name, b).first;
    }
    if (size) *size = it->second.n;
    return it->second.p;
}

// ------------------------------------------------------------ loader
static int whattorun = 0x7f;  // &1=start, &2=logo..panic &4=mount..jplogo &8=vector, &16=scrollers, &0x80 hidden
static int looping;

void inittwk()
{
    outpw(0x3d4, 0x0009);       // 400 rows
    outpw(0x3d4, 0x0014);       // tweak
    outpw(0x3d4, 0xe317);
    outpw(0x3c4, 0x0604);
    outpw(0x3c4, 0x0f02);
    vga::fill(0, 0, 65536);
}

static void partexecute(void (*part)(), const char *name)
{
    if (dis::aborted()) return;
    sys::log("part %s", name);
    dis::part_begin();
    dis_partstart();
    part();
    dis::part_end();
    while (sys::peekkey()) {}          // flushkbd
}

static void restartmus(int module, int order)
{
    if (dis::aborted()) return;
    music::stop();
    if (order >= 0) music::play(module, order);
}

static void fademusic(int step)
{
    while (music::global_volume() > 0) {
        dis_waitb();
        music::set_global_volume(music::global_volume() - step);
    }
    music::set_global_volume(0);
}

static void printend()
{
    uint32_t n;
    uint8_t *p = data_file("U2END.BIN", &n);
    if (p) memcpy(vga::textmem, p, n < 4000 ? n : 4000);
}

int main(int argc, char **argv)
{
    sys::init(argc, argv);
    vga::reset();
    {
        uint32_t n;
        uint8_t *f = sys::load_file("FONT8X16.BIN", &n);
        if (f && n >= 4096) vga::set_font(f);
    }
    dis::init();

    // command line: "SECOND #" starts from the middle (host build only)
    static const int partsmask[] = {0x1f, 0x1f, 0x1e, 0x1c, 0x18, 0x10, 0, 0, 0x80, 0};
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--start") && i + 1 < argc) whattorun = partsmask[atoi(argv[++i]) % 10];
#ifdef S2R_START
    whattorun = partsmask[S2R_START % 10];      // test builds: start from a later group
#endif

    // the music is loaded from REALITY.FC: a table of offsets, then the modules
    {
        uint32_t n;
        uint8_t *fc = data_file("REALITY.FC", &n);
        if (fc) {
            for (int i = 0; i < 2; i++) {
                uint32_t base = fc[i * 4] | (fc[i * 4 + 1] << 8) | (fc[i * 4 + 2] << 16) | (fc[i * 4 + 3] << 24);
                uint32_t end = i == 0 ? (fc[4] | (fc[5] << 8) | (fc[6] << 16) | (fc[7] << 24)) : n;
                if (i == 0) {
                    // STARTMUS.C: module[50]=0x78 (initial tempo) for the first module
                    fc[base + 50] = 0x78;
                }
                music::load(i, fc + base, end - base);
            }
        }
    }

    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--musictest") && i + 1 < argc) {   // host test: play a module
            music::set_global_volume(64);
            music::play(atoi(argv[i + 1]), 0);
            for (;;) sys::idle();
        }
    sys::start_frame_clock();
    vga::setmode(3);

    // START.EXE: setup menu. Results in message area 3.
    {
        int16_t *ip = (int16_t *)dis_msgarea(3);
        memset(ip, 0, 64);
        dis::part_begin();
        part_start();
        dis::part_end();
        if (ip[0] == -1) {
            vga::setmode(3);
            sys::shutdown();
        }
        if (ip[1] == 0) music::set_global_volume(0);   // "no sound"
        else music::set_global_volume(64);
        looping = ip[3];
    }
    int gvol = music::global_volume();

    // start music
    if (whattorun & 1) restartmus(0, 0);
    else restartmus(1, 0);

    for (;;) {
        vga::lock_textmodes(true);
        if (whattorun & 1) {
            partexecute(part_alku, "ALKU");
            partexecute(part_u2a, "U2A");
            partexecute(part_pam, "PAM");
        }
        if (whattorun & 2) {
            if (!(whattorun & 1)) { vga::setmode(0x13); inittwk(); }
            partexecute(part_beglogo, "BEGLOGO");
            if (whattorun & 1) restartmus(1, 0);
            partexecute(part_glenz, "GLENZ");
            partexecute(part_tunneli, "TUNNELI");
            partexecute(part_techno, "TECHNO");
            partexecute(part_panicend, "PANICEND");
            if (!dis::aborted()) {
                // wait for the music: first until musplus goes negative, then until positive
                for (;;) { dis_waitb(); if (dis_exit()) goto skip5; if (dis_musplus() < 0) break; }
                for (;;) { dis_waitb(); if (dis_exit()) goto skip5; if (dis_musplus() > 0) break; }
            }
        skip5:;
        }
        if (whattorun & 4) {
            if (!(whattorun & 2)) restartmus(1, 42);
            partexecute(part_mntscrl, "MNTSCRL");
            partexecute(part_lnszoom, "LNS&ZOOM");
            partexecute(part_plzpart, "PLZPART");
            partexecute(part_minvball, "MINVBALL");
            partexecute(part_rayscrl, "RAYSCRL");
            partexecute(part_3dsinfld, "3DSINFLD");
            partexecute(part_jplogo, "JPLOGO");
        }
        if (whattorun & 8) {
            restartmus(0, 18);
            partexecute(part_u2e, "U2E");
        }
        if (whattorun & 0x80) {
            restartmus(0, 70);
            partexecute(part_ddstars, "DDSTARS");
        }
        if (whattorun & 16) {
            if (!(whattorun & 8)) {
                restartmus(0, 25);
                vga::setmode(0x13);
                inittwk();
            }
            partexecute(part_endlogo, "ENDLOGO");
            partexecute(part_cred, "CRED");
            if (!looping) partexecute(part_endscrl, "ENDSCRL");
        }
        whattorun = 0x7f;
        if (dis::aborted()) break;
        if (looping == 0 || looping == 2) break;
        fademusic(1);
        music::set_global_volume(gvol);
        restartmus(0, 0);
    }

    vga::lock_textmodes(false);
    fademusic(1);
    music::stop();
    vga::setmode(3);
    for (int i = 0; i < 20; i++) dis_waitb();
    printend();
    // DOS would return to the prompt with the end screen showing; here we hold it
    // until a button is pressed.
    dis::clear_abort();
    while (sys::peekkey()) {}
    for (;;) {
        dis_waitb();
        sys::poll_input();
        if (sys::peekkey() || sys::ctrl_down()) break;
    }
    sys::shutdown();
    return 0;
}
