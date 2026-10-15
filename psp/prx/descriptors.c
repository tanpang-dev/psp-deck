/* USB descriptors: IF0 vendor bulk pair + IF1/IF2 USB Audio Class 1.0 microphone (docs/DESIGN.md §4.1).
 * The bus driver rejected the first UAC layout with SCE_ERROR_USB_INVALID_ARGUMENT, so the layout is
 * chosen at start time from deck_variants (first one sceUsbStart accepts). */
#include "descriptors.h"

#define LE16(v) ((v) & 0xFF), (((v) >> 8) & 0xFF)

const struct deck_variant deck_variants[DECK_NVARIANTS] = {
    { "iso-async ep9",  0x05, 1, 2, 1 },
    { "iso-async ep7",  0x05, 0, 2, 1 },
    { "iso-sync ep7",   0x01, 0, 2, 1 },
    { "iso single-alt", 0x05, 0, 1, 1 },
    { "bulk (diag)",    0x02, 0, 2, 1 },
    { "no-uac",         0,    0, 0, 0 },
};

static const unsigned char ac_extra[] = {
    9, 0x24, 0x01, LE16(0x0100), LE16(30), 1, 2,          /* AC header, collection = IF2 */
    12, 0x24, 0x02, 1, LE16(0x0201), 0, 1, LE16(0), 0, 0, /* input terminal: microphone, 1 ch */
    9, 0x24, 0x03, 2, LE16(0x0101), 0, 1, 0,              /* output terminal: USB streaming */
};

static const unsigned char as_extra[] = {
    7, 0x24, 0x01, 2, 1, LE16(0x0001),                    /* AS general: PCM */
    11, 0x24, 0x02, 1, 1, 2, 16, 1,                       /* type I: 1 ch, 16 bit, 1 rate */
    MIC_RATE & 0xFF, (MIC_RATE >> 8) & 0xFF, (MIC_RATE >> 16) & 0xFF,
};

/* UAC1 endpoints are 9 bytes; the "ep9" variants put bRefresh/bSynchAddress first in extra. */
static const unsigned char ep_iso_extra[] = {
    0, 0,
    7, 0x25, 0x01, 0, 0, LE16(0),
};

struct speed_set {
    struct EndpointDescriptor ep[4] __attribute__((aligned(16)));  /* last entry = terminator */
    struct InterfaceDescriptor ifs[5];
    struct InterfaceSettings set[3];
    struct ConfigDescriptor conf;
};

static struct speed_set g_hi, g_full;

struct UsbConfiguration deck_config_hi   = { &g_hi.conf, g_hi.set, g_hi.ifs, g_hi.ep };
struct UsbConfiguration deck_config_full = { &g_full.conf, g_full.set, g_full.ifs, g_full.ep };

struct DeviceDescriptor deck_dev_hi   = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };
struct DeviceDescriptor deck_dev_full = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };

static void build(struct speed_set *s, const struct deck_variant *v, int bulk_mps, int iso_interval)
{
    struct EndpointDescriptor *ep = s->ep;
    struct InterfaceDescriptor *ifs = s->ifs;
    int ep9 = v->ep9, total = 9 + 9 + 7 + 7;
    int bulk_iso = v->iso_attr == 0x02;

    memset(s, 0, sizeof(*s));
    ep[0] = (struct EndpointDescriptor){ 7, 5, 0x81, 2, bulk_mps, 0, 0, 0 };
    ep[1] = (struct EndpointDescriptor){ 7, 5, 0x02, 2, bulk_mps, 0, 0, 0 };
    ifs[0] = (struct InterfaceDescriptor){ 9, 4, 0, 0, 2, 0xFF, 0x01, 0xFF, 0, &ep[0], 0, 0 };
    s->set[0] = (struct InterfaceSettings){ &ifs[0], 0, 1 };

    if (v->uac) {
        ep[2] = (struct EndpointDescriptor){
            ep9 ? 9 : 7, 5, 0x83, v->iso_attr, bulk_iso ? bulk_mps : MIC_MAX_PACKET,
            bulk_iso ? 0 : iso_interval,
            ep9 ? ep_iso_extra : ep_iso_extra + 2, ep9 ? sizeof(ep_iso_extra) : sizeof(ep_iso_extra) - 2,
        };
        ifs[1] = (struct InterfaceDescriptor){ 9, 4, 1, 0, 0, 0x01, 0x01, 0, 0, 0, ac_extra, sizeof(ac_extra) };
        ifs[2] = (struct InterfaceDescriptor){ 9, 4, 2, 0, 0, 0x01, 0x02, 0, 0, 0, 0, 0 };
        ifs[3] = (struct InterfaceDescriptor){ 9, 4, 2, v->alts == 2 ? 1 : 0, 1, 0x01, 0x02, 0, 0, &ep[2],
                                               as_extra, sizeof(as_extra) };
        s->set[1] = (struct InterfaceSettings){ &ifs[1], 0, 1 };
        s->set[2] = v->alts == 2 ? (struct InterfaceSettings){ &ifs[2], 0, 2 }
                                 : (struct InterfaceSettings){ &ifs[3], 0, 1 };
        total += 9 + sizeof(ac_extra) + (v->alts == 2 ? 9 : 0) + 9 + sizeof(as_extra) + 7 + ep[2].extraLength;
    }
    s->conf = (struct ConfigDescriptor){ 9, 2, total, v->uac ? 3 : 1, 1, 0, 0x80, 250, s->set, 0, 0 };
}

void deck_desc_apply(int variant)
{
    build(&g_hi, &deck_variants[variant], 512, 4);
    build(&g_full, &deck_variants[variant], 64, 1);
}
