/* S1 spike: loads deck.prx (hold X at launch for deck_nouac.prx), echoes every bulk frame back and
 * shows link / mic / charging diagnostics. START+SELECT or HOME exits. */
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
    printf("PSP Deck S1 spike  [%s]\n\n", prx);
    printf("load %08X  start %08X\n", mod, start);
    printf("register %08X bus %08X drv %08X act %08X\n", in.ret_register, in.ret_bus, in.ret_driver, in.ret_activate);
    printf("charge syscon %08X power %08X  charging=%d batt=%d%%   \n\n",
           in.ret_charge_syscon, in.ret_charge_power, scePowerIsBatteryCharging(), scePowerGetBatteryLifePercent());
    printf("speed %d (%s)            \n", in.speed, in.speed == 2 ? "hi 480M" : in.speed == 1 ? "full 12M" : "detached");
    printf("rx %d  tx %d  tx_timeouts %d          \n", in.rx_frames, in.tx_frames, in.tx_timeouts);
    printf("echoed %d  bytes %u  last read %d write %d          \n\n", g_echoed, g_bytes, g_last_read, g_last_write);
    printf("mic alt %d  iso sent %d  err %d  last %08X       \n\n", in.mic_alt, in.iso_sent, in.iso_err, in.iso_last_ret);
    printf("control requests: %d\n", in.nreq);
    for (i = 0; i < 8 && i < in.nreq; i++) {
        unsigned char *r = in.req_log[(in.nreq - 1 - i) & 7];
        printf("  %02X %02X %02X%02X %02X%02X %02X%02X\n", r[0], r[1], r[3], r[2], r[5], r[4], r[7], r[6]);
    }
    printf("\nSTART+SELECT: exit\n");
}

int main(int argc, char **argv)
{
    char path[256];
    const char *name;
    SceCtrlData pad;
    int mod, start = -1;
    char *slash;

    pspDebugScreenInit();
    sceKernelStartThread(sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, NULL), 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlReadBufferPositive(&pad, 1);
    name = (pad.Buttons & PSP_CTRL_CROSS) ? "deck_nouac.prx" : "deck.prx";

    snprintf(path, sizeof(path), "%s", argv[0]);
    slash = strrchr(path, '/');
    snprintf(slash ? slash + 1 : path, sizeof(path) - (slash ? slash + 1 - path : 0), "%s", name);

    mod = pspSdkLoadStartModule(path, PSP_MEMORY_PARTITION_KERNEL);
    if (mod >= 0) {
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
        if ((pad.Buttons & (PSP_CTRL_START | PSP_CTRL_SELECT)) == (PSP_CTRL_START | PSP_CTRL_SELECT))
            g_run = 0;
        sceKernelDelayThread(100000);
    }

    if (mod >= 0)
        deckUsbStop();
    sceKernelExitGame();
    return 0;
}
