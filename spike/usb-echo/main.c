/* S1 spike: pick a descriptor layout, load deck.prx, echo every bulk frame back and show link / mic /
 * charging diagnostics. START+SELECT or HOME exits. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <pspsdk.h>
#include <stdio.h>
#include <string.h>
#include "deck_usb.h"

PSP_MODULE_INFO("DeckEcho", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define printf pspDebugScreenPrintf

static unsigned char g_buf[65536] __attribute__((aligned(64)));
static volatile int g_run = 1;
static int g_echoed, g_last_read, g_last_write;
static unsigned g_bytes;
static int g_iso_mode = DECK_ISO_SINGLE;

/* Same order as deck_variants in psp/prx/descriptors.c */
static const char *const g_layouts[] = {
    "UAC2 mic, hs 125us", "vendor iso, hs 125us (diag)", "bulk only",
};
#define NLAYOUTS ((int)(sizeof(g_layouts) / sizeof(g_layouts[0])))

static int choose_layout(void)
{
    SceCtrlData pad;
    unsigned prev = ~0u;
    int sel = 0, i;

    for (;;) {
        pspDebugScreenSetXY(0, 0);
        printf("PSP Deck S1 spike - choose USB layout\n\n");
        for (i = 0; i < NLAYOUTS; i++)
            printf(" %c %d: %s          \n", i == sel ? '>' : ' ', i, g_layouts[i]);
        printf("\nUP/DOWN: select   CIRCLE: start\n");
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned now = pad.Buttons & ~prev;
        prev = pad.Buttons;
        if (now & PSP_CTRL_UP)
            sel = (sel + NLAYOUTS - 1) % NLAYOUTS;
        if (now & PSP_CTRL_DOWN)
            sel = (sel + 1) % NLAYOUTS;
        if (now & PSP_CTRL_CIRCLE)
            break;
        if (!g_run)
            break;
        sceKernelDelayThread(30000);
    }
    pspDebugScreenClear();
    return sel;
}

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

static int echo_thread(SceSize a, void *p)
{
    while (g_run) {
        int n = deckUsbRead(g_buf, sizeof(g_buf), 100);
        if (n > 0) {
            g_last_read = n;
            g_last_write = deckUsbWrite(g_buf, n);
            if (g_last_write > 0) {
                g_echoed++;
                g_bytes += n;
            }
        } else if (n < 0) {
            g_last_read = n;
            sceKernelDelayThread(100000);
        }
    }
    return 0;
}

static void show(const char *prx, int mod, int start)
{
    deck_usb_info in;
    int i;

    memset(&in, 0, sizeof(in));
    deckUsbInfo(&in);
    pspDebugScreenSetXY(0, 0);
    printf("PSP Deck S1 spike  [%s] %s\n", prx, g_layouts[in.variant < NLAYOUTS ? in.variant : 0]);
    printf("load %08X  start %08X\n", mod, start);
    printf("register %08X bus %08X drv %08X act %08X\n", in.ret_register, in.ret_bus, in.ret_driver, in.ret_activate);
    printf("charge syscon %08X power %08X  charging=%d batt=%d%%   \n",
           in.ret_charge_syscon, in.ret_charge_power, scePowerIsBatteryCharging(), scePowerGetBatteryLifePercent());
    printf("layout %d  tried:", in.variant);
    for (i = 0; i <= in.variant && i < 8; i++)
        printf(" %d=%08X", i, in.variant_ret[i]);
    printf("\n");
    printf("speed %d (%s)            \n", in.speed, in.speed == 2 ? "hi 480M" : in.speed == 1 ? "full 12M" : "detached");
    printf("rx %d  tx %d  tx_timeouts %d          \n", in.rx_frames, in.tx_frames, in.tx_timeouts);
    printf("echoed %d  bytes %u  last read %d write %d          \n", g_echoed, g_bytes, g_last_read, g_last_write);
    printf("mic alt %d  ep3=%d  iso sent %d  err %d  last %08X  stalls %d    \n", in.mic_alt, in.ep3_num,
           in.iso_sent, in.iso_err, in.iso_last_ret, in.iso_timeouts);
    printf("mode: [TRIANGLE] fixed90=%d  [SQUARE] single=%d      \n", !!(g_iso_mode & DECK_ISO_FIXED),
           !!(g_iso_mode & DECK_ISO_SINGLE));
    printf("iso log (ret / bytes / us), newest first:\n");
    for (i = 0; i < 8; i++) {
        int *e = in.iso_log[(in.iso_nlog - 1 - i) & 7];
        if (i < in.iso_nlog)
            printf("  %08X %4d %8d          \n", e[0], e[1], e[2]);
        else
            printf("                                   \n");
    }
    printf("ctrl %d  clock %d  ep0 sent %d done %d ret %08X  behind %d\n", in.nreq, in.uac_answered,
           in.ep0_sent, in.ep0_done, in.ep0_ret, in.iso_behind);
    for (i = 0; i < 3 && i < in.nreq; i++) {
        unsigned char *r = in.req_log[(in.nreq - 1 - i) & 7];
        printf("  %02X %02X %02X%02X %02X%02X %02X%02X\n", r[0], r[1], r[3], r[2], r[5], r[4], r[7], r[6]);
    }
    printf("START+SELECT: exit\n");
}

int main(int argc, char **argv)
{
    char path[256];
    const char *name;
    SceCtrlData pad;
    int mod, start = -1, first;
    unsigned prev = 0;
    char *slash;

    pspDebugScreenInit();
    sceKernelStartThread(sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, NULL), 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlReadBufferPositive(&pad, 1);
    name = "deck.prx";
    first = choose_layout();

    snprintf(path, sizeof(path), "%s", argv[0]);
    slash = strrchr(path, '/');
    snprintf(slash ? slash + 1 : path, sizeof(path) - (slash ? slash + 1 - path : 0), "%s", name);

    mod = pspSdkLoadStartModule(path, PSP_MEMORY_PARTITION_KERNEL);
    if (mod >= 0) {
        deckUsbFirstLayout(first);
        deckUsbIsoMode(g_iso_mode);
        start = deckUsbStart();
        sceKernelStartThread(sceKernelCreateThread("echo", echo_thread, 0x18, 0x4000, 0, NULL), 0, NULL);
    }

    while (g_run) {
        if (mod >= 0)
            show(name, mod, start);
        else {
            pspDebugScreenSetXY(0, 0);
            printf("load %s failed: %08X\n%s\n", name, mod, path);
        }
        sceCtrlReadBufferPositive(&pad, 1);
        if (mod >= 0 && (pad.Buttons & ~prev & (PSP_CTRL_TRIANGLE | PSP_CTRL_SQUARE))) {
            if (pad.Buttons & ~prev & PSP_CTRL_TRIANGLE)
                g_iso_mode ^= DECK_ISO_FIXED;
            if (pad.Buttons & ~prev & PSP_CTRL_SQUARE)
                g_iso_mode ^= DECK_ISO_SINGLE;
            deckUsbIsoMode(g_iso_mode);
        }
        prev = pad.Buttons;
        if ((pad.Buttons & (PSP_CTRL_START | PSP_CTRL_SELECT)) == (PSP_CTRL_START | PSP_CTRL_SELECT))
            g_run = 0;
        sceKernelDelayThread(100000);
    }

    if (mod >= 0)
        deckUsbStop();
    sceKernelExitGame();
    return 0;
}
