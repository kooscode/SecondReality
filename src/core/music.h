// STMIK replacement: a Scream Tracker 3 (S3M) player with the "now playing"
// information the Second Reality parts synchronise to.
#pragma once
#include <stdint.h>

namespace music {

const int RATE = 48000;

// Load a module from memory (data is copied; patterns are decrypted).
// slot 0 = MUSIC0 (Skaven), slot 1 = MUSIC1 (Purple Motion)
bool load(int slot, const uint8_t *data, uint32_t size);

void play(int slot, int order);     // zinitmodule + zplaysong
void gotosong(int order, int row);  // zgotosong
void stop();                        // zstopsong + zshutup
void set_global_volume(int v);      // module header byte 30h (0..64)
int  global_volume();

// Render interleaved stereo s16 samples. Called from the audio thread (or the
// virtual clock on the host build).
void mix(int16_t *out, int frames);

// Position the listener is hearing now (accounts for output latency).
struct Pos {
    int ord, row, pat;
    int zplus;      // bit0: next order is a '+++' marker, bit1: previous order was one
    int zinfo;      // last Zxx parameter
    int frame;      // tick counter (np_zframe)
};
Pos pos();
void set_frame(int f);              // dis_setmframe

// Latency compensation, in output frames (samples per channel)
void set_latency(int frames);
uint64_t mixed_frames();

} // namespace music
