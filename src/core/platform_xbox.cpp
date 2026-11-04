// Original Xbox platform layer: nxdk + SDL2.
//  - video: 640x480x32, three framebuffers; a presenter thread converts the
//    latest emulated VGA frame and flips at vertical blank (polled, no IRQ)
//  - the VGA's 70 Hz retrace (the demo's "copper" timer interrupt) runs on a
//    timer thread, like the original timer IRQ
//  - audio: the S3M player is mixed at 48 kHz by a polling thread, out of the
//    AC97 on a real Xbox and through an APU voice under xemu (see below)
//  - input: SDL game controller
// Nothing here relies on device interrupts: some never arrive under xemu and
// nxdk's AC97 handler can hang a real console after a quick reboot.
#include "sys.h"
#include "vga.h"
#include "dis.h"
#include "music.h"
#include <SDL.h>
#include <hal/video.h>
#include <hal/xbox.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <atomic>

namespace sys {

static const double VGA_HZ = 70.086;

static SDL_Window *window;
static double tfreq;
static ULONGLONG t0;

// triple buffered frames: copper thread renders, presenter shows the latest
static vga::Frame frames[3];
static std::atomic<int> latest{-1}, showing{-1};
static std::atomic<unsigned> published{0};

// input
static int keyq[32];
static std::atomic<unsigned> kq_head{0}, kq_tail{0};
static std::atomic<bool> back_held{false};
static SDL_GameController *pads[4];
static double last_poll;
static volatile unsigned audio_calls;

double now() { return (double)(KeQueryPerformanceCounter() - t0) / tfreq; }

#ifdef S2R_AUTOSTART
static char loglines[6][96];
static int logn;
#endif
static const uint8_t *dbgfont;

void log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    DbgPrint("[%u] %s\n", (unsigned)(now() * 1000), buf);
#ifdef S2R_AUTOSTART
    snprintf(loglines[logn % 6], 96, "%6u %s", (unsigned)(now() * 100), buf);
    logn++;
#endif
}

#ifdef S2R_AUTOSTART
// test builds: draw the last log lines and the frame/music counters on top
static void overlay(uint8_t *dst, int pitch)
{
    if (!dbgfont) dbgfont = load_file("FONT8X16.BIN", nullptr);
    if (!dbgfont) return;
    char lines[8][96];
    int n = 0;
    music::Pos p = music::pos();
    snprintf(lines[n++], 96, "t=%u frames=%u audio=%u ord=%d row=%d plus=%d", (unsigned)(now() * 100), (unsigned)dis::longframecount, audio_calls, p.ord, p.row, p.zplus);
    for (int i = (logn > 6 ? logn - 6 : 0); i < logn; i++) snprintf(lines[n++], 96, "%s", loglines[i % 6]);
    for (int l = 0; l < n; l++)
        for (int c = 0; lines[l][c] && c < 78; c++)
            for (int y = 0; y < 16; y++) {
                uint8_t bits = dbgfont[(uint8_t)lines[l][c] * 16 + y];
                uint32_t *d = (uint32_t *)(dst + (l * 16 + y) * pitch) + c * 8;
                for (int b = 0; b < 8; b++) d[b] = (bits & (0x80 >> b)) ? 0xffffff00 : 0xff000000;
            }
}
#endif

static void wait_until(double t)
{
    for (;;) {
        double d = t - now();
        if (d <= 0) return;
        if (d > 0.002) Sleep((DWORD)((d - 0.0015) * 1000.0));
        else SwitchToThread();
    }
}

// ------------------------------------------------------------ copper thread
static DWORD WINAPI copper_thread(LPVOID)
{
    double start = now();
    uint64_t k = 0;
    for (;;) {
        k++;
        double t = start + k / VGA_HZ;
        wait_until(t);
        dis::frame_tick();
        wait_until(t + 0.0012);
        dis::frame_top();
        // if we fell far behind (debugger etc), resync instead of bursting
        if (now() - t > 0.25) { start = now(); k = 0; }
    }
    return 0;
}

void start_frame_clock()
{
    HANDLE h = CreateThread(nullptr, 64 * 1024, copper_thread, nullptr, 0, nullptr);
    SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
}

vga::Frame *begin_frame()
{
    int l = latest.load(), s = showing.load();
    for (int i = 0; i < 3; i++)
        if (i != l && i != s) return &frames[i];
    return nullptr;
}

