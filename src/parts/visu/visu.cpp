// VISU 3D engine - see visu.h
#include "visu.h"
#include "../../core/dos.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <utility>

extern const int16_t visu_sintable[4096];
extern const uint16_t visu_avistan[256];
extern const uint16_t visu_afilldiv[512];

namespace visu {

// ======================================================================== ADATA
int32_t projclipx[2] = {0, 319};
int32_t projclipy[2] = {0, 199};
int32_t projclipz[2] = {256, 1000000000};
int32_t projmulx = 250, projmuly = 220, projaddx = 160, projaddy = 100;
int16_t projaspect = 256, projoversampleshr = 0;
uint16_t rows[480];
uint16_t rowlen;
uint32_t vrambase;
static uint8_t polydrw[2048 + 64];

static inline void setmask(int m) { vga::outw(0x3c4, (uint16_t)(0x02 | ((m & 0x0f) << 8))); }
static inline void vput(uint16_t di, uint8_t c) { vga::wr((vrambase + di) & 0xFFFF, c); }

// ======================================================================== VISU.C
static uint8_t *readfile(const char *name)
{
    uint8_t *p = data_file(name);
    if (!p) { sys::log("VISU: file %s not found", name); }
    return p;
}

object *vis_loadobject(const char *fname)
{
    uint8_t *d = readfile(fname);
    object *o = (object *)calloc(1, sizeof(object));
    o->flags = F_DEFAULT;
    o->r = (rmatrix *)calloc(1, sizeof(rmatrix));
    o->r0 = (rmatrix *)calloc(1, sizeof(rmatrix));
    o->plnum = 1;
    o->name = "";
    if (!d) return o;
    for (;;) {
        uint8_t *d0 = d;
        d += 8;
        auto gint = [&]() { int16_t v; memcpy(&v, d, 2); d += 2; return (int)v; };
        if (!memcmp(d0, "END ", 4)) break;
        else if (!memcmp(d0, "VERS", 4)) gint();
        else if (!memcmp(d0, "NAME", 4)) o->name = (const char *)d;
        else if (!memcmp(d0, "VERT", 4)) {
            o->vnum = gint();
            gint();
            o->v0 = (vlist *)d;
            o->v = (vlist *)calloc(o->vnum + 1, sizeof(vlist));
            o->pv = (pvlist *)calloc(o->vnum + 1, sizeof(pvlist));
        } else if (!memcmp(d0, "NORM", 4)) {
            o->nnum = gint();
            o->nnum1 = gint();
            o->n0 = (nlist *)d;
            o->n = (nlist *)calloc(o->nnum + 1, sizeof(vlist));
        } else if (!memcmp(d0, "POLY", 4)) {
            o->pd = d;
        } else if (!memcmp(d0, "ORD", 3)) {
            int a = d0[3], b = -1;
            if (a == '0') b = 0;
            else if (a == 'E') b = o->plnum++;
            if (b >= 0 && b < 16) o->pl[b] = (int16_t *)d;
        }
        int32_t l;
        memcpy(&l, d0 + 4, 4);
        d = d0 + l + 8;
    }
    return o;
}

void vis_drawobject(object *o)
{
    if (!(o->flags & F_VISIBLE)) return;
    calc_rotate(o->vnum, o->v, o->v0, o->r);
    if (o->flags & F_GOURAUD) calc_nrotate(o->nnum, o->n, o->n0, o->r);
    else calc_nrotate(o->nnum1, o->n, o->n0, o->r);
    o->vf = calc_project(o->vnum, o->pv, o->v);
    if (o->vf) return;
    int a = 0;
    int32_t al = 0x7fffffff;
    for (int b = 1; b < o->plnum; b++) {
        int c = o->pl[b][1];
        int32_t bl = (o->v + c)->z;
        if (bl < al) { al = bl; a = b; }
    }
    draw_polylist(o->pl[a], o->pd, o->v, o->pv, o->n, o->flags);
}

int calc_invrmatrix(rmatrix *r)
{
    double m[9], d[9], det;
    for (int a = 0; a < 9; a++) m[a] = ((double)r->m[a]) / UNIT;
#define determ(c1, c2, r1, r2) (m[c1 + r1 * 3] * m[c2 + r2 * 3] - m[c1 + r2 * 3] * m[c2 + r1 * 3])
    d[0] = determ(1, 2, 1, 2);
    d[1] = -determ(0, 2, 1, 2);
    d[2] = determ(0, 1, 1, 2);
    d[3] = -determ(1, 2, 0, 2);
    d[4] = determ(0, 2, 0, 2);
    d[5] = -determ(0, 1, 0, 2);
    d[6] = determ(1, 2, 0, 1);
    d[7] = -determ(0, 2, 0, 1);
    d[8] = determ(0, 1, 0, 1);
#undef determ
    det = m[0] * d[0] + m[1] * d[1] + m[2] * d[2];
    if (det > -0.0001 && det < 0.0001) return 0;
    for (int x = 0; x < 3; x++)
        for (int y = 0; y < 3; y++) m[y + x * 3] = d[x + y * 3] / det;
    for (int a = 0; a < 9; a++) r->m[a] = (int32_t)(m[a] * UNIT);
    return (int)(UNIT * det);
}

// ======================================================================== ACALC
static inline int16_t sinv(uint16_t a) { return visu_sintable[(a >> 4) & 4095]; }
static inline int16_t cosv(uint16_t a) { return visu_sintable[((a >> 4) + 1024) & 4095]; }

// dest = m1 * m2 (32 bit products, low dword only, as mulmacro)
static void mulmat(int32_t *out, const int32_t *a, const int32_t *b)
{
    int32_t r[9];
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            uint32_t s = (uint32_t)a[row * 3 + 0] * (uint32_t)b[0 + col];
            s += (uint32_t)a[row * 3 + 1] * (uint32_t)b[3 + col];
            s += (uint32_t)a[row * 3 + 2] * (uint32_t)b[6 + col];
            r[row * 3 + col] = (int32_t)s >> 14;
        }
    memcpy(out, r, sizeof r);
}

