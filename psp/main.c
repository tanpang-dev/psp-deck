/* PSP Deck EBOOT: protocol v1 over deck.prx, buttons / stick → messages, GU + intraFont screen.
 * HOME exits. See docs/DESIGN.md §4.3-4.5 and §6. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <psppower.h>
#include <pspsdk.h>
#include <stdio.h>
#include <string.h>
#include "apps.h"
#include "deck_usb.h"
#include "ui.h"
#include "../proto/deck_proto.h"

PSP_MODULE_INFO("PSPDeck", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

/* deck.prx layout 0: vendor bulk link + UAC2 microphone. */
#define USB_LAYOUT 0
#define MIC_BLOCK 512             /* samples per sceAudioInput (multiple of 64): ~11.6 ms */
#define IDLE_DIM_US (60 * 1000000u)

static const unsigned g_assignable[] = {
    PSP_CTRL_TRIANGLE, PSP_CTRL_SQUARE, PSP_CTRL_CROSS, PSP_CTRL_START, PSP_CTRL_SELECT,
};
static const char *const g_button_names[] = { "△", "□", "×", "START", "SELECT" };
#define NBUTTONS (sizeof(g_assignable) / sizeof(g_assignable[0]))

/* What the Mac tells us to show; written by the link thread, read by the main loop under g_lock. */
typedef struct {
    unsigned pid;
    char app[64];
    char title[256];
    char labels[NBUTTONS][32];
} deck_view;

static volatile int g_run = 1;
static volatile int g_hello;          /* 0 = waiting, 1 = connected, -1 = version mismatch */
static volatile int g_mac_version, g_mouse, g_muted, g_mic_gain, g_mic_live;
static volatile int g_mic_blocks;
static SceUID g_lock;
static deck_view g_view;
static unsigned char g_rx[DECK_MAX_FRAME] __attribute__((aligned(64)));
static unsigned char g_tx_main[256] __attribute__((aligned(64)));
static unsigned char g_tx_link[256] __attribute__((aligned(64)));

static int exit_cb(int a, int b, void *c)
{
    g_run = 0;
    return 0;
}

static int cb_thread(SceSize a, void *p)
{
    sceKernelRegisterExitCallback(sceKernelCreateCallback("exit", exit_cb, NULL));
    sceKernelSleepThreadCB();
    return 0;
}

static void send(unsigned char *buf, uint8_t type, const void *payload, uint32_t len)
{
    int n = deck_frame(buf, type, payload, len);

    if (n > 0 && deckUsbWrite(buf, n) < 0)
        g_hello = 0;  /* the Mac app is gone; it says HELLO again when it comes back */
}

/* Copies the NUL-terminated string at *p (bounded by end) into dst; advances *p past it. */
static void take_string(const uint8_t **p, const uint8_t *end, char *dst, int size)
{
    int n = 0;

    while (*p < end && **p) {
        if (n < size - 1)
            dst[n++] = **p;
        (*p)++;
    }
    dst[n] = 0;
    if (*p < end)
        (*p)++;
}

static unsigned le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

static void on_status(const uint8_t *p, int len)
{
    const uint8_t *end = p + len;
    deck_view v;

    if (len < 4)
        return;
    sceKernelWaitSema(g_lock, 1, NULL);
    v = g_view;
    sceKernelSignalSema(g_lock, 1);
    v.pid = le32(p);
    p += 4;
    take_string(&p, end, v.app, sizeof(v.app));
    take_string(&p, end, v.title, sizeof(v.title));
    sceKernelWaitSema(g_lock, 1, NULL);
    g_view = v;
    sceKernelSignalSema(g_lock, 1);
}

static void on_labels(const uint8_t *p, int len)
{
    const uint8_t *end = p + len;
    char labels[NBUTTONS][32];
    int count, i;
    unsigned k;

    if (len < 4)
        return;
    memset(labels, 0, sizeof(labels));
    count = p[0];
    p += 4;
    for (i = 0; i < count && end - p >= 4; i++) {
        unsigned mask = le32(p);
        char text[32];

        p += 4;
        take_string(&p, end, text, sizeof(text));
        for (k = 0; k < NBUTTONS; k++)
            if (g_assignable[k] == mask)
                strcpy(labels[k], text);
    }
    sceKernelWaitSema(g_lock, 1, NULL);
    memcpy(g_view.labels, labels, sizeof(labels));
    sceKernelSignalSema(g_lock, 1);
}