void end_frame(vga::Frame *f)
{
    latest = (int)(f - frames);
    published++;
}

// ------------------------------------------------------------ presenter
// Three framebuffers. The presenter converts the newest VGA frame into a
// buffer that left the screen at least two flips ago and points the CRTC at
// it during vertical blank (see wait_vblank), so nothing tears.
static uint8_t *fbs[3];
static int fbcur;

// Test builds publish the framebuffers so the host can read frames out of
// guest RAM (QMP pmemsave) instead of grabbing the emulator window.
struct FbInfo { char magic[8]; uint32_t phys[3]; volatile uint32_t cur; uint32_t w, h, pitch; };
extern "C" volatile FbInfo s2r_fbinfo;
volatile FbInfo s2r_fbinfo = {{'S', '2', 'R', 'F', 'B', 'U', 'F', '!'}, {0, 0, 0}, 0, 640, 480, 640 * 4};

static void convert(const vga::Frame &f, uint8_t *dst, int pitch)
{
    int h = f.h > 0 ? f.h : 400;
    for (int y = 0; y < 480; y++) {
        int sy = h == 480 ? y : y * h / 480;
        const uint8_t *s = f.pix + sy * 640;
        uint32_t *d = (uint32_t *)(dst + y * pitch);
        for (int x = 0; x < 640; x++) d[x] = f.pal[s[x]];
    }
}

// Flips are synchronised to the display's vertical blank on real hardware.
// The GPU latches its vblank status bit (PCRTC_INTR bit 0) whether or not the
// interrupt is enabled, so it is polled: sleep until shortly before the
// expected vblank, then poll and flip. No interrupt is used (nxdk's vblank
// wait blocks forever if the IRQ never arrives). xemu keeps the plain timer.
static bool use_vsync;                  // set in init() before the thread starts
static const uint32_t PCRTC_INTR = 0x00600100;

// returns false if no vblank was seen within 50 ms (then the timer is used)
static unsigned vb_flips, vb_late, vb_missed;

// Waits for the next vertical blank. The caller flips right after it returns,
// still at the raised priority, and then calls vblank_done().
static bool wait_vblank(double &last, double &period)
{
    // Time critical while waiting so that no other thread (the audio mixer
    // runs at HIGHEST and mixes ~20 ms at a time) can delay the wake-up past
    // the vblank. The window is short: we sleep until 2 ms before it.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    if (last > 0) {
        double n = now();
        int k = (int)((n - last + 0.002) / period) + 1;
        double target = last + k * period - 0.002;
        if (target - n > 0.0005) Sleep((DWORD)((target - n) * 1000.0));
    }
    VIDEOREG(PCRTC_INTR) = 1;                       // clear the latched status
    double t0 = now();
    // Poll gently: hammering GPU registers while the picture is being scanned
    // out starves the CRTC's memory fetch and shows up as pixel noise.
    while (!(VIDEOREG(PCRTC_INTR) & 1)) {
        if (now() - t0 > 0.05) return false;
        KeStallExecutionProcessor(20);
    }
    double t = now();
    vb_flips++;
    if (t - t0 > period * 0.5) vb_missed++;         // woke after the vblank: one frame late
    else if (t - t0 > 0.004) vb_late++;
    if (vb_flips % 3600 == 0) {
        log("video: last 3600 flips: %u late wake-ups, %u missed vblanks", vb_late, vb_missed);
        vb_late = vb_missed = 0;
    }
    if (last > 0) {
        double d = t - last;
        int n = (int)(d / period + 0.5);            // vblanks since the last one
        if (n >= 1 && n <= 4) period += (d / n - period) * 0.05;
    }
    last = t;
    return true;
}

static void vblank_done() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); }

