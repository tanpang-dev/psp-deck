/* deck.prx: vendor bulk link + UAC1 microphone on one USB device. Transfer handling follows
 * PSPLink usbhostfs (BSD); see docs/DESIGN.md §4.2. */
#include <pspkernel.h>
#include <pspsdk.h>
#include <string.h>
#include "descriptors.h"
#include "deck_usb.h"
#include "../../proto/deck_proto.h"

PSP_MODULE_INFO("DeckUsb", PSP_MODULE_KERNEL, 1, 0);

#define DRIVER_NAME "PSPDeckDriver"
#define TX_TIMEOUT_US 500000

int sceSysconPermitChargeBattery(void);
int scePowerBatteryPermitCharging(void);

enum {
    EV_RX = 1, EV_TX = 2, EV_ISO0 = 4, EV_ISO1 = 8,
    EV_MIC_ON = 16, EV_MIC_OFF = 32, EV_QUIT = 64,
};

static struct UsbEndpoint g_ep[4] = { {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0} };
static struct UsbInterface g_if = { -1, 0, 1 + 2 * DECK_UAC };

static SceUID g_ev = -1, g_tx_sema = -1, g_mic_thid = -1;
static struct UsbbdDeviceRequest g_rx_req, g_tx_req;
static int g_rx_pending;
static unsigned char g_rx_buf[DECK_MAX_FRAME] __attribute__((aligned(64)));
static unsigned char g_tx_buf[DECK_MAX_FRAME] __attribute__((aligned(64)));
static deck_usb_info g_info;
static int g_started;

static int user_ok(const void *p, int len)
{
    u32 a = (u32)p;
    return p && len >= 0 && a + (u32)len >= a && !((a | (a + (u32)len)) & 0x80000000);
}

static void on_rx(struct UsbbdDeviceRequest *req) { sceKernelSetEventFlag(g_ev, EV_RX); }
static void on_tx(struct UsbbdDeviceRequest *req) { sceKernelSetEventFlag(g_ev, EV_TX); }

static void set_mic(int alt)
{
    if (alt == g_info.mic_alt)
        return;
    g_info.mic_alt = alt;
    sceKernelSetEventFlag(g_ev, alt ? EV_MIC_ON : EV_MIC_OFF);
}

static int process_request(int recipient, int arg, struct DeviceRequest *req)
{
    memcpy(g_info.req_log[g_info.nreq & 7], req, 8);
    g_info.nreq++;
    /* macOS may switch alt settings via a plain SET_INTERFACE seen here (cf. psp-uvc). */
    if (req->bmRequestType == 0x01 && req->bRequest == 0x0B && req->wIndex == 2)
        set_mic(req->wValue);
    return 0;
}

static int change_setting(int interface, int alt)
{
    if (interface == 2)
        set_mic(alt);
    return 0;
}

static int attach(int speed)
{
    g_info.speed = speed;
    return 0;
}

static void detach(void)
{
    g_info.speed = 0;
    set_mic(0);
    sceUsbbdReqCancelAll(&g_ep[1]);
    sceUsbbdReqCancelAll(&g_ep[2]);
}

static void configure(int speed, int count, struct InterfaceSettings *settings) {}
static int drv_start(int size, void *args) { return 0; }
static int drv_stop(int size, void *args) { detach(); return 0; }

static struct UsbDriver g_driver = {
    DRIVER_NAME, 3 + DECK_UAC, g_ep, &g_if,
    &deck_dev_hi, &deck_config_hi, &deck_dev_full, &deck_config_full,
    NULL, process_request, change_setting, attach, detach, configure,
    drv_start, drv_stop, NULL,
};

/* ---- microphone (phase 0: 882 Hz test tone, exactly 50 samples per period) ---- */

static const short g_tone[50] = {
    0, 1504, 2984, 4417, 5781, 7053, 8215, 9246, 10132, 10858, 11413, 11787, 11976, 11976, 11787,
    11413, 10858, 10132, 9246, 8215, 7053, 5781, 4417, 2984, 1504, 0, -1504, -2984, -4417, -5781,
    -7053, -8215, -9246, -10132, -10858, -11413, -11787, -11976, -11976, -11787, -11413, -10858,
    -10132, -9246, -8215, -7053, -5781, -4417, -2984, -1504,
};

static struct UsbbdDeviceRequest g_iso_req[2];
static short g_iso_buf[2][32] __attribute__((aligned(64)));
static int g_tone_pos, g_rate_acc;

