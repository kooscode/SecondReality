// U2A.EXE - vector part 0: the ship over the horizon (PSI). Port of VISU/C/U2A.C
#include "../core/dos.h"
#include "../core/parts.h"
#include "visu/visu.h"
#include "../core/music.h"
#include <stdio.h>
#include <stdlib.h>

using namespace visu;

namespace {

const int MAXOBJ = 256;

struct s_co {
    object *o;
    int32_t dist;
    int index;
    int on;
};

const char *scene;
uint8_t *scene0, *scenem;
int city, xit;
s_co co[MAXOBJ];
int conum;
object camobject;
rmatrix cam;
int order[MAXOBJ], ordernum;
const uint8_t *sp;
const uint8_t *scenelist[64];
int scl, sclp;
uint8_t bgplanar[64000];

struct { volatile int frames; volatile int ready; } cl[4];
volatile int clr, clw;
volatile int deadlock, coppercnt, syncframe;
int currframe;
volatile int copperdelay;
int repeat_, avgrepeat;

int32_t lsget(unsigned f)
{
    int32_t l = 0;
    switch (f & 3) {
    case 0: l = 0; break;
    case 1: l = (int8_t)*sp++; break;
    case 2: l = *sp++; l |= (int32_t)(int8_t)(*sp++) << 8; break;
    case 3: l = *sp++; l |= (int32_t)(*sp++) << 8; l |= (int32_t)(*sp++) << 16; l |= (int32_t)(int8_t)(*sp++) << 24; break;
    }
    return l;
}

void resetscene()
{
    sp = scenelist[sclp];
    for (int a = 0; a < conum; a++) {
        memset(co[a].o->r, 0, sizeof(rmatrix));
        memset(co[a].o->r0, 0, sizeof(rmatrix));
    }
    sclp++;
    if (sclp >= scl) sclp = 0;
}

void copper2()
{
    syncframe = syncframe + 1;
    for (int i = 0; i < 4; i++) if (cl[i].ready == 2) cl[i].ready = 0;
    deadlock = deadlock + 1;
    coppercnt = coppercnt + 1;
    if (copperdelay > 0) copperdelay = copperdelay - 1;
    if (copperdelay > 0) return;
    copperdelay = 0;
    if (cl[clr].ready) {
        cl[(clr - 1) & 3].ready = 2;
        vid_setswitch(-1, clr);
        copperdelay = cl[clr].frames;
        clr = (clr + 1) & 3;
    } else avgrepeat++;
}

void musicpos(int &ord, int &row)
{
    dis::checkkeys();
    sys::idle();
    music::Pos p = music::pos();
    ord = p.ord; row = p.row;
}

int run(const uint8_t *bg)
{
    char tmpname[64];
    int a, b, c, d, e, f, g;

    dis_partstart();
    snprintf(tmpname, sizeof tmpname, "%s.00M", scene);
    scene0 = scenem = data_file(tmpname);
    if (!scene0) return 1;

    // background: palette 192..255 and a planar copy for vid_clearbg
    memcpy(scene0 + 16 + 192 * 3, bg + 16, 64 * 3);
    {
        unsigned u = 0;
        for (int z = 0; z < 4; z++)
            for (int y = 0; y < 200; y++)
                for (int x = z; x < 320; x += 4) bgplanar[u++] = bg[16 + 768 + x + y * 320];
    }

    city = 0;
    if (scene0[15] == 'C') city = 1;
    if (scene0[15] == 'R') city = 2;
    int32_t ipo;
    memcpy(&ipo, scene0 + 4, 4);
    const int16_t *ip = (const int16_t *)(scene0 + ipo);
    conum = d = *ip++;
    for (f = -1, c = 1; c < d; c++) {
        e = *ip++;
        if (e > f) {
            f = e;
            snprintf(tmpname, sizeof tmpname, "%s.%03i", scene, e);
            co[c].o = vis_loadobject(tmpname);
            memset(co[c].o->r, 0, sizeof(rmatrix));
            memset(co[c].o->r0, 0, sizeof(rmatrix));
            co[c].index = e;
            co[c].on = 0;
        } else {
            for (g = 0; g < c; g++) if (co[g].index == e) break;
            co[c] = co[g];
            co[c].o = (object *)malloc(sizeof(object));
            *co[c].o = *co[g].o;
            co[c].o->r = (rmatrix *)calloc(1, sizeof(rmatrix));
            co[c].o->r0 = (rmatrix *)calloc(1, sizeof(rmatrix));
            co[c].on = 0;
        }
    }
    co[0].o = &camobject;
    camobject.r = &cam;
    camobject.r0 = &cam;

    snprintf(tmpname, sizeof tmpname, "%s.0AA", scene);
    const int16_t *lp = (const int16_t *)data_file(tmpname);
    if (!lp) return 1;
    while (*lp) {
        a = *lp;
        if (a == -1) break;
        snprintf(tmpname, sizeof tmpname, "%s.0%c%c", scene, a / 10 + 'A', a % 10 + 'A');
        scenelist[scl++] = data_file(tmpname);
        lp += 2;
    }

    resetscene();

    for (;;) {
        musicpos(a, b);
        if (a > 10 && b > 46) break;
        if (dis_exit()) return 0;
    }

    // The original relies on the display already being 320x200 unchained at
    // this point (vid_init(3) sets no mode), but the ALKU source leaves its
    // 320x400 / 704-wide scroller mode active. ALKU's horizon (rows 50..349)
    // and U2A's (rows 25..174 doubled) are the same picture, so switching the
    // CRTC to 320x200x4 pages here is invisible and gives the released look.
    {
        uint8_t pal[768];
        memcpy(pal, vga::dacram(), 768);
        vga::setmode(0x13 | 0x80);
        outpw(0x3c4, 0x0604);
        outpw(0x3d4, 0x0014);
        outpw(0x3d4, 0xe317);
        vga::setpalblock(0, 256, pal);
    }
    vid_init(3);
    const uint8_t *cp = scenem + 16;
    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, cp[a]);
    vid_window(0L, 319L, 25L, 174L, 512L, 9999999L);