static DWORD WINAPI present_thread(LPVOID)
{
    unsigned seen = 0;
    double next = now();
    double vb_last = 0, vb_period = 1.0 / 60.0;
    bool vsync = use_vsync;
    if (vsync) {
        // measure the refresh: the first two vblanks
        bool ok = wait_vblank(vb_last, vb_period) && wait_vblank(vb_last, vb_period);
        vblank_done();
        if (ok)
            log("video: vsync flips, refresh %d mHz", (int)(1000.0 / vb_period));
        else {
            vsync = false;
            log("video: no vblank status, timer flips");
        }
    }
    for (;;) {
        if (vsync) {
            // wait for a new frame (polling every ms), then flip at the next vblank
            if (published.load() == seen) { Sleep(1); continue; }
        } else {
            next += 1.0 / 60.0;
            if (now() - next > 0.1) next = now();
            wait_until(next);
        }
        unsigned p = published.load();
        if (p == seen) continue;
        seen = p;
        int idx = latest.load();
        if (idx < 0) continue;
        showing = idx;
        if (latest.load() != idx) { showing = -1; continue; }
        int back = (fbcur + 1) % 3;
        convert(frames[idx], fbs[back], 640 * 4);
        showing = -1;
#ifdef S2R_AUTOSTART
        overlay(fbs[back], 640 * 4);
#endif
        asm __volatile__("sfence");
        bool waited = false;
        if (vsync) {
            waited = true;
            if (!wait_vblank(vb_last, vb_period)) {
                vsync = false;
                log("video: vblank status lost, timer flips");
            }
        }
        VIDEOREG(0x00600800) = (unsigned int)fbs[back] & 0x7FFFFFFF;   // PCRTC_START
        if (waited) vblank_done();
        fbcur = back;
        s2r_fbinfo.cur = back;
#ifdef S2R_DIAG
        static unsigned flips;
        if (++flips <= 3 || flips % 600 == 0) {
            const vga::Frame &fr = frames[idx];
            unsigned pnz = 0;
            for (int i = 0; i < 640 * 400; i++) pnz += fr.pix[i] != 0;
            char b[200];
            vga::debug_state(b, sizeof b);
            log("diag: vga %s frame_nz=%u", b, pnz);
            log("diag: flip %u back=%d fb=%08x crtc=%08x frame %dx%d px=%08x pal0=%08x", flips, back,
                (unsigned)fbs[back], (unsigned)VIDEOREG(0x00600800), fr.w, fr.h,
                ((uint32_t *)fbs[back])[240 * 640 + 320], fr.pal[0]);
        }
#endif
    }
    return 0;
}

// ------------------------------------------------------------ audio
// The audio runs from a polling thread instead of nxdk's interrupt driven
// XAudio (whose interrupt never arrives under xemu): it keeps a few buffers
// queued in the AC97 descriptor ring and refills them as the hardware's
// current index (CIV) moves on.
static const int ABUF_FRAMES = 1024, ABUF_COUNT = 32, ABUF_AHEAD = 4;
static uint8_t *abuf[ABUF_COUNT];

// Our own minimal AC97 (MCPX ACI) setup instead of nxdk's XAudioInit: that
// connects a level triggered IRQ 6 handler which only clears the PCM out and
// S/PDIF status, so any other source left asserted by the previous title
// (e.g. after a quick reboot) makes it fire forever and hangs the console.
// Here every bus master is reset, every status cleared and no interrupt is
// enabled or connected: the thread below polls the hardware instead.
static volatile uint8_t *const aci = (volatile uint8_t *)0xFEC00000;
struct AciDesc { uint32_t addr; uint16_t samples; uint16_t ctl; };
static AciDesc *pcm_desc, *spdif_desc;           // 32 entry rings

static inline volatile uint32_t &aci32(uint32_t off) { return *(volatile uint32_t *)(aci + off); }

static void spin_ms(int ms)
{
    double t = now();
    while ((now() - t) * 1000.0 < ms) {}
}