static void calcmatrixsep(int32_t *m3, uint16_t rx, uint16_t ry, uint16_t rz)
{
    int32_t rxs = sinv((uint16_t)-rx), rxc = cosv((uint16_t)-rx);
    int32_t rys = sinv((uint16_t)-ry), ryc = cosv((uint16_t)-ry);
    int32_t rzs = sinv((uint16_t)-rz), rzc = cosv((uint16_t)-rz);
    int32_t *X = m3, *Y = m3 + 9, *Z = m3 + 18;
    X[0] = UNIT; X[1] = 0; X[2] = 0; X[3] = 0; X[4] = rxc; X[5] = rxs; X[6] = 0; X[7] = -rxs; X[8] = rxc;
    Y[0] = ryc; Y[1] = 0; Y[2] = -rys; Y[3] = 0; Y[4] = UNIT; Y[5] = 0; Y[6] = rys; Y[7] = 0; Y[8] = ryc;
    Z[0] = rzc; Z[1] = rzs; Z[2] = 0; Z[3] = -rzs; Z[4] = rzc; Z[5] = 0; Z[6] = 0; Z[7] = 0; Z[8] = UNIT;
}

void calc_setrmatrix_ident(rmatrix *m)
{
    for (int i = 0; i < 9; i++) m->m[i] = (i % 4 == 0) ? UNIT : 0;
}

void calc_setrmatrix_rotyxz(rmatrix *m, uint16_t rx, uint16_t ry, uint16_t rz)
{
    int32_t t[27];
    calcmatrixsep(t, rx, ry, rz);
    mulmat(t + 9, t + 9, t + 0);   // Y*=X
    mulmat(t + 9, t + 9, t + 18);  // Y*=Z
    memcpy(m->m, t + 9, 36);
}

void calc_setrmatrix_rotxyz(rmatrix *m, uint16_t rx, uint16_t ry, uint16_t rz)
{
    int32_t t[27];
    calcmatrixsep(t, rx, ry, rz);
    mulmat(t + 0, t + 0, t + 9);
    mulmat(t + 0, t + 0, t + 18);
    memcpy(m->m, t + 0, 36);
}

void calc_setrmatrix_rotzyx(rmatrix *m, uint16_t rx, uint16_t ry, uint16_t rz)
{
    int32_t t[27];
    calcmatrixsep(t, rx, ry, rz);
    mulmat(t + 18, t + 18, t + 9);
    mulmat(t + 18, t + 18, t + 0);
    memcpy(m->m, t + 18, 36);
}

static inline int32_t dot64(int32_t a0, int32_t b0, int32_t a1, int32_t b1, int32_t a2, int32_t b2)
{
    int64_t s = (int64_t)a0 * b0 + (int64_t)a1 * b1 + (int64_t)a2 * b2;
    return (int32_t)(s >> 14);
}

// ds:si=matrix, source/destination xyz
static void rotatesingle(const rmatrix *m, const int32_t *src, int32_t *dst)
{
    int32_t x = dot64(src[0], m->m[0], src[1], m->m[1], src[2], m->m[2]);
    int32_t y = dot64(src[0], m->m[3], src[1], m->m[4], src[2], m->m[5]);
    int32_t z = dot64(src[0], m->m[6], src[1], m->m[7], src[2], m->m[8]);
    dst[0] = x; dst[1] = y; dst[2] = z;
}

void calc_mulrmatrix(rmatrix *dest, const rmatrix *source)
{
    mulmat(dest->m, dest->m, source->m);
    rotatesingle(source, &dest->x, &dest->x);
    dest->x += source->x; dest->y += source->y; dest->z += source->z;
}

void calc_applyrmatrix(rmatrix *dest, const rmatrix *apply)
{
    mulmat(dest->m, apply->m, dest->m);
    rotatesingle(apply, &dest->x, &dest->x);
    dest->x += apply->x; dest->y += apply->y; dest->z += apply->z;
}

