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
static struct UsbInterface g_if = { -1, 0, 3 };

static SceUID g_ev = -1, g_tx_sema = -1, g_mic_thid = -1;
static struct UsbbdDeviceRequest g_rx_req, g_tx_req;
static int g_rx_pending;
static unsigned char g_rx_buf[DECK_MAX_FRAME] __attribute__((aligned(64)));
static unsigned char g_tx_buf[DECK_MAX_FRAME] __attribute__((aligned(64)));
static deck_usb_info g_info;
static int g_started, g_mic_if = 2;

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

/* EP0 data stage for class GET requests (cf. psp-uvc usb_ep0_req_send). */
static struct UsbbdDeviceRequest g_ep0_req;
static unsigned char g_ep0_buf[64] __attribute__((aligned(64)));

static int g_ep0_queued;

static void on_ep0(struct UsbbdDeviceRequest *req) { g_info.ep0_done++; }

/* A new SETUP means the host has finished with the previous control transfer, but if our earlier
 * answer never went out it is still on the bus driver's EP0 queue; pull it off before reusing it. */
static void ep0_send(const void *data, int len, int wlength)
{
    if (g_ep0_queued)
        sceUsbbdReqCancel(&g_ep0_req);
    if (len > wlength)
        len = wlength;
    memcpy(g_ep0_buf, data, len);
    sceKernelDcacheWritebackRange(g_ep0_buf, sizeof(g_ep0_buf));
    memset(&g_ep0_req, 0, sizeof(g_ep0_req));
    g_ep0_req.endpoint = &g_ep[0];
    g_ep0_req.data = g_ep0_buf;
    g_ep0_req.size = len;
    g_ep0_req.onComplete = on_ep0;
    g_info.ep0_ret = sceUsbbdReqSend(&g_ep0_req);
    g_ep0_queued = g_info.ep0_ret >= 0;
    g_info.ep0_sent++;
}

/* UAC2 clock source: sampling frequency (CUR/RANGE) and validity, all fixed. */
static int uac2_clock_request(const struct DeviceRequest *req)
{
    static const unsigned char cur[4] = { MIC_RATE & 0xFF, (MIC_RATE >> 8) & 0xFF, MIC_RATE >> 16, 0 };
    static const unsigned char range[14] = {
        1, 0,
        MIC_RATE & 0xFF, (MIC_RATE >> 8) & 0xFF, MIC_RATE >> 16, 0,
        MIC_RATE & 0xFF, (MIC_RATE >> 8) & 0xFF, MIC_RATE >> 16, 0,
        0, 0, 0, 0,
    };
    static const unsigned char valid = 1;
    int cs = req->wValue >> 8;

    if (req->bmRequestType != 0xA1 || req->wIndex != ((UAC2_CLOCK_ID << 8) | 1))
        return 0;
    if (cs == 1 && req->bRequest == 0x01)
        ep0_send(cur, sizeof(cur), req->wLength);
    else if (cs == 1 && req->bRequest == 0x02)
        ep0_send(range, sizeof(range), req->wLength);
    else if (cs == 2 && req->bRequest == 0x01)
        ep0_send(&valid, 1, req->wLength);
    else
        return 0;
    g_info.uac_answered++;
    return 1;
}

static int process_request(int recipient, int arg, struct DeviceRequest *req)
{
    memcpy(g_info.req_log[g_info.nreq & 7], req, 8);
    g_info.nreq++;
    if (deck_variants[g_info.variant].kind == DECK_KIND_UAC)
        uac2_clock_request(req);
    /* macOS may switch alt settings via a plain SET_INTERFACE seen here (cf. psp-uvc). */
    if (req->bmRequestType == 0x01 && req->bRequest == 0x0B && req->wIndex == g_mic_if)
        set_mic(req->wValue);
    return 0;
}