static bool aci_init()
{
    // The kernel has already brought the codec up; a cold reset of the AC-link
    // is only done when the codec is not ready (resetting it intermittently
    // hung a real console). Only the channels nxdk itself uses are touched.
    log("aci: status %08x control %08x", (unsigned)aci32(0x130), (unsigned)aci32(0x12C));
    static const uint32_t ctl[2] = {0x11B, 0x17B};          // PCM out, S/PDIF out
    for (uint32_t c : ctl) {
        aci[c] = 0;                                          // stop
        aci[c] = 0x02;                                       // reset registers
        double t = now();
        while ((aci[c] & 0x02) && now() - t < 0.1) {}
        aci[c] = 0;
        aci[c - 0x0B + 0x06] = 0x1C;                         // clear LVBCI/BCIS/FIFOE
    }
    log("aci: bus masters reset");
    bool ready = (aci32(0x130) & 0x100) != 0;
    if (!ready) {
        aci32(0x12C) &= ~0x02u;                             // cold reset, like nxdk
        spin_ms(1);
        aci32(0x12C) |= 0x02u;
        double t = now();
        while (!(aci32(0x130) & 0x100) && now() - t < 0.5) {}
        ready = (aci32(0x130) & 0x100) != 0;
        log("aci: cold reset, codec %s", ready ? "ready" : "NOT ready");
    }

    pcm_desc = (AciDesc *)MmAllocateContiguousMemoryEx(32 * sizeof(AciDesc), 0, 0x03FFFFFF, 0x100, PAGE_READWRITE | PAGE_NOCACHE);
    spdif_desc = (AciDesc *)MmAllocateContiguousMemoryEx(32 * sizeof(AciDesc), 0, 0x03FFFFFF, 0x100, PAGE_READWRITE | PAGE_NOCACHE);
    memset((void *)pcm_desc, 0, 32 * sizeof(AciDesc));
    memset((void *)spdif_desc, 0, 32 * sizeof(AciDesc));
    aci32(0x110) = (uint32_t)MmGetPhysicalAddress(pcm_desc);
    aci32(0x170) = (uint32_t)MmGetPhysicalAddress(spdif_desc);
    log("aci: descriptors set");
    return ready;
}

// queue one buffer in descriptor slot i (no interrupt on completion)
static void aci_queue(int i, void *buf, int frames)
{
    uint32_t phys = (uint32_t)MmGetPhysicalAddress(buf);
    pcm_desc[i] = {phys, (uint16_t)(frames * 2), 0};
    spdif_desc[i] = {phys, (uint16_t)(frames * 2), 0};
    aci[0x115] = (uint8_t)i;                     // last valid index
    aci[0x175] = (uint8_t)i;
}

static void aci_run()
{
    aci[0x11B] = 0x01;                           // run, all interrupt enables off
    aci[0x17B] = 0x01;
}

static void aci_stop()
{
    aci[0x11B] = 0;
    aci[0x17B] = 0;
}

static DWORD WINAPI audio_thread(LPVOID)
{
    int queued = 0;          // descriptors handed to the hardware
    int done = 0;            // descriptors finished
    int lastciv = aci[0x114] & 31;
    auto queue_one = [&]() {
        int i = queued % ABUF_COUNT;
        music::mix((int16_t *)abuf[i], ABUF_FRAMES);
        aci_queue(i, abuf[i], ABUF_FRAMES);
        queued++;
        audio_calls++;
    };
    for (int i = 0; i < ABUF_AHEAD; i++) queue_one();
    aci_run();
    for (;;) {
        int civ = aci[0x114] & 31;
        done += (civ - lastciv + ABUF_COUNT) % ABUF_COUNT;
        lastciv = civ;
        if (aci[0x116] & 1) {            // DMA halted (ran dry): restart
            aci[0x116] = 0x1C;
            aci[0x176] = 0x1C;
            aci_run();
        }
        while (queued - done < ABUF_AHEAD) queue_one();
        // latency = everything mixed but not yet played
        uint16_t picb = *(volatile uint16_t *)(aci + 0x118);   // samples left in the current buffer
        int left = (queued - done) * ABUF_FRAMES - (ABUF_FRAMES - picb / 2);
        music::set_latency(left > 0 ? left : 0);
        Sleep(4);
    }
    return 0;
}

// ------------------------------------------------------------ APU output
// xemu plays only the MCPX APU (the DirectSound hardware) on the host; its AC97
// has no host audio backend. Under xemu one APU voice loops over a PCM ring in
// RAM and is fed from its play position. xemu's default setting (no DSP)
// sends the voice processor output straight to the speakers, so no DSP
// program is needed. A real Xbox keeps using the AC97 path above.
static const uint32_t APU_BASE = 0xFE800000;
static const int APU_VOICE = 64;                 // first 2D (non HRTF) voice
static const int RING_FRAMES = 16384, RING_CHUNK = 1024, RING_LEAD = 4096;
static volatile uint32_t *apu_regs() { return (volatile uint32_t *)APU_BASE; }
static uint8_t *apu_voices, *apu_sge, *ring;
static bool use_apu;

