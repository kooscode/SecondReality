// Scream Tracker 3 module player. Behaviour follows ST3.00 (the version both
// Second Reality modules were saved with), including fast volume slides and
// the shared effect memory.
#include "music.h"
#include <string.h>
#include <stdlib.h>
#include <atomic>

namespace music {

static const int MAXCH = 32;

struct Sample {
    const uint8_t *data = nullptr;   // unsigned 8-bit
    uint32_t len = 0, lbeg = 0, lend = 0;
    bool loop = false;
    int vol = 0;
    uint32_t c2spd = 8363;
};

struct Cell { uint8_t note, ins, vol, cmd, info; };

struct Module {
    bool ok = false;
    uint8_t *raw = nullptr;
    int ordnum = 0, insnum = 0, patnum = 0;
    uint16_t flags = 0, cwt = 0;
    uint8_t gvol = 64, speed = 6, tempo = 125, mvol = 0x30;
    bool stereo = true;
    uint8_t chset[32];
    uint8_t orders[256];
    Sample ins[100];
    Cell *pat = nullptr;      // patnum * 64 rows * 32 channels
};

static Module mods[2];

struct Chan {
    bool on = false;
    int pan = 8;              // 0..15
    const Sample *smp = nullptr;
    int insnum = 0;           // 1-based last instrument
    double pos = 0;           // sample position
    double step = 0;
    bool playing = false;
    int vol = 0;              // 0..64 channel volume
    int outvol = 0;           // volume after tremolo/tremor
    int period = 0, outperiod = 0;
    int portatarget = 0, portaspeed = 0;
    int lastnfo = 0;          // shared memory
    int vibspeed = 0, vibdepth = 0, vibpos = 0, vibwave = 0;
    int trempos = 0, tremwave = 0;
    int tremorcnt = 0; bool tremoron = true;
    int retrigcnt = 0;
    int offsetmem = 0;
    int note = 255;
    Cell cur{};
    int delaytick = -1, cuttick = -1;
    int loopstart = 0, loopcnt = 0;
    // ramp
    float lvol = 0, rvol = 0;    // current
};

struct Player {
    Module *m = nullptr;
    int slot = -1;
    bool playing = false;
    int ord = 0, row = 0, pat = 0;
    int tick = 0, speed = 6, tempo = 125, gvol = 64;
    int samples_left = 0;
    int breakord = -1, breakrow = -1;
    int patdelay = 0, patdelaycnt = 0;
    int rowsdone_patdelay = 0;
    int zinfo = 0;
    uint32_t frame = 0;
    Chan ch[MAXCH];
};

static Player pl;
static std::atomic<uint64_t> mixed{0};
static uint64_t mixpos;   // sample time of the tick being processed
static int latency_frames = 0;

// ------------------------------------------------------------ command queue
enum { CMD_PLAY, CMD_GOTO, CMD_STOP, CMD_GVOL };
struct Cmd { int type, a, b; };
static Cmd cmdq[64];
static std::atomic<unsigned> cq_head{0}, cq_tail{0};
static std::atomic<int> gvol_shadow{64};

static void push(int t, int a, int b)
{
    unsigned h = cq_head.load(std::memory_order_relaxed);
    cmdq[h & 63] = {t, a, b};
    cq_head.store(h + 1, std::memory_order_release);
}

// ------------------------------------------------------------ position ring
struct Snap { uint64_t t; Pos p; uint32_t rawframe; };
static Snap ring[256];
static std::atomic<unsigned> ring_n{0};
static int64_t frame_base = 0;      // demo-side offset for set_frame

static int calc_zplus()
{
    if (!pl.m) return 0;
    int z = 0;
    if (pl.ord + 1 < pl.m->ordnum && pl.m->orders[pl.ord + 1] == 254) z |= 1;
    if (pl.ord > 0 && pl.m->orders[pl.ord - 1] == 254) z |= 2;
    return z;
}

static void snapshot()
{
    unsigned n = ring_n.load(std::memory_order_relaxed);
    Snap &s = ring[n & 255];
    s.t = mixpos;
    s.p.ord = pl.ord; s.p.row = pl.row; s.p.pat = pl.pat;
    s.p.zplus = calc_zplus();
    s.p.zinfo = pl.zinfo;
    s.p.frame = 0;
    s.rawframe = pl.frame;
    ring_n.store(n + 1, std::memory_order_release);
}

static Snap current_snap()
{
    unsigned n = ring_n.load(std::memory_order_acquire);
    if (!n) return Snap{};
    uint64_t m = mixed.load();
    uint64_t target = m > (uint64_t)latency_frames ? m - latency_frames : 0;
    unsigned lim = n > 200 ? 200 : n;
    for (unsigned i = 1; i <= lim; i++) {
        const Snap &s = ring[(n - i) & 255];
        if (s.t <= target) return s;
    }
    return ring[(n - lim) & 255];
}

Pos pos()
{
    Snap s = current_snap();
    Pos p = s.p;
    p.frame = (int)((int64_t)s.rawframe - frame_base);
    return p;
}

void set_frame(int f)
{
    Snap s = current_snap();
    frame_base = (int64_t)s.rawframe - f;
}

void set_latency(int f) { latency_frames = f; }
uint64_t mixed_frames() { return mixed; }

// ------------------------------------------------------------ loading
bool load(int slot, const uint8_t *d, uint32_t size)
{
    Module &m = mods[slot];
    if (size < 0x60 || memcmp(d + 0x2C, "SCRM", 4)) return false;
    m.raw = (uint8_t *)malloc(size);
    memcpy(m.raw, d, size);
    auto u16 = [&](uint32_t o) { return (uint16_t)(d[o] | (d[o + 1] << 8)); };
    m.ordnum = u16(0x20); m.insnum = u16(0x22); m.patnum = u16(0x24);
    m.flags = u16(0x26); m.cwt = u16(0x28);
    m.gvol = d[0x30]; m.speed = d[0x31]; m.tempo = d[0x32]; m.mvol = d[0x33];
    m.stereo = (m.mvol & 0x80) != 0;
    memcpy(m.chset, d + 0x40, 32);
    memset(m.orders, 255, sizeof m.orders);
    memcpy(m.orders, d + 0x60, m.ordnum > 256 ? 256 : m.ordnum);
    uint32_t po = 0x60 + m.ordnum;
    for (int i = 0; i < m.insnum && i < 100; i++) {
        uint32_t o = u16(po + i * 2) * 16;
        const uint8_t *h = d + o;
        Sample &s = m.ins[i];
        if (h[0] != 1) continue;
        uint32_t seg = (h[13] << 16) | (h[14] | (h[15] << 8));
        uint32_t len = h[16] | (h[17] << 8) | (h[18] << 16) | (h[19] << 24);
        uint32_t lb = h[20] | (h[21] << 8) | (h[22] << 16) | (h[23] << 24);
        uint32_t le = h[24] | (h[25] << 8) | (h[26] << 16) | (h[27] << 24);
        if (len > 64000) len = 64000;
        if (seg * 16 + len > size) { if (seg * 16 >= size) len = 0; else len = size - seg * 16; }
        s.data = m.raw + seg * 16;
        s.len = len;
        s.vol = h[28] > 64 ? 64 : h[28];
        s.loop = (h[31] & 1) && le > lb;
        if (lb > len) lb = len;
        if (le > len) le = len;
        if (le <= lb) s.loop = false;
        s.lbeg = lb; s.lend = le;
        s.c2spd = h[32] | (h[33] << 8) | (h[34] << 16) | (h[35] << 24);
        if (s.c2spd > 65535) s.c2spd = 65535;
        if (!s.c2spd) s.c2spd = 8363;
    }
    // patterns (decrypt: key = p ^ (p << 2), p = offset from pattern start)
    m.pat = (Cell *)calloc((size_t)m.patnum * 64 * 32, sizeof(Cell));
    for (int i = 0; i < m.patnum * 64 * 32; i++) { m.pat[i].note = 255; m.pat[i].ins = 0; m.pat[i].vol = 255; m.pat[i].cmd = 0; }
    uint32_t pp = po + m.insnum * 2;
    uint8_t *buf = (uint8_t *)malloc(65536);   // not on the stack: nxdk's main stack is 64 KB
    for (int p = 0; p < m.patnum; p++) {
        uint32_t o = u16(pp + p * 2) * 16;
        if (!o || o + 2 > size) continue;
        uint32_t len = u16(o);
        if (o + len > size) len = size - o;
        for (uint32_t k = 0; k < len; k++) buf[k] = d[o + k] ^ (uint8_t)(k ^ (k << 2));
        uint32_t q = 2;
        int row = 0;
        while (row < 64 && q < len) {
            uint8_t b = buf[q++];
            if (!b) { row++; continue; }
            Cell &c = m.pat[(p * 64 + row) * 32 + (b & 31)];
            if (b & 32) { c.note = buf[q]; c.ins = buf[q + 1]; q += 2; }
            if (b & 64) { c.vol = buf[q]; q += 1; }
            if (b & 128) { c.cmd = buf[q]; c.info = buf[q + 1]; q += 2; }
        }
    }
    free(buf);
    m.ok = true;
    return true;
}

// ------------------------------------------------------------ player core
static const int stperiod[12] = {1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907};
static const uint8_t sinetab[32] = {0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
                                    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24};

static int note_period(int note, uint32_t c2)
{
    int n = note & 15, o = note >> 4;
    if (n > 11) n = 11;
    return (int)((8363LL * 16 * (stperiod[n] >> o)) / (int64_t)c2);
}

static int waveval(int wave, int pos)
{
    pos &= 63;
    switch (wave & 3) {
    case 1: return (pos < 32 ? 255 - pos * 8 : (pos - 32) * 8 - 255) ;   // ramp down
    case 2: return pos < 32 ? 255 : -255;
    case 3: return (rand() & 511) - 255;
    default: return pos < 32 ? sinetab[pos] : -sinetab[pos - 32];
    }
}

static void set_step(Chan &c, int period)
{
    if (period <= 0) { c.step = 0; return; }
    c.step = (14317056.0 / period) / RATE;
}

static void reset_channels()
{
    for (int i = 0; i < MAXCH; i++) {
        Chan &c = pl.ch[i];
        c = Chan();
        uint8_t cs = pl.m ? pl.m->chset[i] : 255;
        c.on = cs < 16;
        c.pan = pl.m && pl.m->stereo ? (cs < 8 ? 3 : 12) : 8;
    }
}

static void start_song(int slot, int order)
{
    pl.slot = slot;
    pl.m = (slot >= 0 && slot < 2 && mods[slot].ok) ? &mods[slot] : nullptr;
    if (!pl.m) { pl.playing = false; return; }
    reset_channels();
    pl.speed = pl.m->speed ? pl.m->speed : 6;
    pl.tempo = pl.m->tempo >= 32 ? pl.m->tempo : 125;
    pl.gvol = gvol_shadow.load();
    pl.ord = order;
    while (pl.ord < pl.m->ordnum && pl.m->orders[pl.ord] == 254) pl.ord++;
    if (pl.ord >= pl.m->ordnum || pl.m->orders[pl.ord] == 255) pl.ord = 0;
    pl.pat = pl.m->orders[pl.ord];
    pl.row = 0;
    pl.tick = 0;
    pl.samples_left = 0;
    pl.breakord = pl.breakrow = -1;
    pl.patdelay = pl.patdelaycnt = 0;
    pl.playing = true;
}

static void next_order()
{
    pl.ord++;
    for (int guard = 0; guard < 512; guard++) {
        if (pl.ord >= pl.m->ordnum || pl.m->orders[pl.ord] == 255) { pl.ord = 0; continue; }
        if (pl.m->orders[pl.ord] == 254) { pl.ord++; continue; }
        break;
    }
    pl.pat = pl.m->orders[pl.ord];
}

static void trigger(Chan &c, int ofs)
{
    if (!c.smp || !c.smp->len) { c.playing = false; return; }
    c.pos = ofs;
    if (c.pos >= c.smp->len) {
        if (c.smp->loop) c.pos = c.smp->lbeg;
        else { c.playing = false; return; }
    }
    c.playing = true;
}

static void do_vslide(Chan &c, int nfo, bool first)
{
    int x = nfo >> 4, y = nfo & 15;
    bool fast = pl.m->cwt == 0x1300 || (pl.m->flags & 64);
    if (y == 15 && x) { if (first) c.vol += x; }
    else if (x == 15 && y) { if (first) c.vol -= y; }
    else if (y == 0) { if (!first || fast) c.vol += x; }
    else if (x == 0) { if (!first || fast) c.vol -= y; }
    if (c.vol < 0) c.vol = 0;
    if (c.vol > 64) c.vol = 64;
}

static void clamp_period(Chan &c)
{
    if (c.period < 64) c.period = 64;
    if (c.period > 32767) c.period = 32767;
}

static void process_cell(Chan &c, const Cell &cell)
{
    // tick 0 handling of a row
    c.cur = cell;
    c.outvol = -1;
    c.delaytick = -1; c.cuttick = -1;
    int cmd = cell.cmd, nfo = cell.info;
    if (cmd == 19 && (nfo >> 4) == 0xD && (nfo & 15)) {   // SDx note delay
        c.delaytick = nfo & 15;
        return;
    }
    // instrument
    if (cell.ins && cell.ins <= pl.m->insnum) {
        c.insnum = cell.ins;
        const Sample *s = &pl.m->ins[cell.ins - 1];
        c.vol = s->vol;
        if (!(cmd == 7 || cmd == 12)) c.smp = s; // tone porta keeps sample playing
        else if (!c.smp) c.smp = s;
    }
    // note
    if (cell.note == 254) { c.playing = false; c.vol = 0; }
    else if (cell.note < 254) {
        const Sample *s = c.insnum ? &pl.m->ins[c.insnum - 1] : nullptr;
        int per = s ? note_period(cell.note, s->c2spd) : 0;
        if ((cmd == 7 || cmd == 12) && c.playing) {
            c.portatarget = per;
        } else {
            c.smp = s;
            c.period = per;
            c.portatarget = per;
            int ofs = 0;
            if (cmd == 15) {
                if (nfo) c.offsetmem = nfo;
                ofs = c.offsetmem << 8;
            }
            trigger(c, ofs);
            c.vibpos = 0;
            c.trempos = 0;
        }
        c.note = cell.note;
    }
    if (cell.vol <= 64) c.vol = cell.vol;

    // effect memory (ST3 shares one slot among most effects)
    if (cmd && nfo) {
        switch (cmd) {
        case 4: case 5: case 6: case 9: case 10: case 11: case 12: case 17: case 18: case 19:
            c.lastnfo = nfo; break;
        default: break;
        }
    }
    int mem = nfo ? nfo : c.lastnfo;
    switch (cmd) {
    case 1: if (nfo) pl.speed = nfo; break;                                  // A
    case 2: pl.breakord = nfo; if (pl.breakrow < 0) pl.breakrow = 0; break;   // B
    case 3:                                                                   // C
        pl.breakrow = (nfo >> 4) * 10 + (nfo & 15);
        if (pl.breakrow > 63) pl.breakrow = 0;
        if (pl.breakord < 0) pl.breakord = -2;
        break;
    case 4: do_vslide(c, mem, true); break;                                   // D
    case 5:                                                                   // E
        if ((mem >> 4) == 0xF) c.period += (mem & 15) * 4;
        else if ((mem >> 4) == 0xE) c.period += (mem & 15);
        clamp_period(c); break;
    case 6:                                                                   // F
        if ((mem >> 4) == 0xF) c.period -= (mem & 15) * 4;
        else if ((mem >> 4) == 0xE) c.period -= (mem & 15);
        clamp_period(c); break;
    case 7: if (nfo) c.portaspeed = nfo; break;                               // G
    case 8: case 21:                                                          // H, U
        if (nfo >> 4) c.vibspeed = nfo >> 4;
        if (nfo & 15) c.vibdepth = nfo & 15;
        break;
    case 11: do_vslide(c, mem, true); break;                                  // K
    case 12: do_vslide(c, mem, true); break;                                  // L
    case 17: c.retrigcnt = 0; break;                                          // Q
    case 19: {                                                                // S
        int x = mem >> 4, y = mem & 15;
        switch (x) {
        case 3: c.vibwave = y; break;
        case 4: c.tremwave = y; break;
        case 8: c.pan = y; break;
        case 0xA: c.pan = (y & 8) ? 15 - c.pan : c.pan; break;
        case 0xB:
            if (!y) c.loopstart = pl.row;
            else {
                if (!c.loopcnt) { c.loopcnt = y; pl.breakrow = c.loopstart; pl.breakord = -3; }
                else if (--c.loopcnt) { pl.breakrow = c.loopstart; pl.breakord = -3; }
            }
            break;
        case 0xC: c.cuttick = y; break;
        case 0xE: if (!pl.patdelaycnt) pl.patdelay = y; break;
        default: break;
        }
        break;
    }
    case 20: if (nfo >= 0x20) pl.tempo = nfo; break;                          // T
    case 22: pl.gvol = nfo > 64 ? 64 : nfo; break;                            // V
    case 26: pl.zinfo = nfo; break;                                           // Z (sync)
    default: break;
    }
    c.outperiod = c.period;
}

static void effect_tick(Chan &c)
{
    const Cell &cell = c.cur;
    int cmd = cell.cmd, nfo = cell.info;
    int mem = nfo ? nfo : c.lastnfo;
    c.outperiod = c.period;
    c.outvol = -1;
    if (c.delaytick == pl.tick) {
        Cell d = cell; d.cmd = 0; d.info = 0;
        process_cell(c, d);
        c.cur = cell;
        c.delaytick = -1;
        return;
    }
    if (c.cuttick == pl.tick) { c.vol = 0; }
    switch (cmd) {
    case 4: do_vslide(c, mem, false); break;
    case 5:
        if (mem < 0xE0) { c.period += mem * 4; clamp_period(c); }
        c.outperiod = c.period; break;
    case 6:
        if (mem < 0xE0) { c.period -= mem * 4; clamp_period(c); }
        c.outperiod = c.period; break;
    case 7: case 12: {
        if (c.portatarget && c.period) {
            int sp = c.portaspeed * 4;
            if (c.period < c.portatarget) { c.period += sp; if (c.period > c.portatarget) c.period = c.portatarget; }
            else if (c.period > c.portatarget) { c.period -= sp; if (c.period < c.portatarget) c.period = c.portatarget; }
        }
        c.outperiod = c.period;
        if (cmd == 12) do_vslide(c, mem, false);
        break;
    }
    case 8: case 11: case 21: {
        int d = (waveval(c.vibwave, c.vibpos) * c.vibdepth) >> 7;
        c.outperiod = c.period + (cmd == 21 ? d : d * 4);
        c.vibpos += c.vibspeed;
        if (cmd == 11) do_vslide(c, mem, false);
        break;
    }
    case 9: {   // tremor
        int on = (mem >> 4) + 1, off = (mem & 15) + 1;
        c.tremorcnt++;
        if (c.tremoron) { if (c.tremorcnt >= on) { c.tremoron = false; c.tremorcnt = 0; } }
        else { if (c.tremorcnt >= off) { c.tremoron = true; c.tremorcnt = 0; } }
        if (!c.tremoron) c.outvol = 0;
        break;
    }
    case 10: {  // arpeggio
        int t = pl.tick % 3;
        if (t && c.insnum) {
            int add = t == 1 ? (mem >> 4) : (mem & 15);
            int n = (c.note >> 4) * 12 + (c.note & 15) + add;
            int note = ((n / 12) << 4) | (n % 12);
            c.outperiod = note_period(note, pl.m->ins[c.insnum - 1].c2spd);
            // keep any fine porta offset
            c.outperiod += c.period - note_period(c.note, pl.m->ins[c.insnum - 1].c2spd);
        }
        break;
    }
    case 17: {  // retrig + volume change
        int x = mem >> 4, y = mem & 15;
        if (y && ++c.retrigcnt >= y) {
            c.retrigcnt = 0;
            trigger(c, 0);
            switch (x) {
            case 1: c.vol -= 1; break; case 2: c.vol -= 2; break; case 3: c.vol -= 4; break;
            case 4: c.vol -= 8; break; case 5: c.vol -= 16; break; case 6: c.vol = c.vol * 2 / 3; break;
            case 7: c.vol /= 2; break; case 9: c.vol += 1; break; case 10: c.vol += 2; break;
            case 11: c.vol += 4; break; case 12: c.vol += 8; break; case 13: c.vol += 16; break;
            case 14: c.vol = c.vol * 3 / 2; break; case 15: c.vol *= 2; break; default: break;
            }
            if (c.vol < 0) c.vol = 0;
            if (c.vol > 64) c.vol = 64;
        }
        break;
    }
    case 18: {  // tremolo
        int d = (waveval(c.tremwave, c.trempos) * (mem & 15)) >> 6;
        int v = c.vol + d;
        c.outvol = v < 0 ? 0 : v > 64 ? 64 : v;
        c.trempos += mem >> 4;
        break;
    }
    default: break;
    }
}

static void do_row()
{
    pl.breakord = pl.breakrow = -1;
    const Cell *row = &pl.m->pat[(pl.pat * 64 + pl.row) * 32];
    for (int i = 0; i < MAXCH; i++)
        if (pl.ch[i].on) process_cell(pl.ch[i], row[i]);
}

static void advance_row()
{
    if (pl.patdelay) {
        if (pl.patdelaycnt < pl.patdelay) { pl.patdelaycnt++; return; }
        pl.patdelay = 0; pl.patdelaycnt = 0;
    }
    if (pl.breakord == -3) {               // pattern loop
        pl.row = pl.breakrow;
    } else if (pl.breakord >= 0 || pl.breakord == -2) {
        if (pl.breakord >= 0) {
            pl.ord = pl.breakord - 1;
        }
        int r = pl.breakrow < 0 ? 0 : pl.breakrow;
        next_order();
        pl.row = r;
    } else if (++pl.row >= 64) {
        pl.row = 0;
        next_order();
    }
}

static void tick()
{
    if (pl.tick == 0) {
        if (pl.patdelaycnt == 0) do_row();
        else for (int i = 0; i < MAXCH; i++) if (pl.ch[i].on) { Chan &c = pl.ch[i]; c.outperiod = c.period; c.outvol = -1; }
    } else {
        for (int i = 0; i < MAXCH; i++) if (pl.ch[i].on) effect_tick(pl.ch[i]);
    }
    for (int i = 0; i < MAXCH; i++) {
        Chan &c = pl.ch[i];
        if (!c.on) continue;
        set_step(c, c.outperiod ? c.outperiod : c.period);
    }
    pl.frame++;
    snapshot();
    if (++pl.tick >= pl.speed) {
        pl.tick = 0;
        advance_row();
    }
}

static void apply_cmds()
{
    unsigned h = cq_head.load(std::memory_order_acquire);
    unsigned t = cq_tail.load(std::memory_order_relaxed);
    while (t != h) {
        Cmd c = cmdq[t & 63];
        t++;
        switch (c.type) {
        case CMD_PLAY: start_song(c.a, c.b); snapshot(); break;
        case CMD_GOTO:
            if (pl.m) {
                pl.ord = c.a;
                while (pl.ord < pl.m->ordnum && pl.m->orders[pl.ord] == 254) pl.ord++;
                if (pl.ord >= pl.m->ordnum || pl.m->orders[pl.ord] == 255) pl.ord = 0;
                pl.pat = pl.m->orders[pl.ord];
                pl.row = c.b; pl.tick = 0; pl.patdelay = pl.patdelaycnt = 0;
                pl.playing = true;
            }
            break;
        case CMD_STOP:
            pl.playing = false;
            for (int i = 0; i < MAXCH; i++) pl.ch[i].playing = false;
            break;
        case CMD_GVOL: pl.gvol = c.a; break;
        }
    }
    cq_tail.store(t, std::memory_order_release);
}

void play(int slot, int order) { push(CMD_PLAY, slot, order); }
void gotosong(int order, int row) { push(CMD_GOTO, order, row); }
void stop() { push(CMD_STOP, 0, 0); }
void set_global_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 64) v = 64;
    gvol_shadow = v;
    push(CMD_GVOL, v, 0);
}
int global_volume() { return gvol_shadow; }