void calc_rotate(int count, vlist *dest, const vlist *src, const rmatrix *m)
{
    // only the low words of the matrix are used (movsx word ptr)
    int32_t m0 = (int16_t)m->m[0], m1 = (int16_t)m->m[1], m2 = (int16_t)m->m[2];
    int32_t m3 = (int16_t)m->m[3], m4 = (int16_t)m->m[4], m5 = (int16_t)m->m[5];
    int32_t m6 = (int16_t)m->m[6], m7 = (int16_t)m->m[7], m8 = (int16_t)m->m[8];
    for (int i = 0; i < count; i++) {
        const vlist &s = src[i];
        vlist &d = dest[i];
        int32_t x = dot64(m0, s.x, m1, s.y, m2, s.z) + m->x;
        int32_t y = dot64(m3, s.x, m4, s.y, m5, s.z) + m->y;
        int32_t z = dot64(m6, s.x, m7, s.y, m8, s.z) + m->z;
        d.x = x; d.y = y; d.z = z;
        d.normal = s.normal;
    }
}

int32_t calc_singlez(int vertex, const vlist *v, const rmatrix *m)
{
    const vlist &s = v[vertex];
    return dot64(m->m[6], s.x, m->m[7], s.y, m->m[8], s.z) + m->z;
}

void calc_nrotate(int count, nlist *dest, const nlist *src, const rmatrix *m)
{
    for (int i = 0; i < count; i++) {
        const nlist &s = src[i];
        nlist &d = dest[i];
        int32_t x = (int32_t)s.x * (int16_t)m->m[0] + (int32_t)s.y * (int16_t)m->m[1] + (int32_t)s.z * (int16_t)m->m[2];
        int32_t y = (int32_t)s.x * (int16_t)m->m[3] + (int32_t)s.y * (int16_t)m->m[4] + (int32_t)s.z * (int16_t)m->m[5];
        int32_t z = (int32_t)s.x * (int16_t)m->m[6] + (int32_t)s.y * (int16_t)m->m[7] + (int32_t)s.z * (int16_t)m->m[8];
        d.x = (int16_t)(x >> 14);
        d.y = (int16_t)(y >> 14);
        d.z = (int16_t)(z >> 14);
    }
}

static inline int32_t idiv64(int64_t a, int32_t b)
{
    if (!b) return 0;
    return (int32_t)(a / b);
}

int calc_project(int count, pvlist *dest, const vlist *src)
{
    int and_ = 0xffff;
    for (int i = 0; i < count; i++) {
        int32_t x = src[i].x, y = src[i].y, z = src[i].z;
        int bp = 0;
        if (z < projclipz[0]) { bp |= VF_NEAR; z = projclipz[0]; }
        else if (z > projclipz[1]) bp |= VF_FAR;
        int32_t py = idiv64((int64_t)y * projmuly, z) + projaddy;
        if (py > projclipy[1]) bp |= VF_DOWN;
        if (py < projclipy[0]) bp |= VF_UP;
        dest[i].y = (int16_t)py;
        int32_t px = idiv64((int64_t)projmulx * x, z) + projaddx;
        if (px > projclipx[1]) bp |= VF_RIGHT;
        if (px < projclipx[0]) bp |= VF_LEFT;
        dest[i].x = (int16_t)px;
        dest[i].vf = (int16_t)bp;
        and_ &= bp;
    }
    return and_;
}

// ======================================================================== AVID
void vid_cameraangle(uint16_t a)
{
    uint32_t half = (uint32_t)(projclipx[1] - projaddx);
    uint16_t bx = a >> 1;
    if (bx < 8 * 64) bx = 8 * 64;
    if (bx >= 16384) bx = 16383;
    uint32_t t = visu_avistan[bx >> 6];
    projmulx = (int32_t)(((uint64_t)half * t) >> 8);
    projmuly = (int32_t)(((uint64_t)(uint32_t)projmulx * (uint16_t)projaspect) >> 8);
}

void vid_window(int32_t x1, int32_t x2, int32_t y1, int32_t y2, int32_t z1, int32_t z2)
{
    projclipx[0] = x1; projclipx[1] = x2; projaddx = (x1 + x2) >> 1;
    projclipy[0] = y1; projclipy[1] = y2; projaddy = (y1 + y2) >> 1;
    projclipz[0] = z1; projclipz[1] = z2;
}

static void setrows(int cx, uint16_t dx)
{
    rowlen = dx;
    uint16_t a = 0;
    for (int i = 0; i < cx; i++) { rows[i] = a; a += dx; }
}

static void tweak320x200()
{
    vga::setmode(0x13);
    outp(0x3c8, 0);
    for (int i = 0; i < 768; i++) outp(0x3c9, 0);
    outp(0x3c4, 4); outp(0x3c5, (vga::in(0x3c5) & ~0x08) | 0x04);
    outp(0x3ce, 5); outp(0x3cf, vga::in(0x3cf) & ~0x10);
    outp(0x3ce, 6); outp(0x3cf, vga::in(0x3cf) & ~0x02);
    // (the CR9 write in the original does nothing)
    outp(0x3d4, 0x14); outp(0x3d5, vga::in(0x3d5) & ~0x40);
    outp(0x3d4, 0x17); outp(0x3d5, vga::in(0x3d5) | 0x40);
    outpw(0x3c4, 0x0f02);
    vga::fill(vrambase & 0xFFFF, 0, 65536);
}