static int link_thread(SceSize a, void *p)
{
    while (g_run) {
        deck_hdr h;
        const uint8_t *pl;
        int n = deckUsbRead(g_rx, sizeof(g_rx), 200), len;

        if (n < 0) {
            g_hello = 0;
            sceKernelDelayThread(100000);
            continue;
        }
        if (n == 0 || (len = deck_parse(g_rx, n, &h, &pl)) < 0)
            continue;
        switch (h.type) {
        case DECK_M_HELLO:
            if (len >= 2) {
                deck_usb_info in;
                uint16_t reply[2];

                g_mac_version = pl[0] | pl[1] << 8;
                deckUsbInfo(&in);
                reply[0] = DECK_PROTO_VERSION;
                reply[1] = in.speed;
                send(g_tx_link, DECK_P_HELLO, reply, sizeof(reply));
                g_mouse = g_muted = 0;
                g_hello = g_mac_version == DECK_PROTO_VERSION ? 1 : -1;
            }
            break;
        case DECK_M_MODE:
            if (len >= 2) {
                g_mouse = pl[0];
                g_muted = pl[1];
                if (len >= 3)
                    g_mic_gain = (int8_t)pl[2];
            }
            break;
        case DECK_M_STATUS:
            on_status(pl, len);
            break;
        case DECK_M_LABELS:
            on_labels(pl, len);
            break;
        case DECK_M_APP:
            apps_on_message(pl, len);
            break;
        }
    }
    return 0;
}

static volatile int g_usb_ready;  /* deck.prx is loaded, so deckUsbInfo / deckMicPush may be called */

/* Captures the built-in mic continuously from startup and hands every block to deck.prx (which only streams
 * it while the Mac has the input open). The input must never stop: once a 64-sample hardware chunk passes with
 * no request pending, the next sceAudioInput redoes the DMA setup and codec programming over I2C, and that
 * hangs while iso packets are flowing. sceAudioInputBlocking waits for the previous block and then queues the
 * next one before returning, so ping-ponging two buffers always leaves a request queued. */
static int mic_thread(SceSize a, void *p)
{
    static short buf[2][MIC_BLOCK] __attribute__((aligned(64)));
    int cur = 0, gain = g_mic_gain;

    sceAudioInputBlocking(MIC_BLOCK, 44100, buf[cur]);
    while (g_run) {
        short *done = buf[cur];

        /* Returns once `done` is full, with the other buffer already queued. */
        int ret = sceAudioInputBlocking(MIC_BLOCK, 44100, buf[cur ^ 1]);
        cur ^= 1;
        if (ret < 0) {
            sceKernelDelayThread(10000);
            continue;
        }
        g_mic_blocks++;
        /* Reprogramming the codec gain is I2C traffic too, so only while nothing is streaming. */
        if (gain != g_mic_gain && !g_mic_live) {
            gain = g_mic_gain;
            sceAudioInputInit(0, gain, 0);
        }
        if (!g_usb_ready)
            continue;
        if (g_muted)
            memset(done, 0, MIC_BLOCK * sizeof(short));
        deckMicPush(done, MIC_BLOCK);
    }
    return 0;
}

static void send_button(unsigned mask, int down)
{
    uint8_t p[8] = { mask, mask >> 8, mask >> 16, mask >> 24, down };

    send(g_tx_main, DECK_P_BUTTON, p, sizeof(p));
}

static void send_activate(unsigned pid)
{
    uint8_t p[4] = { pid, pid >> 8, pid >> 16, pid >> 24 };

    send(g_tx_main, DECK_P_APP_ACTIVATE, p, sizeof(p));
}

static void send_tab_step(int dir)
{
    int8_t p[4] = { dir };

    send(g_tx_main, DECK_P_TAB_STEP, p, sizeof(p));
}

/* L / R act on release; pressing both cancels them instead of stepping back and forth. */
static void handle_lr(unsigned held, unsigned released)
{
    static int combo;

    if ((held & PSP_CTRL_LTRIGGER) && (held & PSP_CTRL_RTRIGGER))
        combo = 1;
    if ((released & PSP_CTRL_LTRIGGER) && !combo)
        send_tab_step(-1);
    if ((released & PSP_CTRL_RTRIGGER) && !combo)
        send_tab_step(1);
    if (!(held & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)))
        combo = 0;
}