    dis_setcopper(2, copper2);
    dis_partstart();
    xit = 0;
    coppercnt = 0;
    syncframe = 0;
    avgrepeat = 1;
    cl[0].ready = 0;
    cl[1].ready = 0;
    cl[2].ready = 0;
    cl[3].ready = 1;
    uint16_t fov = 0;
    while (!dis_exit() && !xit) {
        int onum;
        int32_t pflag, l;
        object *o;
        rmatrix *r;

        musicpos(a, b);
        if (a > 11 && b > 54) break;

        deadlock = 0;
        while (cl[clw].ready) {
            if (deadlock > 16) break;
            sys::idle();
        }
        // draw to free frame
        vid_setswitch(clw, -1);
        vid_clearbg(bgplanar);
        vid_cameraangle(fov);
        ordernum = 0;
        for (a = 1; a < conum; a++)
            if (co[a].on) {
                order[ordernum++] = a;
                o = co[a].o;
                memcpy(o->r, o->r0, sizeof(rmatrix));
                calc_applyrmatrix(o->r, &cam);
                b = o->pl[0][1];
                co[a].dist = calc_singlez(b, o->v0, o->r);
            }
        if (city == 1) {
            co[2].dist = 1000000000L;
            co[7].dist = 1000000000L;
            co[13].dist = 1000000000L;
        }
        if (city == 2) co[14].dist = 1000000000L;
        for (a = 0; a < ordernum; a++) {
            int32_t dis = co[c = order[a]].dist;
            for (b = a - 1; b >= 0 && dis > co[order[b]].dist; b--) order[b + 1] = order[b];
            order[b + 1] = c;
        }
        for (a = 0; a < ordernum; a++) vis_drawobject(co[order[a]].o);

        // how many frames late of schedule
        avgrepeat = (avgrepeat + (syncframe - currframe) + 1) / 2;
        repeat_ = avgrepeat;
        if (repeat_ < 1) repeat_ = 1;
        cl[clw].frames = repeat_;
        cl[clw].ready = 1;
        clw = (clw + 1) & 3;
        currframe += repeat_;
        while (repeat_-- && !xit) {
            // parse animation stream for 1 frame
            onum = 0;
            while (!xit) {
                a = *sp++;
                if (a == 0xff) {
                    a = *sp++;
                    if (a <= 0x7f) {
                        fov = (uint16_t)(a << 8);
                        break;
                    } else if (a == 0xff) {
                        resetscene();
                        xit = 1;
                        continue;
                    }
                }
                if ((a & 0xc0) == 0xc0) {
                    onum = ((a & 0x3f) << 4);
                    a = *sp++;
                }
                onum = (onum & 0xff0) | (a & 0xf);
                switch (a & 0xc0) {
                case 0x80: co[onum].on = 1; break;
                case 0x40: co[onum].on = 0; break;
                }
                if (onum >= conum) return 3;
                r = co[onum].o->r0;
                pflag = 0;
                switch (a & 0x30) {
                case 0x00: break;
                case 0x10: pflag |= *sp++; break;
                case 0x20: pflag |= sp[0]; pflag |= (int32_t)sp[1] << 8; sp += 2; break;
                case 0x30: pflag |= sp[0]; pflag |= (int32_t)sp[1] << 8; pflag |= (int32_t)sp[2] << 16; sp += 3; break;
                }
                l = lsget(pflag); r->x += l;
                l = lsget(pflag >> 2); r->y += l;
                l = lsget(pflag >> 4); r->z += l;
                if (pflag & 0x40) {
                    for (b = 0; b < 9; b++) if (pflag & (0x80 << b)) r->m[b] += lsget(2);
                } else {
                    for (b = 0; b < 9; b++) if (pflag & (0x80 << b)) r->m[b] += lsget(1);
                }
            }
        }
    }
    dis_setcopper(2, nullptr);

    for (int pg = 0; pg < 4; pg++) {
        vid_setswitch(pg, -1);
        vid_clearbg(bgplanar);
    }
    return 0;
}

} // namespace

void part_u2a()
{
    scene = "U2A";
    memset(co, 0, sizeof co);
    memset(&camobject, 0, sizeof camobject);
    memset(&cam, 0, sizeof cam);
    scl = sclp = 0;
    clr = 0; clw = 0;
    deadlock = 0; coppercnt = 0; syncframe = 0;
    currframe = 0;
    copperdelay = 16;
    for (auto &c : cl) { c.frames = 0; c.ready = 0; }
    const uint8_t *bg = data_file("U2A_BG.BIN");
    if (!bg) return;
    run(bg);
}
