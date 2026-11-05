// Virtual VGA: register-level emulation of the parts of the IBM VGA that
// Second Reality uses (planar memory, chain-4, write modes 0-3, latches,
// CRTC start/offset/line compare/double scan, attribute controller, DAC,
// and 80x25 text mode). The parts are ported so that they talk to this
// exactly as they talked to the real card.
#pragma once
#include <stdint.h>

namespace vga {

extern uint8_t plane[4][65536];   // 256 KB video memory, one array per plane
extern uint8_t textmem[32768];    // B800:0000 text buffer (char, attr pairs)

void reset();
void set_font(const uint8_t *font8x16);   // 256 glyphs x 16 rows (CP437)

// BIOS int 10h, AH=0. Bit 7 of mode = don't clear memory.
// While text modes are locked (inside the demo) modes <= 4 are ignored, like
// the U2 loader's int 10h hook did.
void setmode(int mode);
void lock_textmodes(bool lock);
int  curmode();

// Port I/O
void    out(uint16_t port, uint8_t v);
void    outw(uint16_t port, uint16_t v);   // out port,ax : al to port, ah to port+1
uint8_t in(uint16_t port);

// CPU access to the A000 64 KB window
uint8_t rd(uint32_t off);
void    wr(uint32_t off, uint8_t v);
inline void wrw(uint32_t off, uint16_t v) { wr(off, (uint8_t)v); wr((off + 1) & 0xFFFF, (uint8_t)(v >> 8)); }
inline uint16_t rdw(uint32_t off) { uint16_t lo = rd(off); return lo | (rd((off + 1) & 0xFFFF) << 8); }
void    fill(uint32_t off, uint8_t v, uint32_t count);           // rep stosb
void    copy_in(uint32_t off, const uint8_t *src, uint32_t n);   // rep movsb to vram

// Direct register access helpers for ported code
uint8_t seq(int i);
uint8_t crtc(int i);
uint8_t gc(int i);
uint8_t attr(int i);
void    setpal(int index, uint8_t r, uint8_t g, uint8_t b);      // 6-bit values
void    setpalblock(int start, int count, const uint8_t *rgb);   // 6-bit triplets
void    getpal(int index, uint8_t *rgb);
const uint8_t *dacram();    // 768 bytes, 6-bit

// Rendering. Produces up to 640 dots x 480 lines of 8-bit DAC indices plus the
// 256-colour palette (8-bit RGB) in effect.
struct Frame {
    int w, h;                 // w is 640, h is 400 or 480
    uint8_t pix[640 * 480];
    uint32_t pal[256];        // 0x00RRGGBB
};
void render(Frame &f);
// Latch the display start address and panning (the VGA does this at the start
// of vertical retrace); render() uses the latched values.
void latch_display();

// Per-scanline hook used by the few parts that change registers mid-frame
// (their DOS code polled horizontal retrace). Called by render() before
// scanline y (0..h-1) is generated. nullptr disables.
typedef void (*RasterHook)(int y);
void set_raster_hook(RasterHook h);

// Vertical retrace status for port 3DA reads. Driven by the frame clock.
void set_retrace(bool on);

} // namespace vga
