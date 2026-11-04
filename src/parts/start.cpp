// START.EXE - setup menu (MAIN/START.C, MAIN/MENU.C, MAIN/VIDTEXT.C)
#include "../core/dos.h"
#include "../core/parts.h"
#include <stdio.h>

namespace {

int col[] = {
    0x00, /* 0=default color */
    0x08, /* 1=switch name */
    0x09, /* 2=switch value */
    0x0a, /* 3=switch description */
    0x0b, /* 4=bottom line */
    0x0c, /* 5=bottom line key */
    0x0f, /* 6=DONOTDISTRIBUTE */
};

// ---- vidtext.c
uint8_t *vram = vga::textmem;
unsigned char vramcol = 0, vramor = 0, vramforce = 0, vramusecol;
unsigned vramp = 0, vramx = 0, vramy = 0, vramxm = 79, vramym = 24;

void prtc(int cc) { vram[vramp++] = cc; vram[vramp++] = vramusecol; vramx++; }

void gotoxy(int x, int y)
{
    if ((unsigned)y > vramym) {
        unsigned a;
        for (a = 0; a < vramym - 1; a++) {
            gotoxy(0, a);
            memmove(vram + vramp, vram + vramp + 160, vramxm * 2);
        }
        gotoxy(0, a);
        y = vramym - 1;
    }
    vramx = x; vramy = y;
    vramp = y * 160 + x * 2;
}

void prtt(const char *t)
{
    int a, b = 1000;
    while ((a = (unsigned char)*(t++)) && b--) {
        switch (a) {
        case 126:
            a = (*t++);
            if (!a) break;
            vramcol = col[a - '0'];
            if (vramforce) vramusecol = vramforce;
            else vramusecol = vramcol | vramor;
            break;
        case 10:
            gotoxy(0, vramy + 1);
            break;
        default:
            prtc(a);
        }
    }
}

void prtf(const char *s) { prtt(s); }

// ---- menu.c
int nilli;
int menuy = 0;
int *menux = &nilli;

int m_detail = 0;
int m_soundcard = 3;
int m_soundquality = 2;
int m_looping = 0;
int m_exit = 0;

void RGB(int r, int g, int b) { outp(0x3c9, r); outp(0x3c9, g); outp(0x3c9, b); }

void delay(int f)
{
    while (f--) {
        while (!(inp(0x3da) & 8)) {}
        while ((inp(0x3da) & 8)) {}
    }
}

void menucolorize(int index, int *x)
{
    static uint8_t *v = vram;
    if (menuy == index - 1) {
        v[4 * 2] = 175;
        v[80 * 2 - 4 * 2] = 174;
    }
    if (menuy == index) {
        menux = x;
        col[1] |= 0x10;
        col[2] |= 0x10;
    } else {
        col[1] &= ~0x10;
        col[2] &= ~0x10;
    }
    v = vram + vramp;
}

void mainmenu()
{
    if (menuy < 1) menuy = 1;
    if (menuy > 3) menuy = 3;

    if (m_detail < 0) m_detail = 1;
    if (m_detail > 1) m_detail = 0;
    if (m_soundcard < 0) m_soundcard = 3;
    if (m_soundcard > 3) m_soundcard = 0;
    if (m_soundquality < 0) m_soundquality = 2;
    if (m_soundquality > 2) m_soundquality = 0;
    if (m_looping < 0) m_looping = 1;
    if (m_looping > 1) m_looping = 0;
    gotoxy(0, 2);

    col[1] &= ~0x10; col[2] &= ~0x10; prtf(
"~0        ~3 This demonstration has been designed for fast machines, which         ~0  \n"
"~0        ~3 in this case means 33Mhz 486 computers. The demo also runs on         ~0  \n"
"~0        ~3 slower 386 computers, but some parts will naturally slow down.        ~0  \n"
"~0        ~3 In case you encounter difficulties running this demo, try             ~0  \n"
"~0        ~3 rebooting you computer with a clean boot (no TSRs etc.)               ~0  \n"
    "\n");
    menucolorize(1, &m_soundcard);
    switch (m_soundcard) {
    case 0: prtf(
"~0    ~1       Soundcard:~2 No sound                                               ~0  \n"
        ); break;
    case 1: prtf(
"~0    ~1       Soundcard:~2 SoundBlaster (mono)                                    ~0  \n"
        ); break;
    case 2: prtf(
"~0    ~1       Soundcard:~2 SoundBlaster Pro (stereo)                              ~0  \n"
        ); break;
    case 3: prtf(
"~0    ~1       Soundcard:~2 Gravis Ultrasound, 512K of memory (stereo)             ~0  \n"
        ); break;
    }
    col[1] &= ~0x10; col[2] &= ~0x10; prtf(
"~0        ~3 With Gravis Ultrasound or no music, the demo requires only            ~0  \n"
"~0        ~3 570,000 bytes of conventional memory. With SoundBlaster, an           ~0  \n"
"~0        ~3 additional 1MB of expanded memory (EMS) is required.                  ~0  \n"
    "\n");
    menucolorize(2, &m_soundquality);
    switch (m_soundquality) {
    case 0: prtf(
"~0    ~1       Sound quality:~2 Poor                                               ~0  \n"
        ); break;
    case 1: prtf(
"~0    ~1       Sound quality:~2 Standard                                           ~0  \n"
        ); break;
    case 2: prtf(
"~0    ~1       Sound quality:~2 High                                               ~0  \n"
        ); break;
    }
    col[1] &= ~0x10; col[2] &= ~0x10; prtf(
"~0        ~3 For SoundBlaster cards, the quality directly effects the mixing       ~0  \n"
"~0        ~3 rate. For the Gravis Ultrasound, the quality has no effect. If        ~0  \n"
"~0        ~3 the demo runs too slowly, try decreasing the sound quality.           ~0  \n"
    "\n");
    menucolorize(3, &m_looping);
    switch (m_looping) {
    case 0: prtf(
"~0    ~1       Demo looping:~2 Disabled                                            ~0  \n"
        ); break;
    case 1: prtf(
"~0    ~1       Demo looping:~2 Enabled (no end scroller shown)                     ~0  \n"
        ); break;
    }
    col[1] &= ~0x10; col[2] &= ~0x10; prtf(
"~0        ~3 If you wish the demo to run continuously, enable this option.         ~0  \n"
    "\n");
    menucolorize(4, &m_looping);
}

void menu()
{
    int y, a;

    delay(1);

    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, 0);

