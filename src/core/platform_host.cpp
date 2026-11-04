// Headless Linux test build. Time is virtual: it only advances when the demo
// waits, so runs are deterministic and as fast as the CPU allows.
//
//   s2r_host [--data DIR] [--start N] [--until SEC] [--dump DIR] [--dump-every N]
//            [--dump-at t1,t2,...] [--wav FILE] [--keys t:key,...]
//   keys: esc ctrl enter up down left right ff
#include "sys.h"
#include "vga.h"
#include "dis.h"
#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <algorithm>

namespace sys {

static double vt;
static double next_frame = 1e30, next_top = 1e30;
static bool clock_on;
static std::string datadir = "build/data";
static std::string dumpdir;
static int dump_every = 0;
static std::vector<double> dump_at;
static double until = 1e30;
static FILE *wav;
static uint32_t wav_frames;
static std::vector<std::pair<double, int>> keys;   // time, key (negative = ctrl)
static size_t keypos;
static std::vector<int> keyq;
static bool ctrl;
static vga::Frame frame;
static uint32_t frameno;

static void wav_header()
{
    uint32_t data = wav_frames * 4;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                     0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    uint32_t v = 36 + data; memcpy(h + 4, &v, 4);
    v = music::RATE; memcpy(h + 24, &v, 4);
    v = music::RATE * 4; memcpy(h + 28, &v, 4);
    memcpy(h + 40, &data, 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
}

void shutdown()
{
    if (wav) { wav_header(); fclose(wav); }
    printf("exit at t=%.2f frames=%u\n", vt, frameno);
    exit(0);
}

static int keyname(const char *s)
{
    if (!strcmp(s, "esc")) return 27;
    if (!strcmp(s, "enter")) return 13;
    if (!strcmp(s, "up")) return KEY_UP;
    if (!strcmp(s, "down")) return KEY_DOWN;
    if (!strcmp(s, "left")) return KEY_LEFT;
    if (!strcmp(s, "right")) return KEY_RIGHT;
    if (!strcmp(s, "ff")) return '1';
    if (!strcmp(s, "skip")) return KEY_SKIP;
    if (!strcmp(s, "ctrl")) return -1;
    return s[0];
}

void init(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--data") datadir = next();
        else if (a == "--until") until = atof(next().c_str());
        else if (a == "--dump") dumpdir = next();
        else if (a == "--dump-every") dump_every = atoi(next().c_str());
        else if (a == "--dump-at") {
            std::string s = next();
            for (char *p = strtok(&s[0], ","); p; p = strtok(nullptr, ",")) dump_at.push_back(atof(p));
            std::sort(dump_at.begin(), dump_at.end());
        } else if (a == "--wav") {
            wav = fopen(next().c_str(), "wb");
            if (wav) { uint8_t z[44] = {0}; fwrite(z, 1, 44, wav); }
        } else if (a == "--keys") {
            std::string s = next();
            for (char *p = strtok(&s[0], ","); p; p = strtok(nullptr, ",")) {
                char *c = strchr(p, ':');
                if (!c) continue;
                *c = 0;
                keys.push_back({atof(p), keyname(c + 1)});
            }
            std::sort(keys.begin(), keys.end());
        }
    }
    music::set_latency(0);
}

double now() { return vt; }

static void write_frame(const vga::Frame &f, double t)
{
    char name[512];
    snprintf(name, sizeof name, "%s/f%06u_%07.2f.ppm", dumpdir.c_str(), frameno, t);
    FILE *o = fopen(name, "wb");
    if (!o) return;
    fprintf(o, "P6\n640 480\n255\n");
    static uint8_t row[640 * 3];
    for (int y = 0; y < 480; y++) {
        int sy = f.h == 480 ? y : y * f.h / 480;
        const uint8_t *s = f.pix + sy * 640;
        for (int x = 0; x < 640; x++) {
            uint32_t c = f.pal[s[x]];
            row[x * 3] = c >> 16; row[x * 3 + 1] = c >> 8; row[x * 3 + 2] = c;
        }
        fwrite(row, 1, sizeof row, o);
    }
    fclose(o);
}

static void run_events()
{
    while (keypos < keys.size() && keys[keypos].first <= vt) {
        int k = keys[keypos++].second;
        if (k < 0) ctrl = true;
        else keyq.push_back(k);
    }
    if (clock_on) {
        while (vt >= next_frame) {
            dis::frame_tick();
            next_top = next_frame + 0.0012;
            next_frame += 1.0 / 70.086;
        }
        if (vt >= next_top) { dis::frame_top(); next_top = 1e30; }
    }
    uint64_t want = (uint64_t)(vt * music::RATE);
    uint64_t have = music::mixed_frames();
    if (want > have) {
        static int16_t buf[2 * 8192];
        while (want > have) {
            int n = (int)std::min<uint64_t>(want - have, 8192);
            music::mix(buf, n);
            if (wav) { fwrite(buf, 4, n, wav); wav_frames += n; }
            have += n;
        }
    }
    if (vt > until) shutdown();
}

void idle()
{
    vt += 1e-4;
    run_events();
}

void start_frame_clock()
{
    clock_on = true;
    next_frame = vt + 1.0 / 70.086;
}

vga::Frame *begin_frame() { return &frame; }

void end_frame(vga::Frame *f)
{
    frameno++;
    if (dumpdir.empty()) return;
    bool dump = dump_every && (frameno % dump_every) == 0;
    while (!dump_at.empty() && dump_at.front() <= vt) { dump = true; dump_at.erase(dump_at.begin()); }
    if (dump) write_frame(*f, vt);
}

void poll_input() {}
bool kbhit() { run_events(); return !keyq.empty(); }
int peekkey()
{
    if (keyq.empty()) return 0;
    int k = keyq.front();
    keyq.erase(keyq.begin());
    return k;
}
int getkey()
{
    while (keyq.empty()) idle();
    return peekkey();
}
bool ctrl_down() { return ctrl; }

uint8_t *load_file(const char *name, uint32_t *size)
{
    std::string n = name, up = name, lo = name;
    for (auto &c : up) c = toupper(c);
    for (auto &c : lo) c = tolower(c);
    for (const std::string &cand : {n, up, lo}) {
        std::string p = datadir + "/" + cand;
        FILE *f = fopen(p.c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long l = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *b = (uint8_t *)malloc(l + 16);
        if (fread(b, 1, l, f) != (size_t)l) { /* short read */ }
        memset(b + l, 0, 16);
        fclose(f);
        if (size) *size = (uint32_t)l;
        return b;
    }
    fprintf(stderr, "load_file: %s not found\n", name);
    return nullptr;
}

void log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[%8.3f] ", vt);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

} // namespace sys