static inline void apu_wr(uint32_t reg, uint32_t v) { apu_regs()[reg / 4] = v; }
static inline volatile uint32_t *apu_voice(int v, uint32_t off)
{
    return (volatile uint32_t *)(apu_voices + v * 0x80 + off);
}

static bool running_on_xemu()
{
    // AC97 codec vendor ID: the real Xbox has a Wolfson WM9709 (0x574D),
    // xemu emulates QEMU's SigmaTel STAC9700 (0x8384). Codec reads go over the
    // AC-link, so take the codec access semaphore first (bit 0 of 0x134 reads
    // 1 while another access is in progress) and give up rather than wait.
    volatile uint8_t &cas = *(volatile uint8_t *)(0xFEC00000 + 0x134);   // byte register
    // reading the register also claims the semaphore: stop at the first free read
    double t = now();
    bool got = false;
    while (!(got = !(cas & 1)) && now() - t < 0.05) {}
    if (!got) { log("aci: codec busy, assuming real hardware"); return false; }
    volatile uint16_t *codec = (volatile uint16_t *)0xfec00000;
    uint16_t id = codec[0x7C / 2];
    log("aci: codec id %04x", id);
    return id == 0x8384;
}

static void apu_init()
{
    const int ring_bytes = RING_FRAMES * 4, pages = ring_bytes / 4096;
    apu_voices = (uint8_t *)MmAllocateContiguousMemoryEx(256 * 0x80, 0, 0x03FFFFFF, 0x1000, PAGE_READWRITE | PAGE_NOCACHE);
    apu_sge = (uint8_t *)MmAllocateContiguousMemoryEx(4096, 0, 0x03FFFFFF, 0x1000, PAGE_READWRITE | PAGE_NOCACHE);
    ring = (uint8_t *)MmAllocateContiguousMemoryEx(ring_bytes, 0, 0x03FFFFFF, 0x1000, PAGE_READWRITE | PAGE_NOCACHE);
    memset(apu_voices, 0, 256 * 0x80);
    memset(apu_sge, 0, 4096);
    memset(ring, 0, ring_bytes);
    // scatter/gather table: one {physical address, control} entry per 4 KB page
    uint32_t *sge = (uint32_t *)apu_sge;
    for (int i = 0; i < pages; i++) {
        sge[i * 2] = (uint32_t)MmGetPhysicalAddress(ring + i * 4096);
        sge[i * 2 + 1] = 0;
    }

    // stop the frame engine while the voice tables are set up
    apu_wr(0x2000, 0);                           // SECTL: XCNTMODE off
    apu_wr(0x1100, 0);                           // FECTL: free running
    apu_wr(0x1004, 0);                           // IEN: no interrupts
    apu_wr(0x1000, 0xFFFFFFFF);                  // ISTS: clear
    apu_wr(0x202C, (uint32_t)MmGetPhysicalAddress(apu_voices));   // VPVADDR
    apu_wr(0x2030, (uint32_t)MmGetPhysicalAddress(apu_sge));      // VPSGEADDR
    apu_wr(0x2054, APU_VOICE);                   // TVL2D: 2D list = our voice
    apu_wr(0x2060, 0xFFFF);                      // TVL3D: empty
    apu_wr(0x206C, 0xFFFF);                      // TVLMP: empty

    const int v = APU_VOICE;
    *apu_voice(v, 0x00) = 0 | (1 << 5);          // CFG_VBIN: left -> bin 0, right -> bin 1
    *apu_voice(v, 0x04) = (1u << 30)             // CFG_FMT: 16 bit container
                        | (1u << 28)             //   signed 16 bit samples
                        | (1u << 27)             //   stereo
                        | (1u << 25)             //   loop
                        | (1u << 16);            //   2 samples per block (one stereo frame)
    *apu_voice(v, 0x18) = 0;                     // CFG_MISC: filters bypassed
    *apu_voice(v, 0x20) = 0;                     // CUR_PSL_START: BA = 0
    *apu_voice(v, 0x24) = 0;                     // CUR_PSH_SAMPLE: LBO = 0
    *apu_voice(v, 0x58) = 0;                     // PAR_OFFSET: CBO = 0
    *apu_voice(v, 0x5C) = RING_FRAMES - 1;       // PAR_NEXT: EBO
    // volumes are attenuations (0 = full, 0xFFF = silent): bins 0/1 full, rest off
    *apu_voice(v, 0x60) = 0xF | (0xF << 16);     // VOLA: volume0/1 = 0, volume6/7 low bits = F
    *apu_voice(v, 0x64) = (0xFFF << 4) | (0xFFFu << 20) | 0xF | (0xF << 16);
    *apu_voice(v, 0x68) = (0xFFF << 4) | (0xFFFu << 20) | 0xF | (0xF << 16);
    *apu_voice(v, 0x7C) = 0xFFFF;                // TAR_PITCH_LINK: pitch 0 (48 kHz), end of list
    *apu_voice(v, 0x54) = (1 << 21);             // PAR_STATE: active, envelopes off
    apu_wr(0x2000, 2 << 3);                      // SECTL: start the frame engine
}