static void handle_input(const SceCtrlData *pad, unsigned prev)
{
    static int last_x = -1, last_y = -1;
    unsigned b = pad->Buttons, pressed = b & ~prev, released = prev & ~b;
    unsigned i;

    for (i = 0; i < NBUTTONS; i++) {
        if (pressed & g_assignable[i])
            send_button(g_assignable[i], 1);
        if (released & g_assignable[i])
            send_button(g_assignable[i], 0);
    }
    /* In mouse mode CIRCLE is the left click (CROSS, already sent above, is the right click). */
    if (g_mouse && (pressed & PSP_CTRL_CIRCLE))
        send_button(PSP_CTRL_CIRCLE, 1);
    if (released & PSP_CTRL_CIRCLE)
        send_button(PSP_CTRL_CIRCLE, 0);
    handle_lr(b, released);
    if (pad->Lx != last_x || pad->Ly != last_y) {
        uint8_t p[4] = { pad->Lx, pad->Ly };

        send(g_tx_main, DECK_P_STICK, p, sizeof(p));
        last_x = pad->Lx;
        last_y = pad->Ly;
    }
}

/* True if the user touched anything this frame (the stick counts once it leaves the center). */
static int any_input(const SceCtrlData *pad, unsigned prev)
{
    int dx = pad->Lx - 128, dy = pad->Ly - 128;

    return (pad->Buttons & ~prev) || dx * dx + dy * dy > 50 * 50;
}

#define C_BG     UI_RGB(18, 20, 28)
#define C_BAR    UI_RGB(34, 38, 52)
#define C_TEXT   UI_RGB(235, 238, 245)
#define C_DIM    UI_RGB(140, 148, 165)
#define C_ACCENT UI_RGB(110, 190, 255)
#define C_WARN   UI_RGB(255, 170, 90)

static unsigned g_drops;  /* frames that missed their vblank: the carousel must keep this at 0 */

static void draw(const deck_view *v, int usb_ok)
{
    char line[96];
    float x;
    unsigned i;

    ui_begin(C_BG);

    /* top bar: app ─ window title, then status badges on the right */
    ui_rect(0, 0, UI_W, 24, C_BAR);
    x = UI_W - 6;
    if (scePowerIsBatteryCharging()) {
        x -= 34;
        ui_text(x, 17, 0.6f, C_ACCENT, 40, "充電");
    }
    snprintf(line, sizeof(line), "%d%%", scePowerGetBatteryLifePercent());
    x -= 36;
    ui_text(x, 17, 0.6f, C_DIM, 40, line);
    if (g_mouse) {
        x -= 50;
        ui_text(x, 17, 0.6f, C_WARN, 50, "MOUSE");
    }
    if (g_mic_live) {
        x -= 44;
        ui_text(x, 17, 0.6f, g_muted ? C_DIM : UI_RGB(255, 90, 90), 44, g_muted ? "MUTE" : "● MIC");
    }
    if (g_drops) {
        snprintf(line, sizeof(line), "drop %u", g_drops);
        x -= 60;
        ui_text(x, 17, 0.5f, C_WARN, 60, line);
    }
    if (g_hello == 1 && v->app[0]) {
        float w = ui_text(8, 17, 0.7f, C_ACCENT, 140, v->app);
        if (v->title[0]) {
            ui_text(8 + w, 17, 0.7f, C_DIM, 30, " ─ ");
            ui_text(8 + w + 22, 17, 0.7f, C_TEXT, x - 8 - w - 30, v->title);
        }
    } else {
        ui_text(8, 17, 0.7f, C_TEXT, x - 16, "PSP Deck");
    }

    /* center: the app carousel, or the connection state until there is one */
    if (g_hello == 1 && apps_count() > 0)
        apps_draw(v->pid);
    else if (!usb_ok)
        ui_text(24, 130, 0.9f, C_WARN, UI_W - 48, "USB ドライバを開始できませんでした");
    else if (g_hello == 1)
        ui_text(24, 130, 1.0f, C_TEXT, UI_W - 48, v->app[0] ? v->app : "接続しました");
    else if (g_hello < 0) {
        snprintf(line, sizeof(line), "Mac アプリのバージョンが違います（Mac v%d / PSP v%d）", g_mac_version,
                 DECK_PROTO_VERSION);
        ui_text(24, 130, 0.8f, C_WARN, UI_W - 48, line);
    } else
        ui_text(24, 130, 0.9f, C_DIM, UI_W - 48, "Mac の PSP Deck を待っています…");

    /* bottom bar: what each button does */
    ui_rect(0, UI_H - 26, UI_W, 26, C_BAR);
    for (i = 0; i < NBUTTONS; i++) {
        float cx = 6 + i * (UI_W / NBUTTONS);
        float w = ui_text(cx, UI_H - 8, 0.6f, C_ACCENT, 40, g_button_names[i]);
        ui_text(cx + w + 4, UI_H - 8, 0.55f, C_TEXT, UI_W / NBUTTONS - w - 12,
                g_hello == 1 && v->labels[i][0] ? v->labels[i] : "—");
    }

    ui_end();
}

