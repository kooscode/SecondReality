/* Demo Interrupt Server (DIS) - port of DIS/DISINT.ASM + MAIN/COPPER.ASM.
** Same API as the original DIS.H so the parts' code stays recognisable. */
#pragma once
#include <stdint.h>

int  dis_version(void);
int  dis_indemo(void);
int  dis_waitb(void);
int  dis_exit(void);
void dis_partstart(void);
void *dis_msgarea(int areanumber);
int  dis_muscode(int code);
int  dis_musplus(void);
int  dis_musrow(void);
void dis_setcopper(int routine_number, void (*routine)(void));
void dis_setmframe(int frame);
int  dis_getmframe(void);
int  dis_sync(void);

namespace dis {
void init();
void frame_tick();          // retrace: copper int1, capture, int2
void frame_top();           // ~scanline 25: copper int0
void part_begin();          // loader: before a part runs
void part_end();            // loader: after a part returns (resets coppers)
bool aborted();             // Ctrl / Back pressed: skip the rest of the demo
void clear_abort();
void checkkeys();
extern volatile int  copperframecount;
extern volatile uint32_t longframecount;
extern volatile int  exitflag;
}
