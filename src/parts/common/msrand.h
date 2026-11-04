// Microsoft C runtime rand() (each part EXE started with the default seed 1)
#pragma once
#include <stdint.h>
struct MsRand {
    uint32_t next = 1;
    int operator()() { next = next * 214013u + 2531011u; return (int)((next >> 16) & 0x7fff); }
};