static int change_setting(int interface, int alt)
{
    if (interface == g_mic_if)
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
    DRIVER_NAME, 4, g_ep, &g_if,
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
static short g_iso_buf[2][64] __attribute__((aligned(64)));
_Static_assert(sizeof(g_iso_buf[0]) >= MIC_MAX_PACKET, "iso buffer smaller than one packet");
static int g_tone_pos, g_iso_busy[2], g_iso_mode = DECK_ISO_SINGLE, g_first_layout, g_iso_per_sec = 1000;
static u32 g_iso_t0[2];
/* Samples come due by wall clock, so a missed microframe is made up in the next packets. */
static u32 g_due_last, g_due_rem;
static int g_owed;

static void on_iso0(struct UsbbdDeviceRequest *req) { sceKernelSetEventFlag(g_ev, EV_ISO0); }
static void on_iso1(struct UsbbdDeviceRequest *req) { sceKernelSetEventFlag(g_ev, EV_ISO1); }

static void iso_log(int ret, int tx, int us)
{
    int *e = g_info.iso_log[g_info.iso_nlog & 7];
    e[0] = ret;
    e[1] = tx;
    e[2] = us;
    g_info.iso_nlog++;
}

static void iso_queue(int i)
{
    int n, k, ret;

    if ((i == 1 && (g_iso_mode & DECK_ISO_SINGLE)) || g_iso_busy[i])
        return;
    {
        u32 now = sceKernelGetSystemTimeLow(), dt = now - g_due_last;
        int max = deck_iso_packet(g_iso_per_sec) / 2;

        g_due_last = now;
        if (dt > 50000)
            dt = 50000;
        g_due_rem += dt * MIC_RATE;
        g_owed += g_due_rem / 1000000;
        g_due_rem %= 1000000;
        if (g_owed > MIC_RATE / 100)        /* never more than 10 ms behind */
            g_owed = MIC_RATE / 100;
        n = g_owed < 1 ? 1 : g_owed < max ? g_owed : max;
        if (g_owed > max)
            g_info.iso_behind++;
        g_owed -= n;
    }
    if (g_iso_mode & DECK_ISO_FIXED)
        n = deck_iso_packet(g_iso_per_sec) / 2;
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
    g_iso_t0[i] = sceKernelGetSystemTimeLow();
    ret = sceUsbbdReqSend(&g_iso_req[i]);
    g_iso_busy[i] = ret >= 0;
    if (ret < 0) {
        g_info.iso_err++;
        g_info.iso_last_ret = ret;
        iso_log(ret, -1, 0);
    }
}

/* Drops whatever is in flight and starts over; used when a request never completes. */
/* Cancelled requests get no onComplete from the bus driver; CancelAll hands them back synchronously.
 * Only called when the host leaves alt 1 (or on exit), never in a loop. */
static void iso_stop(void)
{
    sceUsbbdReqCancelAll(&g_ep[3]);
    g_iso_busy[0] = g_iso_busy[1] = 0;
    sceKernelClearEventFlag(g_ev, ~(EV_ISO0 | EV_ISO1));
}

static void iso_start(void)
{
    iso_queue(0);
    iso_queue(1);
}

static int mic_thread(SceSize args, void *argp)
{
    u32 bits;

    for (;;) {
        sceKernelWaitEventFlag(g_ev, EV_MIC_ON | EV_QUIT, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits, NULL);
        if (bits & EV_QUIT)
            return 0;
        sceKernelClearEventFlag(g_ev, ~(EV_ISO0 | EV_ISO1 | EV_MIC_OFF));
        g_info.ep3_num = g_ep[3].endpointNumber;
        g_iso_per_sec = g_info.speed == 2 ? 8000 : 1000;
        g_due_last = sceKernelGetSystemTimeLow();
        g_due_rem = 0;
        g_owed = 0;
        iso_start();
        for (;;) {
            SceUInt to = 20000;
            if (sceKernelWaitEventFlag(g_ev, EV_ISO0 | EV_ISO1 | EV_MIC_OFF | EV_QUIT,
                                       PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &bits, &to) < 0) {
                /* The host is not polling right now; leave the request queued until it does. */
                g_info.iso_timeouts++;
                continue;
            }
            if (bits & (EV_MIC_OFF | EV_QUIT)) {
                iso_stop();
                if (bits & EV_QUIT)
                    return 0;
                break;
            }
            for (int i = 0; i < 2; i++) {
                if (!(bits & (EV_ISO0 << i)) || !g_iso_busy[i])
                    continue;
                g_iso_busy[i] = 0;
                iso_log(g_iso_req[i].returnCode, g_iso_req[i].transmitted,
                        (int)(sceKernelGetSystemTimeLow() - g_iso_t0[i]));
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

/* Tries each descriptor layout until the bus driver accepts it; every result lands in g_info. */
static int start_variants(void)
{
    int v, ret = -1;

    v = g_first_layout;
    for (; v < DECK_NVARIANTS; v++) {
        int iso = deck_variants[v].kind != DECK_KIND_NONE;

        deck_desc_apply(v);
        g_driver.numEndpoints = iso ? 4 : 3;
        g_if.numInterfaces = deck_variant_ifaces(&deck_variants[v]);
        g_mic_if = deck_variants[v].kind == DECK_KIND_UAC ? 2 : 1;
        g_info.variant = v;
        ret = g_info.ret_register = sceUsbbdRegister(&g_driver);
        if (ret >= 0)
            ret = g_info.ret_bus = sceUsbStart(PSP_USBBUS_DRIVERNAME, 0, 0);
        if (ret >= 0)
            ret = g_info.ret_driver = sceUsbStart(DRIVER_NAME, 0, 0);
        g_info.variant_ret[v] = ret;
        if (ret >= 0)
            return ret;
        sceUsbStop(PSP_USBBUS_DRIVERNAME, 0, 0);
        sceUsbbdUnregister(&g_driver);
    }
    return ret;
}

int deckUsbStart(void)
{
    int k1 = pspSdkSetK1(0), ret;

    if (g_started) {
        pspSdkSetK1(k1);
        return 0;
    }
    ret = start_variants();
    if (ret >= 0)
        ret = g_info.ret_activate = sceUsbActivate(DECK_PID);
    if (ret >= 0) {
        g_info.ret_charge_syscon = sceSysconPermitChargeBattery();
        g_info.ret_charge_power = scePowerBatteryPermitCharging();
        if (deck_variants[g_info.variant].kind != DECK_KIND_NONE) {
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

void sceDisplaySetBrightness(int level, int unk1);
void sceDisplayGetBrightness(int *level, int *unk1);

/* Idle dimming (DESIGN §4.4). on=0 dims to a low level, on=1 restores what was there before. */
int deckSetBacklight(int on)
{
    static int saved = -1;
    int k1 = pspSdkSetK1(0), level, unk;

    if (!on && saved < 0) {
        sceDisplayGetBrightness(&level, &unk);
        saved = level;
        sceDisplaySetBrightness(level < 10 ? level : 10, 0);
    } else if (on && saved >= 0) {
        sceDisplaySetBrightness(saved, 0);
        saved = -1;
    }
    pspSdkSetK1(k1);
    return 0;
}

int deckUsbIsoMode(int flags)
{
    g_iso_mode = flags;
    return 0;
}

int deckUsbFirstLayout(int v)
{
    if (v < 0 || v >= DECK_NVARIANTS)
        return DECK_EINVAL;
    g_first_layout = v;
    return 0;
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