static void m1_vars()
{
    vrambase = 0;
    setrows(200, 320 / 4);
    projclipz[0] = 256; projclipz[1] = 1000000000;
    projclipx[0] = 0; projclipx[1] = 319;
    projclipy[0] = 0; projclipy[1] = 199;
    projmulx = 250; projmuly = 220; projaddx = 160; projaddy = 100;
    projoversampleshr = 0;
    projaspect = 225;
}

void vid_init(int mode)
{
    if (mode == 1) { vrambase = 0; tweak320x200(); m1_vars(); }
    else if (mode == 11 || mode == 3) m1_vars();   // m11_init / m1o_init: no mode set
}

void vid_deinit() { vga::setmode(3); }

void vid_setswitch(int write, int display)
{
    if (write != -1) vrambase = (uint32_t)(write << 10) * 16;
    if (display != -1) outpw(0x3d4, (uint16_t)(((display << 14) & 0xff00) | 0x0c));
}

void vid_clearbg(const uint8_t *bg)
{
    // m1_clear "bgcopy": four planes of 80x200 from a planar buffer
    for (int p = 0; p < 4; p++) {
        setmask(1 << p);
        for (int i = 0; i < 16000; i++) vput((uint16_t)i, bg[p * 16000 + i]);
    }
}

void vid_clear()
{
    outpw(0x3c4, 0x0f02);
    vga::fill(vrambase & 0xFFFF, 0, 16000);
}

// ======================================================================== ADRAW
static const int16_t newlight[3] = {12118, 10603, 3030};

static int normallight(const nlist *n)
{
    int32_t s = (int32_t)n->x * newlight[0] + (int32_t)n->y * newlight[1] + (int32_t)n->z * newlight[2];
    int16_t ax = (int16_t)(s >> 16);
    ax >>= (2 * 14 - 7 - 16);
    int a = ax + 128;
    if (a > 255) a = 255;
    if (a < 0) a = 0;
    return a;
}

static int calclight(int flags, const nlist *n)
{
    int ax = flags & F_SHADE32;
    if (ax) {
        int l = normallight(n);
        int dx = ax >> 10;
        ax = l >> (6 - dx);
    }
    if (ax <= 1) ax = 1;
    if (ax >= 30) ax = 30;
    return ax;
}

static bool hidden(const nlist *n, const vlist *v)   // checkculling: carry=1=hidden
{
    int64_t s = (int64_t)n->x * v->x + (int64_t)n->y * v->y + (int64_t)n->z * v->z;
    return s >= 0;
}

// polygon work buffers (POLY* layout as an indexed struct; element i at [i+1]
// so the "previous vertex" slot [-1] of the original exists)
const int MAXV = 96;
struct Poly {
    int16_t sides, color, flags, vxseg;
    int16_t X[MAXV + 1], Y[MAXV + 1];
    int VX[MAXV + 1];
    int16_t GR[MAXV + 1];
    int16_t &x(int i) { return X[i + 1]; }
    int16_t &y(int i) { return Y[i + 1]; }
    int &vx(int i) { return VX[i + 1]; }
    int16_t &gr(int i) { return GR[i + 1]; }
};
static Poly poly1, poly2;
static const vlist *clip_v;      // fs segment for z clipping

// ---------------------------------------------------------------- ADRAWCLP
static int clipcount, clipzero;
static int clipvisfl;            // never reset, as in the original
static bool clipgrd;

static inline int16_t mulshr14(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 14); }

// One screen edge. coordY selects the clipped coordinate, upper = visible when <= limit
static void clip_edge(Poly *s, Poly *d, bool coordY, int16_t lim, bool upper)
{
    auto C = [&](Poly *p, int i) -> int16_t & { return coordY ? p->y(i) : p->x(i); };
    auto O = [&](Poly *p, int i) -> int16_t & { return coordY ? p->x(i) : p->y(i); };
    auto vis = [&](int16_t c) { return upper ? c <= lim : c >= lim; };     // initial / visible-loop test
    auto hid = [&](int16_t c) { return upper ? c >= lim : c <= lim; };     // hidden-loop test (still hidden)

    clipzero = 0;
    d->sides = s->sides; d->color = s->color;
    clipcount = s->sides + 1;
    d->flags = s->flags; d->vxseg = s->vxseg;
    d->x(-1) = 0x7fff;
    int si = 0, bx = 0, di = 0;

    auto next = [&]() -> bool {   // NEWCLIPNEXT, false = list ended
        bx = si;
        int dx = clipcount - 1;
        if (dx < 1) return false;
        clipcount = dx;
        if (dx > 1) si++;
        else si = clipzero;
        return true;
    };
    auto copy = [&]() {           // NEWCLIPCOPY
        if (d->x(di - 1) == s->x(si) && d->y(di - 1) == s->y(si)) return;
        d->x(di) = s->x(si); d->y(di) = s->y(si); d->gr(di) = s->gr(si);
        di++;
    };
    auto calc = [&]() {           // NEWCLIPCALC (bx=last, si=current)
        int16_t dx = O(s, si), cx = C(s, si), ax = O(s, bx), bxv = C(s, bx);
        bool flip = false;
        if (!(cx < bxv)) { flip = true; std::swap(cx, bxv); std::swap(ax, dx); }
        int16_t base = dx;
        int16_t bp = (int16_t)(lim - cx);
        int16_t delta = (int16_t)(ax - dx);
        int32_t num = (int32_t)bp * 16384;
        int16_t len = (int16_t)(bxv - cx);
        int16_t mult = len ? (int16_t)(num / len) : 0;
        int16_t other = (int16_t)(mulshr14(delta, mult) + base);
        int16_t clipped = lim;
        if (O(d, di - 1) == other && C(d, di - 1) == clipped) return;
        O(d, di) = other; C(d, di) = clipped;
        if (clipgrd) {
            int from = si, to = bx;          // newclip_grd: BX->SI clipped at CX
            if (flip) std::swap(from, to);
            int16_t g = (int16_t)(s->gr(to) - s->gr(from));
            d->gr(di) = (int16_t)(mulshr14(g, mult) + s->gr(from));
        }
        di++;
    };

    if (vis(C(s, si))) goto visible;
hidden_:
    for (;;) {
        if (!next()) goto done;
        if (!hid(C(s, si))) break;
    }
    calc();
visible:
    for (;;) {
        copy();
        if (!next()) goto done;
        if (!vis(C(s, si))) break;
    }
    calc();
    goto hidden_;
done:
    d->sides = (int16_t)di;
}