static uint32_t apu_cbo() { return *apu_voice(APU_VOICE, 0x58) & 0xFFFFFF; }

static DWORD WINAPI apu_thread(LPVOID)
{
    uint64_t written = 0, played = 0;            // stereo frames, monotonic
    uint32_t last = apu_cbo();
    for (;;) {
        uint32_t cbo = apu_cbo();
        played += (cbo - last + RING_FRAMES) % RING_FRAMES;
        last = cbo;
        if (written < played)                    // fell behind: skip ahead
            written = (played + RING_CHUNK - 1) / RING_CHUNK * RING_CHUNK;
        while (written - played < RING_LEAD) {
            int pos = (int)(written % RING_FRAMES);
            music::mix((int16_t *)(ring + pos * 4), RING_CHUNK);
            written += RING_CHUNK;
            audio_calls++;
        }
        music::set_latency((int)(written - played));
        Sleep(4);
    }
    return 0;
}

// ------------------------------------------------------------ init
void init(int, char **)
{
    tfreq = (double)KeQueryPerformanceFrequency();
    t0 = KeQueryPerformanceCounter();
    log("init: start");
#ifdef S2R_DIAG
    {
        // RAM after a quick reboot is not zero: check the loader zeroed .bss
        unsigned nz = 0, fz = 0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 65536; j++) nz += vga::plane[i][j] != 0;
        const uint8_t *fp = (const uint8_t *)frames;
        for (size_t i = 0; i < sizeof frames; i++) fz += fp[i] != 0;
        log("diag: bss check: vga planes nonzero=%u frames nonzero=%u", nz, fz);
        char b[200];
        vga::debug_state(b, sizeof b);
        log("diag: vga %s", b);
    }