int main(int argc, char **argv)
{
    char path[256], *slash;
    SceCtrlData pad;
    deck_view view;
    unsigned prev = 0, last_input, last_frame = 0;
    int mod, start = -1, dimmed = 0;

    sceKernelStartThread(sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, NULL), 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    g_lock = sceKernelCreateSema("view", 0, 1, 1, NULL);
    /* Start capturing before USB comes up (see mic_thread), and give the input a moment to get going. */
    sceAudioInputInit(0, g_mic_gain, 0);
    sceKernelStartThread(sceKernelCreateThread("mic", mic_thread, 0x12, 0x4000, 0, NULL), 0, NULL);
    {
        int waited;
        for (waited = 0; waited < 50 && !g_mic_blocks; waited++)
            sceKernelDelayThread(10000);
    }
    apps_init();
    ui_init();

    snprintf(path, sizeof(path), "%s", argv[0]);
    slash = strrchr(path, '/');
    snprintf(slash ? slash + 1 : path, sizeof(path) - (slash ? slash + 1 - path : 0), "deck.prx");
    mod = pspSdkLoadStartModule(path, PSP_MEMORY_PARTITION_KERNEL);
    if (mod >= 0) {
        deckUsbFirstLayout(USB_LAYOUT);
        start = deckUsbStart();
        if (start >= 0) {
            sceKernelStartThread(sceKernelCreateThread("link", link_thread, 0x18, 0x4000, 0, NULL), 0, NULL);
            g_usb_ready = 1;
        }
    }

    last_input = sceKernelGetSystemTimeLow();
    while (g_run) {
        sceCtrlPeekBufferPositive(&pad, 1);
        if (any_input(&pad, prev)) {
            last_input = sceKernelGetSystemTimeLow();
            if (dimmed) {
                /* Waking the screen is all this press does. */
                deckSetBacklight(1);
                dimmed = 0;
                prev = pad.Buttons;
                continue;
            }
        } else if (!dimmed && mod >= 0 && sceKernelGetSystemTimeLow() - last_input > IDLE_DIM_US) {
            deckSetBacklight(0);
            dimmed = 1;
        }
        sceKernelWaitSema(g_lock, 1, NULL);
        view = g_view;
        sceKernelSignalSema(g_lock, 1);
        if (g_usb_ready) {
            deck_usb_info in;
            deckUsbInfo(&in);
            g_mic_live = in.mic_alt != 0;  /* the Mac has the PSP input open */
        }
        apps_frame_begin();
        if (g_hello == 1) {
            /* In mouse mode CIRCLE is a click, so it does not pick an app. */
            unsigned go = apps_update(pad.Buttons & ~prev, pad.Buttons, !g_mouse, view.pid);
            if (go)
                send_activate(go);
            handle_input(&pad, prev);
        }
        prev = pad.Buttons;

        draw(&view, mod >= 0 && start >= 0);  /* waits for vblank */
        if (last_frame && sceKernelGetSystemTimeLow() - last_frame > 20000)
            g_drops++;
        last_frame = sceKernelGetSystemTimeLow();
    }

    if (dimmed)
        deckSetBacklight(1);
    g_usb_ready = 0;
    if (mod >= 0)
        deckUsbStop();
    ui_term();
    sceKernelExitGame();
    return 0;
}