static int16_t zclip(int bp, int bx, int32_t ecx, int16_t &ox, int16_t &oy)
{
    const vlist *V = clip_v;
    int32_t eax = V[bx].z, edx = V[bp].z;
    if (!(eax > edx)) { std::swap(eax, edx); std::swap(bx, bp); }
    int32_t esi = eax - edx, edi = eax - ecx;
    int32_t X = idiv64((int64_t)(V[bp].x - V[bx].x) * edi, esi) + V[bx].x;
    int32_t Y = idiv64((int64_t)(V[bp].y - V[bx].y) * edi, esi) + V[bx].y;
    int16_t f = 0;
    int32_t py = idiv64((int64_t)Y * projmuly, ecx) + projaddy;
    if (py > projclipy[1]) f |= VF_DOWN;
    if (py < projclipy[0]) f |= VF_UP;
    int32_t px = idiv64((int64_t)X * projmulx, ecx) + projaddx;
    if (px > projclipx[1]) f |= VF_RIGHT;
    if (px < projclipx[0]) f |= VF_LEFT;
    ox = (int16_t)px; oy = (int16_t)py;
    return f;
}

static int clipnear(Poly *s, Poly *d)
{
    clipzero = 0;
    int32_t lim = projclipz[0];
    d->sides = s->sides; d->color = s->color;
    clipcount = s->sides + 1;
    d->flags = s->flags; d->vxseg = s->vxseg;
    d->x(-1) = 0x7fff;
    int si = 0, di = 0, bp = 0;
    int bx = s->vx(0);
    const vlist *V = clip_v;

    auto getvx = [&]() -> bool {     // ZCLIPGETVX
        bp = bx;
        int dx = clipcount - 1;
        if (dx < 1) return false;
        clipcount = dx;
        if (dx > 1) si++;
        else si = clipzero;
        bx = s->vx(si);
        return true;
    };
    auto addvis = [&]() {            // ZCLIPADDVISVX
        int16_t dx = s->x(si), ax = s->y(si);
        if (d->x(di - 1) == dx && d->y(di - 1) == ax) return;
        d->x(di) = dx; d->y(di) = ax; d->gr(di) = s->gr(si);
        di++;
    };
    auto addvx = [&](int16_t dx, int16_t cx) {   // ZCLIPADDVX
        if (d->x(di - 1) == dx && d->y(di - 1) == cx) return;
        d->x(di) = dx; d->y(di) = cx; d->gr(di) = d->gr(di - 1);
        di++;
    };
    int16_t nx, ny;

    if (lim <= V[bx].z) goto vis;
hid:
    do { if (!getvx()) goto done; } while (lim >= V[bx].z);
    clipvisfl |= zclip(bp, bx, lim, nx, ny);
    addvx(nx, ny);
vis:
    for (;;) {
        addvis();
        if (!getvx()) goto done;
        if (!(lim <= V[bx].z)) break;
    }
    clipvisfl |= zclip(bp, bx, lim, nx, ny);
    addvx(nx, ny);
    goto hid;
done:
    d->sides = (int16_t)di;
    return clipvisfl;
}