void mix(int16_t *out, int frames)
{
    mixpos = mixed.load();
    apply_cmds();
    static int32_t acc[2 * 4096];
    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;
        memset(acc, 0, n * 2 * sizeof(int32_t));
        int done = 0;
        while (done < n) {
            mixpos = mixed.load() + done;
            if (pl.playing && pl.m && pl.samples_left <= 0) {
                tick();
                pl.samples_left = (RATE * 5) / (pl.tempo * 2);
            }
            int run = n - done;
            if (pl.playing && pl.m && run > pl.samples_left) run = pl.samples_left;
            if (pl.playing && pl.m) {
                // master scaling: global volume (0..64) and master volume (0..127)
                float master = (pl.gvol / 64.0f) * ((pl.m->mvol & 0x7F) / 48.0f) * 0.30f;
                for (int i = 0; i < MAXCH; i++) {
                    Chan &c = pl.ch[i];
                    if (!c.on || !c.playing || !c.smp || c.step <= 0) {
                        c.lvol *= 0.0f; c.rvol *= 0.0f;
                        continue;
                    }
                    int v = c.outvol >= 0 ? c.outvol : c.vol;
                    float vol = v / 64.0f * master * 256.0f;
                    float pr = c.pan / 15.0f;
                    float tl = vol * (1.0f - pr) * 2.0f, tr = vol * pr * 2.0f;
                    if (!pl.m->stereo) { tl = tr = vol; }
                    const Sample &s = *c.smp;
                    int32_t *a = acc + done * 2;
                    double pos = c.pos, step = c.step;
                    uint32_t end = s.loop ? s.lend : s.len;
                    float lv = c.lvol, rv = c.rvol;
                    const float ramp = 1.0f / 48.0f;
                    for (int k = 0; k < run; k++) {
                        if (pos >= end) {
                            if (s.loop) {
                                double ll = (double)(s.lend - s.lbeg);
                                while (pos >= end) pos -= ll;
                            } else { c.playing = false; break; }
                        }
                        uint32_t ip = (uint32_t)pos;
                        float fr = (float)(pos - ip);
                        int s0 = (int)s.data[ip] - 128;
                        uint32_t ip1 = ip + 1;
                        if (ip1 >= end) ip1 = s.loop ? s.lbeg : ip;
                        int s1 = (int)s.data[ip1] - 128;
                        float smp = s0 + (s1 - s0) * fr;
                        lv += (tl - lv) * ramp; rv += (tr - rv) * ramp;
                        a[k * 2] += (int32_t)(smp * lv);
                        a[k * 2 + 1] += (int32_t)(smp * rv);
                        pos += step;
                    }
                    c.pos = pos; c.lvol = lv; c.rvol = rv;
                }
                pl.samples_left -= run;
            }
            done += run;
        }
        for (int i = 0; i < n * 2; i++) {
            int32_t v = acc[i] >> 1;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out[i] = (int16_t)v;
        }
        out += n * 2;
        frames -= n;
        mixed.fetch_add(n);
    }
}

} // namespace music
