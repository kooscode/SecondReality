// Platform layer: implemented by platform_xbox.cpp (nxdk + SDL2) and
// platform_host.cpp (headless Linux test build with a virtual clock).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace vga { struct Frame; }

namespace sys {

void init(int argc, char **argv);
void shutdown();                 // never returns

double now();                    // seconds since init
void idle();                     // called by every busy-wait in ported code

// Copper ("timer interrupt") and presentation
void start_frame_clock();        // starts calling dis::frame_tick() at 70 Hz
vga::Frame *begin_frame();       // a free frame to render into (copper thread)
void end_frame(vga::Frame *f);   // publish it for display

// Input, as the DOS keyboard looked to the demo
enum Key {
    KEY_NONE = 0, KEY_ESC = 27, KEY_ENTER = 13, KEY_SPACE = 32,
    KEY_UP = 1072, KEY_DOWN = 1080, KEY_LEFT = 1075, KEY_RIGHT = 1077,
    KEY_FF = '1', KEY_SKIP = 2000
};
void poll_input();
bool kbhit();
int  getkey();                   // like getch(): blocks (idles) until a key
int  peekkey();                  // returns next key or 0, consuming it
bool ctrl_down();                // "abort demo" button held

// Files (from the data directory: D:\data on Xbox)
uint8_t *load_file(const char *name, uint32_t *size);   // malloc'd, nullptr on failure

void log(const char *fmt, ...);

} // namespace sys