// returns the clipped polygon (poly1 or poly2)
static Poly *newclip(Poly *si, Poly *di, int flags, int dx)
{
    clipgrd = flags & F_GOURAUD;
    int dl = dx & 0xff, dh = (dx >> 8) & 0xff;
    if (dh & VF_FAR) { si->sides = 0; return si; }
    if ((dl & VF_NEAR) && si->sides) {
        int al = clipnear(si, di);
        std::swap(si, di);
        dl |= al & 0xff;
    }
    if ((dl & VF_UP) && si->sides) { clip_edge(si, di, true, (int16_t)projclipy[0], false); std::swap(si, di); }
    if ((dl & VF_DOWN) && si->sides) { clip_edge(si, di, true, (int16_t)projclipy[1], true); std::swap(si, di); }
    if ((dl & VF_LEFT) && si->sides) { clip_edge(si, di, false, (int16_t)projclipx[0], false); std::swap(si, di); }
    if ((dl & VF_RIGHT) && si->sides) { clip_edge(si, di, false, (int16_t)projclipx[1], true); std::swap(si, di); }
    return si;
}

// ---------------------------------------------------------------- poly_nrm / poly_grd
static uint8_t polyoversample16, polyoversamplea, polyoversample;
static int16_t polycury, polylhig, polyrhig;

static inline void W16(int off, int v) { int16_t t = (int16_t)v; memcpy(polydrw + off, &t, 2); }
static inline void W32(int off, int32_t v) { memcpy(polydrw + off, &v, 4); }

static void sidecalc(Poly *p, int fr, int to, int16_t height, int &bp, bool grd)
{
    W16(bp - 2, 0x7777);
    if (grd) {
        int16_t cs = p->gr(fr);
        W16(bp + 0, cs);
        int16_t ca = height ? (int16_t)((int16_t)(p->gr(to) - cs) / height) : 0;
        W16(bp + 2, ca);
        bp += 4;
    }
    int32_t edx = (int32_t)p->x(fr) << polyoversample16;
    edx = (edx & 0xffff0000) | 32768;
    W32(bp + 0, edx);
    int32_t eax = (int32_t)p->x(to) << polyoversample16;
    eax = (eax & 0xffff0000) | 32768;
    eax -= edx;
    eax = height ? eax / height : 0;
    int cl = polycury & polyoversamplea;
    if (cl) {
        int32_t xs;
        memcpy(&xs, polydrw + bp, 4);
        while (cl--) xs -= eax;
        W32(bp, xs);
    }
    eax <<= polyoversample;
    W32(bp + 4, eax);
    bp += 8;
}

static void poly_fill(Poly *p, int start, bool grd)
{
    int n = p->sides;
    int omin = 0, omax = n - 1;
    int bx = 0;
    int16_t dx = p->y(0), mx = p->y(0);
    for (int i = 1; i < n; i++) {
        int16_t ax = p->y(i);
        if (ax > mx) mx = ax;
        if (ax < dx) { dx = ax; bx = i; }
    }
    polycury = dx;
    int bp = start;
    W16(bp, p->color);
    W16(bp + 2, dx >> polyoversample);
    bp += 6;
    if (dx == mx) { W16(bp - 2, 0xffff); return; }
    bp -= 2;
    int si = bx, di = bx;
    for (int guard = 0; guard < 1000; guard++) {
        W16(bp, 0); bp += 2;
        if (p->y(si) == polycury) {
            for (;;) {
                int b = si - 1;
                if (b < omin) b = omax;
                uint16_t d = (uint16_t)(p->y(b) - p->y(si));
                if ((uint16_t)p->y(b) < (uint16_t)p->y(si)) { W16(bp - 2, 0xffff); return; }
                if (!d) { si = b; continue; }
                polylhig = (int16_t)d;
                sidecalc(p, si, b, (int16_t)d, bp, grd);
                si = b;
                break;
            }
        }
        W16(bp, 0); bp += 2;
        if (p->y(di) == polycury) {
            for (;;) {
                int b = di + 1;
                if (b > omax) b = omin;
                uint16_t d = (uint16_t)(p->y(b) - p->y(di));
                if ((uint16_t)p->y(b) < (uint16_t)p->y(di)) { W16(bp - 2, 0xffff); return; }
                if (!d) { di = b; continue; }
                polyrhig = (int16_t)d;
                sidecalc(p, di, b, (int16_t)d, bp, grd);
                di = b;
                break;
            }
        }
        int16_t ax = polylhig < polyrhig ? polylhig : polyrhig;
        W16(bp, ax >> polyoversample);
        polylhig -= ax;
        polyrhig -= ax;
        bp += 2;
        polycury += ax;
        if (bp > 2000) { W16(bp - 2, 0xffff); return; }
    }
}

static fillroutine drawfill_routine = vid_drawfill;

void draw_setfillroutine(fillroutine r) { drawfill_routine = r ? r : vid_drawfill; }