#endif
    BOOL vok = XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    {
        VIDEO_MODE vm = XVideoGetMode();
        unsigned char *fb = XVideoGetFB();
        log("init: video mode set ok=%d %dx%dx%d r%d fb=%08x phys=%08x crtc=%08x", (int)vok, vm.width, vm.height,
            vm.bpp, vm.refresh, (unsigned)fb, fb ? (unsigned)MmGetPhysicalAddress(fb) : 0u,
            (unsigned)VIDEOREG(0x00600800));
        if (!vok) {
            VIDEO_MODE m;
            void *it = nullptr;
            while (XVideoListModes(&m, 0, 0, &it)) log("  mode %dx%dx%d r%d", m.width, m.height, m.bpp, m.refresh);
        }
#ifdef S2R_DIAG
        // two second colour bars straight into nxdk's own framebuffer
        if (fb) {
            for (int y = 0; y < 480; y++) {
                uint32_t *d = (uint32_t *)(fb + y * 640 * 4);
                for (int x = 0; x < 640; x++)
                    d[x] = x < 213 ? 0xFFFF0000 : x < 426 ? 0xFF00FF00 : 0xFF0000FF;
            }
            asm __volatile__("sfence");
            log("diag: colour bars drawn");
            Sleep(2000);
        }
#endif
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    log("init: SDL ok");
    window = SDL_CreateWindow("Second Reality", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN);
    (void)window;
    fbs[0] = XVideoGetFB();
    for (int i = 1; i < 3; i++) {
        fbs[i] = (uint8_t *)MmAllocateContiguousMemoryEx(640 * 480 * 4, 0, 0x7FFFFFFF, 0x1000, PAGE_READWRITE | PAGE_WRITECOMBINE);
        memset(fbs[i], 0, 640 * 480 * 4);
    }
    for (int i = 0; i < 3; i++) s2r_fbinfo.phys[i] = (uint32_t)MmGetPhysicalAddress(fbs[i]);

    log("init: video ok");
    bool codec_ready = aci_init();
    use_apu = codec_ready && running_on_xemu();
    log("audio: %s%s", use_apu ? "APU voice (xemu)" : "AC97", codec_ready ? "" : " (codec not ready)");
    if (use_apu) {
        apu_init();
        HANDLE ha = CreateThread(nullptr, 64 * 1024, apu_thread, nullptr, 0, nullptr);
        SetThreadPriority(ha, THREAD_PRIORITY_HIGHEST);
    } else {
        for (int i = 0; i < ABUF_COUNT; i++) {
            abuf[i] = (uint8_t *)MmAllocateContiguousMemoryEx(ABUF_FRAMES * 4, 0, 0x03FFAFFF, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
            memset(abuf[i], 0, ABUF_FRAMES * 4);
        }
        HANDLE ha = CreateThread(nullptr, 64 * 1024, audio_thread, nullptr, 0, nullptr);
        SetThreadPriority(ha, THREAD_PRIORITY_HIGHEST);
    }

    use_vsync = !use_apu;                // real hardware (see present_thread)
#ifdef S2R_FORCE_VSYNC
    use_vsync = true;                    // test builds: exercise the vsync path in xemu
#endif
    HANDLE h = CreateThread(nullptr, 64 * 1024, present_thread, nullptr, 0, nullptr);
    SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
}

void shutdown()
{
    if (use_apu) apu_wr(0x2000, 0);
    aci_stop();
    XReboot();
    for (;;) Sleep(1000);
}

void idle() { SwitchToThread(); }

// ------------------------------------------------------------ input
static void pushkey(int k)
{
    unsigned h = kq_head.load();
    if (h - kq_tail.load() >= 32) return;
    keyq[h & 31] = k;
    kq_head = h + 1;
}

void poll_input()
{
    double t = now();
    if (t - last_poll < 0.004) return;
    last_poll = t;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:
            for (int i = 0; i < 4; i++)
                if (!pads[i]) { pads[i] = SDL_GameControllerOpen(e.cdevice.which); break; }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for (int i = 0; i < 4; i++)
                if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == e.cdevice.which) {
                    SDL_GameControllerClose(pads[i]);
                    pads[i] = nullptr;
                }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP: pushkey(KEY_UP); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: pushkey(KEY_DOWN); break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: pushkey(KEY_LEFT); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: pushkey(KEY_RIGHT); break;
            case SDL_CONTROLLER_BUTTON_A:
            case SDL_CONTROLLER_BUTTON_START: pushkey(KEY_ENTER); break;
            case SDL_CONTROLLER_BUTTON_B: pushkey(KEY_ESC); break;
            case SDL_CONTROLLER_BUTTON_BACK: pushkey(KEY_ESC); back_held = true; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: pushkey(KEY_SKIP); break;   // white: skip part
            default: break;
            }
            break;
        case SDL_CONTROLLERBUTTONUP:
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) back_held = false;
            break;
        default: break;
        }
    }
}

bool kbhit() { poll_input(); return kq_head.load() != kq_tail.load(); }

int peekkey()
{
    poll_input();
    unsigned t = kq_tail.load();
    if (t == kq_head.load()) return 0;
    int k = keyq[t & 31];
    kq_tail = t + 1;
    return k;
}

int getkey()
{
#ifdef S2R_AUTOSTART
    // test builds: confirm the setup menu by itself
    static bool once;
    if (!once) { once = true; double t = now(); while (now() - t < 2.0) Sleep(10); return KEY_ENTER; }
#endif
    for (;;) {
        int k = peekkey();
        if (k) return k;
        Sleep(5);
    }
}

bool ctrl_down() { return back_held.load(); }

// ------------------------------------------------------------ files
uint8_t *load_file(const char *name, uint32_t *size)
{
    char path[128] = "D:\\data\\";
    size_t n = strlen(path);
    for (const char *p = name; *p && n < sizeof path - 1; p++) path[n++] = (char)toupper(*p);
    path[n] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) { log("load_file: %s not found", path); return nullptr; }
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = (uint8_t *)malloc(l + 16);
    fread(b, 1, l, f);
    memset(b + l, 0, 16);
    fclose(f);
    if (size) *size = (uint32_t)l;
    return b;
}

} // namespace sys