static void on_iso0(struct UsbbdDeviceRequest *req) { sceKernelSetEventFlag(g_ev, EV_ISO0); }
static void on_iso1(struct UsbbdDeviceRequest *req) { sceKernelSetEventFlag(g_ev, EV_ISO1); }

static void iso_queue(int i)
{
    int n, k, ret;

    g_rate_acc += MIC_RATE;
    n = g_rate_acc / 1000;
    g_rate_acc %= 1000;
    for (k = 0; k < n; k++) {
        g_iso_buf[i][k] = g_tone[g_tone_pos];
        g_tone_pos = (g_tone_pos + 1) % 50;
    }
    sceKernelDcacheWritebackRange(g_iso_buf[i], sizeof(g_iso_buf[i]));
    memset(&g_iso_req[i], 0, sizeof(g_iso_req[i]));
    g_iso_req[i].endpoint = &g_ep[3];
    g_iso_req[i].data = g_iso_buf[i];
    g_iso_req[i].size = n * 2;
    g_iso_req[i].onComplete = i ? on_iso1 : on_iso0;
    ret = sceUsbbdReqSend(&g_iso_req[i]);
    if (ret < 0) {
        g_info.iso_err++;
        g_info.iso_last_ret = ret;
    }
}

static int mic_thread(SceSize args, void *argp)
{
    u32 bits;

    for (;;) {
        sceKernelWaitEventFlag(g_ev, EV_MIC_ON | EV_QUIT, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits, NULL);
        if (bits & EV_QUIT)
            return 0;
        sceKernelClearEventFlag(g_ev, ~(EV_ISO0 | EV_ISO1 | EV_MIC_OFF));
        iso_queue(0);
        iso_queue(1);
        for (;;) {
            SceUInt to = 100000;
            if (sceKernelWaitEventFlag(g_ev, EV_ISO0 | EV_ISO1 | EV_MIC_OFF | EV_QUIT,
                                       PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits, &to) < 0)
                continue;
            if (bits & (EV_MIC_OFF | EV_QUIT)) {
                sceUsbbdReqCancelAll(&g_ep[3]);
                if (bits & EV_QUIT)
                    return 0;
                break;
            }
            for (int i = 0; i < 2; i++) {
                if (!(bits & (EV_ISO0 << i)))
                    continue;
                if (g_iso_req[i].returnCode == 0)
                    g_info.iso_sent++;
                else {
                    g_info.iso_err++;
                    g_info.iso_last_ret = g_iso_req[i].returnCode;
                }
                iso_queue(i);
            }
        }
    }
}

/* ---- exports ---- */

int deckUsbStart(void)
{
    int k1 = pspSdkSetK1(0), ret;

    if (g_started) {
        pspSdkSetK1(k1);
        return 0;
    }
    ret = g_info.ret_register = sceUsbbdRegister(&g_driver);
    if (ret >= 0)
        ret = g_info.ret_bus = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
    if (ret >= 0)
        ret = g_info.ret_driver = sceUsbStart(DRIVER_NAME, 0, 0);
    if (ret >= 0)
        ret = g_info.ret_activate = sceUsbActivate(DECK_PID);
    if (ret >= 0) {
        g_info.ret_charge_syscon = sceSysconPermitChargeBattery();
        g_info.ret_charge_power = scePowerBatteryPermitCharging();
        if (DECK_UAC) {
            g_mic_thid = sceKernelCreateThread("DeckMic", mic_thread, 16, 0x1000, 0, NULL);
            if (g_mic_thid >= 0)
                sceKernelStartThread(g_mic_thid, 0, NULL);
        }
        g_started = 1;
    }
    pspSdkSetK1(k1);
    return ret;
}

int deckUsbStop(void)
{
    int k1 = pspSdkSetK1(0);

    if (g_started) {
        if (g_mic_thid >= 0) {
            sceKernelSetEventFlag(g_ev, EV_QUIT);
            sceKernelWaitThreadEnd(g_mic_thid, NULL);
            sceKernelDeleteThread(g_mic_thid);
            g_mic_thid = -1;
        }
        sceUsbDeactivate(DECK_PID);
        sceUsbStop(DRIVER_NAME, 0, 0);
        sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
        sceUsbbdUnregister(&g_driver);
        g_started = 0;
    }
    pspSdkSetK1(k1);
    return 0;
}