void draw_polylist(const int16_t *l, const uint8_t *d, const vlist *v, const pvlist *pv, const nlist *n, int f)
{
    if (!(f & 1)) return;
    int cl = projoversampleshr;
    polyoversample = (uint8_t)cl;
    polyoversample16 = (uint8_t)(16 - cl);
    polyoversamplea = (uint8_t)((1 << cl) - 1);
    clip_v = v;
    for (const int16_t *e = l + 2; *e; e++) {
        const uint8_t *s = d + (uint16_t)*e;
        uint16_t cx = s[0] | (s[1] << 8);
        poly1.flags = (int16_t)((f | 0x0f00) & cx);
        poly1.sides = cx & 0xff;
        int16_t normal; memcpy(&normal, s + 4, 2);
        int16_t first; memcpy(&first, s + 6, 2);
        if (s[2] == 0xff && s[3] == 0xff) continue;
        const nlist *nn = n + normal;
        if (!(poly1.flags & F_2SIDE) && hidden(nn, v + first)) continue;
        if (!(poly1.flags & F_GOURAUD)) poly1.color = (int16_t)((poly1.color & 0xff00) | (uint8_t)(calclight(poly1.flags, nn) + s[2]));
        else poly1.color = (int16_t)((poly1.color & 0xff00) | s[2]);
        int dh = 0xff, dl = 0;
        int sides = poly1.sides;
        if (sides > 16) sides = 16;
        for (int k = 0; k < sides; k++) {
            int16_t vi; memcpy(&vi, s + 6 + 2 * k, 2);
            poly1.vx(k) = vi;
            int vf = pv[vi].vf & 0xff;
            dh &= vf;
            dl |= vf;
            poly1.x(k) = pv[vi].x;
            poly1.y(k) = pv[vi].y;
        }
        if (poly1.flags & F_GOURAUD) {
            for (int k = 0; k < sides; k++) {
                const nlist *gn = n + v[poly1.vx(k)].normal;
                int c = (calclight(poly1.flags, gn) + (uint8_t)poly1.color) & 0xff;
                poly1.gr(k) = (int16_t)(c << 8);
            }
        }
        if (dh) continue;
        Poly *p = &poly1;
        if (dl) {
            p = newclip(&poly1, &poly2, poly1.flags, (dh << 8) | dl);
            if (p->sides == 0) continue;
        }
        if (p->flags & F_GOURAUD) { W16(0, 1); poly_fill(p, 2, true); }
        else { W16(0, 0); poly_fill(p, 2, false); }
        drawfill_routine(polydrw);
    }
}

// ======================================================================== AVIDFILL
static const uint8_t leftside[4] = {0xf, 0xe, 0xc, 0x8};
static const uint8_t rightside[4] = {0x1, 0x3, 0x7, 0xf};
static const uint8_t middledot[4] = {0x1, 0x2, 0x4, 0x8};
static uint8_t vdfg_buf[640 + 16];

static inline int16_t R16(const uint8_t *p, int off) { int16_t v; memcpy(&v, p + off, 2); return v; }
static inline int32_t R32(const uint8_t *p, int off) { int32_t v; memcpy(&v, p + off, 4); return v; }

static void movs(uint16_t &di, const uint8_t *src, int n)
{
    for (int i = 0; i < n; i++) vput(di++, src[i]);
}

static void drawfill_nrm(const uint8_t *s)
{
    uint8_t color = (uint8_t)R16(s, 0);
    int16_t y = R16(s, 2);
    s += 4;
    uint16_t vdf_di = rows[y & 511];
    int32_t leftx = 0, lefta = 0, rightx = 0, righta = 0;
    for (;;) {
        int16_t ax = R16(s, 0); s += 2;
        if (ax < 0) return;
        if (ax) { leftx = R32(s, 0); lefta = R32(s, 4); s += 8; }
        ax = R16(s, 0); s += 2;
        if (ax < 0) return;
        if (ax) { rightx = R32(s, 0); righta = R32(s, 4); s += 8; }
        int16_t count = R16(s, 0);
        if (!count) return;
        s += 2;
        do {
            leftx += lefta;
            rightx += righta;
            int16_t dx = (int16_t)(leftx >> 16), a = (int16_t)(rightx >> 16);
            if (a != dx) {
                if (a > dx) std::swap(a, dx);
                dx--;
                int16_t bx = a >> 2;
                int16_t cx = (int16_t)((dx >> 2) - bx);
                uint16_t di = (uint16_t)(vdf_di + bx);
                uint8_t lm = leftside[a & 3], rm = rightside[dx & 3];
                if (cx == 0) {
                    setmask(lm & rm);
                    vput(di, color);
                } else {
                    setmask(lm);
                    vput(di, color);
                    di++;
                    cx--;
                    if (cx > 0) {
                        setmask(0x0f);
                        vga::fill((vrambase + di) & 0xFFFF, color, cx);
                        di += cx;
                    }
                    setmask(rm);
                    vput(di, color);
                }
            }
            vdf_di = (uint16_t)(vdf_di + rowlen);
        } while (--count);
    }
}

