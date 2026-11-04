// VISU - PSI's 3D vector engine used by U2A and U2E.
// Port of VISU/VISU.C and the VISU assembler library (ACALC, ADRAW,
// ADRAWCLP, AVID, AVIDM1, AVIDFILL). Data layouts match CD.H/AD.INC since the
// objects are loaded straight from the original files.
#pragma once
#include <stdint.h>

namespace visu {

#pragma pack(push, 1)
struct rmatrix { int32_t m[9]; int32_t x, y, z; };
struct vlist { int32_t x, y, z; int16_t normal, reserved; };
struct nlist { int16_t x, y, z, reserved; };
struct pvlist { int16_t x, y; int16_t vf; int16_t reserved[5]; };
#pragma pack(pop)

enum { VF_UP = 1, VF_DOWN = 2, VF_LEFT = 4, VF_RIGHT = 8, VF_NEAR = 16, VF_FAR = 32 };
enum {
    F_DEFAULT = 0xf001, F_VISIBLE = 0x0001, F_FLIP = 0x0100, F_2SIDE = 0x0200, F_SHADE8 = 0x0400,
    F_SHADE16 = 0x0800, F_SHADE32 = 0x0C00, F_GOURAUD = 0x1000, F_TEXTURE = 0x2000
};
const int UNIT = 16384;

struct object {
    int flags;
    object *parent;
    rmatrix *r0;
    rmatrix *r;
    uint8_t *pd;        // polydata
    int pdlen;
    int plnum;
    int16_t *pl[16];    // polylists
    int vnum, nnum, nnum1;
    vlist *v0;
    nlist *n0;
    vlist *v;
    nlist *n;
    pvlist *pv;
    int vf;
    const char *name;
};

// projection / clip state (ADATA.ASM)
extern int32_t projclipx[2], projclipy[2], projclipz[2];
extern int32_t projmulx, projmuly, projaddx, projaddy;
extern int16_t projaspect, projoversampleshr;
extern uint16_t rows[480];
extern uint16_t rowlen;
extern uint32_t vrambase;   // byte offset of the drawing page in video memory

object *vis_loadobject(const char *fname);
void vis_drawobject(object *o);

void vid_init(int mode);
void vid_window(int32_t x1, int32_t x2, int32_t y1, int32_t y2, int32_t z1, int32_t z2);
void vid_cameraangle(uint16_t a);
void vid_setswitch(int write, int display);
void vid_clearbg(const uint8_t *bg);
void vid_clear();
void vid_deinit();

void calc_applyrmatrix(rmatrix *dest, const rmatrix *apply);
void calc_mulrmatrix(rmatrix *dest, const rmatrix *source);
int32_t calc_singlez(int vertex, const vlist *v, const rmatrix *m);
void calc_rotate(int count, vlist *dest, const vlist *src, const rmatrix *m);
void calc_nrotate(int count, nlist *dest, const nlist *src, const rmatrix *m);
int calc_project(int count, pvlist *dest, const vlist *src);
void calc_setrmatrix_ident(rmatrix *m);
void calc_setrmatrix_rotyxz(rmatrix *m, uint16_t rx, uint16_t ry, uint16_t rz);
void calc_setrmatrix_rotxyz(rmatrix *m, uint16_t rx, uint16_t ry, uint16_t rz);
void calc_setrmatrix_rotzyx(rmatrix *m, uint16_t rx, uint16_t ry, uint16_t rz);
int calc_invrmatrix(rmatrix *r);

void draw_polylist(const int16_t *l, const uint8_t *d, const vlist *v, const pvlist *pv, const nlist *n, int f);
typedef void (*fillroutine)(const uint8_t *drawdata);
void draw_setfillroutine(fillroutine r);
void vid_drawfill(const uint8_t *drawdata);

} // namespace visu
