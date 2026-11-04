// U2E.EXE - vector part II, the city flight (PSI). Port of VISU/C/U2E.C
#include "../core/dos.h"
#include "../core/parts.h"
#include "../core/music.h"
#include "visu/visu.h"
#include <stdio.h>
#include <stdlib.h>

using namespace visu;

namespace {

const int MAXOBJ = 256;
struct s_co { object *o; int32_t dist; int index; int on; };

const char *scene = "U2E";
uint8_t *scene0, *scenem;
uint8_t palcopy[768];
int city, xit;
s_co co[MAXOBJ];
int conum;
object camobject;
rmatrix cam;
int order[MAXOBJ], ordernum;
const uint8_t *sp;
const uint8_t *scenelist[64];
int scl, sclp;
int fpal[768];

struct { volatile int frames; volatile int ready; } cl[4];
volatile int clr, clw;
int firstframe;
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

void memsetv(uint32_t off, uint8_t v, int n) { vga::fill(off & 0xFFFF, v, n); }

// letterbox frame on one page (offsets relative to the page)
void fadeset(uint32_t vram)
{
    int y;
    outp(0x3c4, 2);
    outp(0x3c5, 15);
    for (y = 0; y < 25; y++) {
        memsetv(vram + y * 80 + 0, 0, 17);
        memsetv(vram + y * 80 + 17, 252, 47);
        outp(0x3c4, 2); outp(0x3c5, 2 + 4 + 8);
        vga::wr((vram + y * 80 + 63) & 0xFFFF, 0);
        outp(0x3c4, 2); outp(0x3c5, 15);
        memsetv(vram + y * 80 + 252, 0, 16);     // (sic) +252, not +64
    }
    for (y = 25; y < 175; y++) {
        memsetv(vram + y * 80 + 0, 254, 17);
        memsetv(vram + y * 80 + 17, 253, 47);
        outp(0x3c4, 2); outp(0x3c5, 2 + 4 + 8);
        vga::wr((vram + y * 80 + 63) & 0xFFFF, 254);
        outp(0x3c4, 2); outp(0x3c5, 15);
        memsetv(vram + y * 80 + 64, 254, 16);
    }
    for (; y < 200; y++) {
        memsetv(vram + y * 80 + 0, 0, 17);
        memsetv(vram + y * 80 + 17, 252, 47);
        outp(0x3c4, 2); outp(0x3c5, 2 + 4 + 8);
        vga::wr((vram + y * 80 + 63) & 0xFFFF, 0);
        outp(0x3c4, 2); outp(0x3c5, 15);
        memsetv(vram + y * 80 + 64, 0, 16);
    }
}

void vid_clear255()
{
    outpw(0x3c4, 0x0f02);
    vga::fill(visu::vrambase & 0xFFFF, 0xff, 16000);
}

void outfpal(int start)
{
    outp(0x3c8, start);
    for (int a = 0; a < 768; a++) outp(0x3c9, fpal[a]);
}

int run()
{
    char tmpname[64];
    int a, b, c, d, e, f, g;
    int jellywas = 0;

    // int 10h AH=0Fh: the previous part left mode 13h set
    if (vga::curmode() > 3) jellywas = 1;

    dis_partstart();
    snprintf(tmpname, sizeof tmpname, "%s.00M", scene);
    uint32_t mlen;
    uint8_t *m = data_file(tmpname, &mlen);
    if (!m) return 1;
    scene0 = scenem = m;

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

    if (!jellywas) {
        vid_init(1);
        for (a = 0; a < 768; a++) fpal[a] = 0;
        for (a = 3; a < 64 * 3; a++) fpal[a] = 63;
    } else {
        outp(0x3c7, 0);
        for (a = 0; a < 768; a++) fpal[a] = inp(0x3c9);
    }

    for (b = 0; b < 33; b++) {
        for (a = 3; a < 768 - 6; a++) {
            fpal[a] += 2;
            if (fpal[a] > 63) fpal[a] = 63;
        }
        dis_waitb();
        outfpal(0);
    }
    for (b = 0; b < 16; b++) dis_waitb();

    fadeset(0x0000);
    dis_waitb();
    outp(0x3d4, 9);
    a = inp(0x3d5);
    a = (a & 0xf0) | 0x80;
    outp(0x3d5, a);
    dis_waitb();
    fadeset(0x4000);
    fadeset(0x8000);
    fadeset(0xc000);

    for (b = 0; b < 16; b++) dis_waitb();

    for (b = 0; b < 33; b++) {
        for (a = 3; a < 768 - 9; a++) {
            fpal[a] -= 2;
            if (fpal[a] < 0) fpal[a] = 0;
        }
        for (a = 768 - 9; a < 768 - 3; a++) {
            fpal[a] += 2;
            if (fpal[a] > 63) fpal[a] = 63;
        }
        dis_waitb();
        outfpal(0);
    }
    vid_init(11);
    uint8_t *cp = scenem + 16;
    memcpy(palcopy, cp, 768);
    cp = palcopy;
    cp[255 * 3 + 0] = 0; cp[255 * 3 + 1] = 0; cp[255 * 3 + 2] = 0;
    cp[252 * 3 + 0] = 0; cp[252 * 3 + 1] = 0; cp[252 * 3 + 2] = 0;
    cp[253 * 3 + 0] = 63; cp[253 * 3 + 1] = 63; cp[253 * 3 + 2] = 63;
    cp[254 * 3 + 0] = 63; cp[254 * 3 + 1] = 63; cp[254 * 3 + 2] = 63;
    wait_vr();          // vid_setpal waits for the retrace
    outp(0x3c8, 0);
    for (a = 0; a < 768; a++) outp(0x3c9, cp[a]);
    vid_window(0L, 319L, 25L, 174L, 512L, 9999999L);

    dis_setcopper(2, copper2);
    dis_partstart();
    xit = 0;

    while (!dis_exit()) {
        dis::checkkeys();
        sys::idle();
        if (music::pos().ord > 18) break;
    }

    coppercnt = 0;
    syncframe = 0;
    avgrepeat = 1;
    cl[0].ready = 0; cl[1].ready = 0; cl[2].ready = 0; cl[3].ready = 1;

    uint16_t fov = 0;
    while (!dis_exit() && !xit) {
        int onum;
        int32_t pflag, l;
        object *o;
        rmatrix *r;

        if (!firstframe) {
            deadlock = 0;
            while (cl[clw].ready) {
                if (deadlock > 16) break;
                sys::idle();
            }
            vid_setswitch(clw, -1);
            vid_clear255();
            vid_cameraangle(fov);
            ordernum = 0;
            for (a = 1; a < conum; a++)
                if (co[a].on) {
                    order[ordernum++] = a;
                    o = co[a].o;
                    memcpy(o->r, o->r0, sizeof(rmatrix));
                    calc_applyrmatrix(o->r, &cam);
                    b = o->pl[0][1];
                    if (o->name[1] == '_') co[a].dist = 1000000000L;
                    else co[a].dist = calc_singlez(b, o->v0, o->r);
                    if (currframe > 900 * 2 && currframe < 1100 * 2)
                        if (o->name[1] == 's' && o->name[2] == '0' && o->name[3] == '1') co[a].dist = 1L;
                }
            for (a = 0; a < ordernum; a++) {
                int32_t dis = co[c = order[a]].dist;
                for (b = a - 1; b >= 0 && dis > co[order[b]].dist; b--) order[b + 1] = order[b];
                order[b + 1] = c;
            }
            for (a = 0; a < ordernum; a++) vis_drawobject(co[order[a]].o);
        } else {
            syncframe = 0;
            firstframe = 0;
            coppercnt = 1;
        }
        a = (syncframe - currframe);
        repeat_ = a + 1;
        if (repeat_ < 0) repeat_ = 0;
        if (repeat_ == 0) cl[clw].frames = 1;
        else cl[clw].frames = repeat_;
        cl[clw].ready = 1;
        clw = (clw + 1) & 3;
        repeat_ = (repeat_ + 1) / 2;
        currframe += repeat_ * 2;
        while (repeat_-- && !xit) {
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

    outp(0x3c7, 0);
    for (a = 0; a < 768; a++) fpal[a] = inp(0x3c9);
    for (b = 0; b < 16; b++) {
        for (a = 0; a < 768; a++) {
            fpal[a] += 4;
            if (fpal[a] > 63) fpal[a] = 63;
        }
        dis_waitb();
        outfpal(255);       // (sic) starts at colour 255 and wraps
    }
    return 0;
}

} // namespace

void part_u2e()
{
    memset(co, 0, sizeof co);
    memset(&camobject, 0, sizeof camobject);
    memset(&cam, 0, sizeof cam);
    scl = sclp = 0;
    clr = 0; clw = 0;
    firstframe = 1;
    deadlock = 0; coppercnt = 0; syncframe = 0;
    currframe = 0;
    copperdelay = 16;
    for (auto &c : cl) { c.frames = 0; c.ready = 0; }
    run();
}