static void drawfill_grd(const uint8_t *s)
{
    int16_t y = R16(s, 2);
    s += 4;
    uint16_t vdf_di = rows[y & 511];
    int32_t leftx = 0, lefta = 0, rightx = 0, righta = 0;
    int16_t leftc = 0, leftca = 0, rightc = 0, rightca = 0;
    for (;;) {
        int16_t ax = R16(s, 0); s += 2;
        if (ax < 0) return;
        if (ax) { leftc = R16(s, 0); leftca = R16(s, 2); leftx = R32(s, 4); lefta = R32(s, 8); s += 12; }
        ax = R16(s, 0); s += 2;
        if (ax < 0) return;
        if (ax) { rightc = R16(s, 0); rightca = R16(s, 2); rightx = R32(s, 4); righta = R32(s, 8); s += 12; }
        int16_t count = R16(s, 0);
        if (!count) return;
        s += 2;
        do {
            leftc += leftca; rightc += rightca;
            leftx += lefta; rightx += righta;
            int16_t dx = (int16_t)(leftx >> 16), a = (int16_t)(rightx >> 16);
            int16_t bx = leftc, cx = rightc;
            uint16_t di = vdf_di;
            if (a != dx) {
                if (!(a < dx)) { std::swap(bx, cx); std::swap(a, dx); }
                dx--;
                int16_t color1 = cx, color2 = bx, left = a, right = dx;
                if (!(right & 1)) {
                    setmask(middledot[right & 3]);
                    vput((uint16_t)(((uint16_t)right >> 2) + di), (uint8_t)(color2 >> 8));
                }
                if (left & 1) {
                    setmask(middledot[left & 3]);
                    vput((uint16_t)(((uint16_t)left >> 2) + di), (uint8_t)(color1 >> 8));
                }
                uint16_t lefts = (uint16_t)(left + 1) >> 1;
                uint16_t dxb = (uint16_t)(right - 1) >> 1;
                di = (uint16_t)(di + (lefts >> 1));
                int16_t blocks = (int16_t)(dxb - lefts + 1);
                if (blocks > 0) {
                    int16_t diff = (int16_t)(color1 - color2);
                    int16_t add;
                    if (diff >= 0) add = (int16_t)(((uint32_t)(uint16_t)diff * visu_afilldiv[blocks & 511]) >> 16);
                    else add = (int16_t)-(int16_t)(((uint32_t)(uint16_t)(-diff) * visu_afilldiv[blocks & 511]) >> 16);
                    int mode = (add >= 128 || add <= -128) ? ((add >= 400 || add <= -400) ? 1 : 2) : 4;
                    if (mode == 4 && blocks < 4) mode = 2;
                    if (mode == 1) {
                        int16_t ad = add >> 1;
                        int npx = blocks * 2;
                        uint16_t c = (uint16_t)color2;
                        for (int z = npx - 1; z >= 0; z--) {
                            c = (uint16_t)(c + ad);
                            if (z < 640) vdfg_buf[(z & 3) * 160 + (z >> 2)] = (uint8_t)(c >> 8);
                        }
                        uint8_t al = (lefts & 1) ? 0x44 : 0x11;
                        int ccx = npx + 3;
                        for (int k = 0; k < 4; k++) {
                            int cnt = (uint16_t)ccx >> 2;
                            if (cnt) {
                                setmask(al);
                                uint16_t d2 = di;
                                movs(d2, vdfg_buf + k * 160, cnt);
                            }
                            ccx--;
                            bool carry = al & 0x80;
                            al = (uint8_t)((al << 1) | (al >> 7));
                            if (carry) di++;
                        }
                    } else if (mode == 2) {
                        uint16_t c = (uint16_t)color2;
                        for (int z = blocks - 1; z >= 0; z--) {
                            c = (uint16_t)(c + add);
                            if (z < 640) vdfg_buf[(z & 1) * 320 + (z >> 1)] = (uint8_t)(c >> 8);
                        }
                        uint8_t al = (lefts & 1) ? 0xcc : 0x33;
                        setmask(al);
                        uint16_t d2 = di;
                        movs(d2, vdfg_buf, (blocks + 1) >> 1);
                        bool carry = (al >> 6) & 1;   // rol al,2: last bit rotated out
                        al = (uint8_t)((al << 2) | (al >> 6));
                        if (carry) di++;
                        if (blocks >> 1) {
                            setmask(al);
                            d2 = di;
                            movs(d2, vdfg_buf + 320, blocks >> 1);
                        }
                    } else {
                        int16_t ad = (int16_t)(add << 1);
                        int cnt = (blocks >> 1) + 1;
                        uint16_t c = (uint16_t)(color2 - ad);
                        for (int z = cnt - 1; z >= 0; z--) {
                            c = (uint16_t)(c + ad);
                            if (z < 640) vdfg_buf[z] = (uint8_t)(c >> 8);
                        }
                        int ccx = blocks;
                        const uint8_t *src = vdfg_buf;
                        if (lefts & 1) {
                            setmask(0x0c);
                            vput(di++, *src++);
                        } else ccx++;
                        bool carry = ccx & 1;
                        ccx >>= 1;
                        if (carry) {
                            setmask(0x0f);
                            movs(di, src, ccx);
                        } else {
                            ccx--;
                            setmask(0x0f);
                            movs(di, src, ccx);
                            src += ccx;
                            setmask(0x03);
                            vput(di++, *src++);
                        }
                    }
                }
            }
            vdf_di = (uint16_t)(vdf_di + rowlen);
        } while (--count);
    }
}

void vid_drawfill(const uint8_t *s)
{
    outpw(0x3c4, 0x0f02);
    int16_t bx = R16(s, 0);
    if (bx == 0) drawfill_nrm(s + 2);
    else if (bx == 1) drawfill_grd(s + 2);
}

} // namespace visu
