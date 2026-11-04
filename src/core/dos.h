// Small DOS/Borland/MSC compatibility layer for the ported part code.
#pragma once
#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "sys.h"
#include "dis.h"

inline void outp(int port, int v) { vga::out((uint16_t)port, (uint8_t)v); }
inline void outpw(int port, int v) { vga::outw((uint16_t)port, (uint16_t)v); }
inline int inp(int port)
{
    if (port == 0x3DA) sys::idle();
    return vga::in((uint16_t)port);
}

// int 10h AX=mode
inline void int10_setmode(int mode) { vga::setmode(mode); }

// Wait helpers used where the DOS code polled the retrace bit directly
inline void wait_retrace_start() { while (!(inp(0x3DA) & 8)) {} }
inline void wait_retrace_end() { while (inp(0x3DA) & 8) {} }
inline void wait_vr() { wait_retrace_end(); wait_retrace_start(); }

inline int kbhit() { return sys::kbhit(); }
inline int getch() { return sys::getkey(); }

// 16-bit helpers for faithful arithmetic
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef uint8_t  u8;
typedef int8_t   s8;

// Load a data file that the DOS part had linked into its EXE (or read from disk)
uint8_t *data_file(const char *name, uint32_t *size = nullptr);