int deckUsbRead(void *buf, int max, int timeout_ms)
{
    int k1 = pspSdkSetK1(0), ret;
    u32 bits;
    SceUInt to = (SceUInt)timeout_ms * 1000;

    if (!user_ok(buf, max) || max <= 0 || timeout_ms < 0) {
        ret = DECK_EINVAL;
        goto out;
    }
    if (!g_info.speed) {
        ret = DECK_ENOTCONN;
        goto out;
    }
    if (!g_rx_pending) {
        sceKernelDcacheInvalidateRange(g_rx_buf, sizeof(g_rx_buf));
        memset(&g_rx_req, 0, sizeof(g_rx_req));
        g_rx_req.endpoint = &g_ep[2];
        g_rx_req.data = g_rx_buf;
        g_rx_req.size = sizeof(g_rx_buf);
        g_rx_req.onComplete = on_rx;
        sceKernelClearEventFlag(g_ev, ~EV_RX);
        if (sceUsbbdReqRecv(&g_rx_req) < 0) {
            ret = DECK_EIO;
            goto out;
        }
        g_rx_pending = 1;
    }
    if (sceKernelWaitEventFlag(g_ev, EV_RX, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits,
                               timeout_ms ? &to : NULL) < 0) {
        ret = 0;
        goto out;
    }
    g_rx_pending = 0;
    if (g_rx_req.returnCode != 0) {
        ret = DECK_EIO;
        goto out;
    }
    ret = g_rx_req.transmitted;
    sceKernelDcacheInvalidateRange(g_rx_buf, sizeof(g_rx_buf));
    memcpy(buf, g_rx_buf, ret < max ? ret : max);
    g_info.rx_frames++;
out:
    pspSdkSetK1(k1);
    return ret;
}

int deckUsbWrite(const void *buf, int len)
{
    int k1 = pspSdkSetK1(0), ret;
    u32 bits;
    SceUInt to = TX_TIMEOUT_US;

    if (!user_ok(buf, len) || len <= 0 || len > DECK_MAX_FRAME) {
        pspSdkSetK1(k1);
        return DECK_EINVAL;
    }
    sceKernelWaitSema(g_tx_sema, 1, NULL);
    if (!g_info.speed) {
        ret = DECK_ENOTCONN;
        goto out;
    }
    memcpy(g_tx_buf, buf, len);
    sceKernelDcacheWritebackRange(g_tx_buf, len);
    memset(&g_tx_req, 0, sizeof(g_tx_req));
    g_tx_req.endpoint = &g_ep[1];
    g_tx_req.data = g_tx_buf;
    g_tx_req.size = len;
    g_tx_req.onComplete = on_tx;
    sceKernelClearEventFlag(g_ev, ~EV_TX);
    if (sceUsbbdReqSend(&g_tx_req) < 0) {
        ret = DECK_EIO;
        goto out;
    }
    if (sceKernelWaitEventFlag(g_ev, EV_TX, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits, &to) < 0) {
        SceUInt grace = 100000;
        sceUsbbdReqCancel(&g_tx_req);
        sceKernelWaitEventFlag(g_ev, EV_TX, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits, &grace);
        g_info.tx_timeouts++;
        ret = DECK_ETIMEDOUT;
        goto out;
    }
    ret = g_tx_req.returnCode == 0 ? g_tx_req.transmitted : DECK_EIO;
    if (ret > 0)
        g_info.tx_frames++;
out:
    sceKernelSignalSema(g_tx_sema, 1);
    pspSdkSetK1(k1);
    return ret;
}

int deckUsbInfo(deck_usb_info *out)
{
    int k1 = pspSdkSetK1(0), ret = DECK_EINVAL;

    if (user_ok(out, sizeof(*out))) {
        memcpy(out, &g_info, sizeof(*out));
        ret = 0;
    }
    pspSdkSetK1(k1);
    return ret;
}

int module_start(SceSize args, void *argp)
{
    g_ev = sceKernelCreateEventFlag("DeckUsbEv", PSP_EVENT_WAITMULTIPLE, 0, NULL);
    g_tx_sema = sceKernelCreateSema("DeckUsbTx", 0, 1, 1, NULL);
    return (g_ev < 0 || g_tx_sema < 0) ? 1 : 0;
}

int module_stop(SceSize args, void *argp)
{
    deckUsbStop();
    sceKernelDeleteEventFlag(g_ev);
    sceKernelDeleteSema(g_tx_sema);
    return 0;
}
