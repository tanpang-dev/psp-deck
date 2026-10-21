/* M1 spike: built-in mic capture, optionally after starting deck.prx, to find what makes sceAudioInput hang
 * inside PSP Deck. Records 3 s at 44.1 kHz to ms0:/mictest.wav. HOME exits.
 * CROSS: record (blocking)  SQUARE: record (poll)  TRIANGLE: start USB bulk-only  CIRCLE: start USB UAC2 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspsdk.h>
#include "deck_usb.h"
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("MicTest", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define printf pspDebugScreenPrintf
#define RATE 44100
#define BLOCK 1024
#define BLOCKS (RATE * 3 / BLOCK)

static short g_rec[BLOCKS * BLOCK] __attribute__((aligned(64)));
static volatile int g_run = 1, g_mode, g_blocks, g_peak, g_ret, g_busy, g_polls, g_stage;
static int g_usb_mod = 0, g_usb_start = 0, g_usb_layout = -1;

static int exit_cb(int a, int b, void *c) { g_run = 0; return 0; }
static int cb_thread(SceSize a, void *p)
{
    sceKernelRegisterExitCallback(sceKernelCreateCallback("exit", exit_cb, NULL));
    sceKernelSleepThreadCB();
    return 0;
}

static void save_wav(void)
{
    struct { char riff[4]; int size; char wave[4], fmt[4]; int fmtlen; short pcm, ch; int rate, bps;
             short align, bits; char data[4]; int datalen; } h = {
        "RIFF", 36 + (int)sizeof(g_rec), "WAVE", "fmt ", 16, 1, 1, RATE, RATE * 2, 2, 16, "data", sizeof(g_rec) };
    SceUID fd = sceIoOpen("ms0:/mictest.wav", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &h, 44);
        sceIoWrite(fd, g_rec, sizeof(g_rec));
        sceIoClose(fd);
    }
}

static int rec_thread(SceSize a, void *p)
{
    int b, k;

    g_busy = 1;
    g_blocks = g_peak = g_polls = 0;
    g_ret = 0;
    for (b = 0; b < BLOCKS && g_run; b++) {
        short *dst = g_rec + b * BLOCK;
        g_stage = 1;  /* inside the input call */
        if (g_mode == 0) {
            g_ret = sceAudioInputBlocking(BLOCK, RATE, dst);
        } else {
            g_ret = sceAudioInput(BLOCK, RATE, dst);
            g_stage = 2;  /* polling */
            while (g_run && sceAudioPollInputEnd() == 1) {   /* 1 = still busy */
                g_polls++;
                sceKernelDelayThread(1000);
            }
        }
        g_stage = 0;
        if (g_ret < 0)
            break;
        for (k = 0; k < BLOCK; k++) {
            int v = dst[k] < 0 ? -dst[k] : dst[k];
            if (v > g_peak)
                g_peak = v;
        }
        g_blocks++;
    }
    save_wav();
    g_busy = 0;
    return 0;
}

int main(void)
{
    SceCtrlData pad;
    unsigned prev = 0;
    int init;

    pspDebugScreenInit();
    sceKernelStartThread(sceKernelCreateThread("cb", cb_thread, 0x11, 0x1000, 0, NULL), 0, NULL);
    init = sceAudioInputInit(0, 0, 0);
    while (g_run) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned now = pad.Buttons & ~prev;
        prev = pad.Buttons;
        if (!g_busy && g_usb_layout < 0 && (now & (PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE))) {
            g_usb_layout = (now & PSP_CTRL_TRIANGLE) ? 2 : 0;
            g_usb_mod = pspSdkLoadStartModule("ms0:/PSP/GAME/PSPDECK/deck.prx", PSP_MEMORY_PARTITION_KERNEL);
            if (g_usb_mod >= 0) {
                deckUsbFirstLayout(g_usb_layout);
                g_usb_start = deckUsbStart();
            }
        }
        if (!g_busy && (now & (PSP_CTRL_CROSS | PSP_CTRL_SQUARE))) {
            g_mode = (now & PSP_CTRL_SQUARE) ? 1 : 0;
            sceKernelStartThread(sceKernelCreateThread("rec", rec_thread, 0x12, 0x4000, 0, NULL), 0, NULL);
        }
        pspDebugScreenSetXY(0, 0);
        printf("PSP Deck M1 mic test (no USB)\n\n");
        printf("init %08X\n", init);
        printf("CROSS: record 3 s (blocking)   SQUARE: record 3 s (poll)\n");
        printf("TRIANGLE: start USB bulk-only  CIRCLE: start USB UAC2\n\n");
        if (g_usb_layout >= 0) {
            deck_usb_info in;
            memset(&in, 0, sizeof(in));
            if (g_usb_mod >= 0)
                deckUsbInfo(&in);
            printf("USB layout %d  load %08X  start %08X  speed %d  mic alt %d   \n", g_usb_layout, g_usb_mod,
                   g_usb_start, in.speed, in.mic_alt);
        } else
            printf("USB not started                                      \n");
        printf("mode %s  %s                 \n", g_mode ? "poll" : "blocking", g_busy ? "RECORDING..." : "idle");
        printf("blocks %d / %d  last ret %08X  polls %d  peak %d  stage %d     \n", g_blocks, BLOCKS, g_ret, g_polls,
               g_peak, g_stage);
        printf("\nSaved to ms0:/mictest.wav after each run. HOME: exit\n");
        sceKernelDelayThread(50000);
    }
    if (g_usb_mod >= 0 && g_usb_layout >= 0)
        deckUsbStop();
    sceKernelExitGame();
    return 0;
}