    delay(5);

    gotoxy(0, 0);
    for (y = 0; y < 50; y++) prtt("~0                                                                                ");
    gotoxy(0, 0);
    prtt("    ~4\xf9~5S~4\xf9~5E~4\xf9~5C~4\xf9~5O~4\xf9~5N~4\xf9~5D~4\xf9~5 ~4\xf9~5R~4\xf9~5E~4\xf9~5A~4\xf9~5L~4\xf9~5I~4\xf9~5T~4\xf9~5Y~4\xf9  "
         "~3(7-OCT-93)~0 ~4 Copyright (C) 1993 ~5Future Crew~4");
    gotoxy(0, 24);
    prtt("    ~5\x18~4/~5\x19~4 - change selection  ~5\x1b~4/~5\x1a~4 - change an entry  ~5<\xc4\xd9~4 - continue  ~5ESC~4 - exit ");

    mainmenu();

    delay(20);

    outp(0x3c8, 0);
    RGB(0, 0, 0);
    RGB(16, 16, 24); // bar
    RGB(13, 0, 0);
    RGB(43, 0, 0);
    RGB(23, 0, 0);
    RGB(53, 0, 0);
    RGB(33, 0, 0); // do not use
    RGB(63, 0, 0);
    outp(0x3c8, 56);
    RGB(0, 50, 63);
    RGB(0, 60, 63);
    RGB(25, 25, 30);
    RGB(0, 30, 60);
    RGB(0, 50, 60);
    RGB(63, 0, 0);
    RGB(63, 0, 0);
    RGB(63, 0, 0);

    for (;;) {
        mainmenu();
        a = getch();
        if (a == 13) break;
        if (a == 27) {
            m_exit = 1;
            break;
        }
        switch (a) {
        case sys::KEY_UP: menuy--; break;
        case sys::KEY_DOWN: menuy++; break;
        case sys::KEY_LEFT: (*menux)--; break;
        case sys::KEY_RIGHT: (*menux)++; break;
        }
    }

    gotoxy(0, 0);
    for (y = 0; y < 50; y++) prtt("~0                                                                                ");

    if (!m_soundcard) {
        gotoxy(0, 10);
        prtt("~4        You have selected the ~5no sound~4 option. Please note that even\n"
             "        though the music is not playing, the demo is still syncronized to\n"
             "        it. This means that there are delays (like in the beginning) that\n"
             "        feel unnecessarily long. There is no way to skip parts while you\n"
             "        watch the demo, so you can only wait.\n");
        prtt("~5\n        Press any key to continue...\n");
        getch();
        gotoxy(0, 0);
        for (y = 0; y < 50; y++) prtt("~0                                                                                ");
    }
}

} // namespace

void part_start()
{
    // static state of a freshly loaded EXE
    col[1] = 0x08; col[2] = 0x09;
    menuy = 0; menux = &nilli;
    m_detail = 0; m_soundcard = 3; m_soundquality = 2; m_looping = 0; m_exit = 0;
    vramp = vramx = vramy = 0;

    int16_t *ip = (int16_t *)dis_msgarea(3);
    if (*(int32_t *)ip == 0) {
        // the cursor is parked off screen by dosgotoxy(0,50)
        vga::outw(0x3d4, 0x200a);   // hide cursor
        menu();
        if (m_exit) {
            *ip = -1;
            return;
        }
        ip[1] = m_soundcard;
        ip[2] = m_soundquality;
        ip[3] = m_looping;
    }
}
