/* PSP Deck EBOOT (phase 1): talks protocol v1 over deck.prx and turns buttons / stick into messages.
 * HOME exits. See docs/DESIGN.md §4.3-4.4 and §6. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspsdk.h>
#include <stdio.h>
#include <string.h>
#include "deck_usb.h"
#include "../proto/deck_proto.h"

PSP_MODULE_INFO("PSPDeck", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define printf pspDebugScreenPrintf

/* deck.prx layout 2 = bulk only. The microphone (layout 0, UAC2) is switched on in phase 5. */
#define USB_LAYOUT 2

static const unsigned g_assignable[] = {
    PSP_CTRL_TRIANGLE, PSP_CTRL_SQUARE, PSP_CTRL_CROSS, PSP_CTRL_START, PSP_CTRL_SELECT,
};

static volatile int g_run = 1;
static volatile int g_hello;          /* 0 = waiting, 1 = connected, -1 = version mismatch */
static volatile int g_mac_version, g_mouse, g_muted;
static unsigned char g_rx[DECK_MAX_FRAME] __attribute__((aligned(64)));
static unsigned char g_tx_main[256] __attribute__((aligned(64)));
static unsigned char g_tx_link[256] __attribute__((aligned(64)));
static char g_last[48] = "-";

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
        if (h.type == DECK_M_HELLO && len >= 2) {
            deck_usb_info in;
            uint16_t reply[2];

            g_mac_version = pl[0] | pl[1] << 8;
            deckUsbInfo(&in);
            reply[0] = DECK_PROTO_VERSION;
            reply[1] = in.speed;
            send(g_tx_link, DECK_P_HELLO, reply, sizeof(reply));
            g_mouse = g_muted = 0;
            g_hello = g_mac_version == DECK_PROTO_VERSION ? 1 : -1;
        } else if (h.type == DECK_M_MODE && len >= 2) {
            g_mouse = pl[0];
            g_muted = pl[1];
        }
    }
    return 0;
}

static void send_button(unsigned mask, int down)
{
    uint8_t p[8] = { mask, mask >> 8, mask >> 16, mask >> 24, down };

    send(g_tx_main, DECK_P_BUTTON, p, sizeof(p));
    snprintf(g_last, sizeof(g_last), "button %06X %s", mask, down ? "down" : "up");
}

static void send_tab_step(int dir)
{
    int8_t p[4] = { dir };

    send(g_tx_main, DECK_P_TAB_STEP, p, sizeof(p));
    snprintf(g_last, sizeof(g_last), "tab %s", dir > 0 ? "next" : "prev");
}

/* L / R act on release so that L+R can later open the tab list without a timer (DESIGN §4.4). */
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

    for (i = 0; i < sizeof(g_assignable) / sizeof(g_assignable[0]); i++) {
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

static void draw(int mod, int start)
{
    deck_usb_info in;
    const char *state = g_hello == 1 ? "connected" : g_hello < 0 ? "VERSION MISMATCH - update the Mac app" :
                        "waiting for the Mac app...";

    memset(&in, 0, sizeof(in));
    deckUsbInfo(&in);
    pspDebugScreenSetXY(0, 0);
    printf("PSP Deck  (protocol v%d)\n\n", DECK_PROTO_VERSION);
    if (mod < 0 || start < 0) {
        printf("USB driver failed: load %08X start %08X\n", mod, start);
        return;
    }
    printf("USB   : %-12s %s                    \n", in.speed == 2 ? "hi-speed" : in.speed == 1 ? "full-speed" :
           "cable?", state);
    if (g_hello < 0)
        printf("        Mac app speaks v%d                \n", g_mac_version);
    else
        printf("                                          \n");
    printf("mode  : %-6s  mic %s                      \n", g_mouse ? "MOUSE" : "keys", g_muted ? "muted" : "on");
    printf("power : %d%% %s                           \n", scePowerGetBatteryLifePercent(),
           scePowerIsBatteryCharging() ? "charging" : "");
    printf("last  : %-40s\n\n", g_last);
    printf("HOME: exit\n");
}

int main(int argc, char **argv)
{
    char path[256], *slash;
    SceCtrlData pad;
    unsigned prev = 0;
    int mod, start = -1, frame = 0;

    pspDebugScreenInit();
    sceKernelStartThread(sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, NULL), 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    snprintf(path, sizeof(path), "%s", argv[0]);
    slash = strrchr(path, '/');
    snprintf(slash ? slash + 1 : path, sizeof(path) - (slash ? slash + 1 - path : 0), "deck.prx");
    mod = pspSdkLoadStartModule(path, PSP_MEMORY_PARTITION_KERNEL);
    if (mod >= 0) {
        deckUsbFirstLayout(USB_LAYOUT);
        start = deckUsbStart();
        if (start >= 0)
            sceKernelStartThread(sceKernelCreateThread("link", link_thread, 0x18, 0x4000, 0, NULL), 0, NULL);
    }

    while (g_run) {
        sceCtrlReadBufferPositive(&pad, 1);  /* paces the loop at the controller sampling rate (~60 Hz) */
        if (g_hello == 1)
            handle_input(&pad, prev);
        prev = pad.Buttons;
        if (frame++ % 10 == 0)
            draw(mod, start);
    }

    if (mod >= 0)
        deckUsbStop();
    sceKernelExitGame();
    return 0;
}
