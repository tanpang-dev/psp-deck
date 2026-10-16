/* USB descriptors: IF0 vendor bulk pair + an isochronous microphone stream (docs/DESIGN.md §4.1).
 * The layout is picked at start time from deck_variants (phase-0 experiments). */
#include "descriptors.h"

#define LE16(v) ((v) & 0xFF), (((v) >> 8) & 0xFF)

const struct deck_variant deck_variants[DECK_NVARIANTS] = {
    { "UAC1 iso, hs 1ms",           DECK_KIND_UAC,    4 },
    { "UAC1 iso, hs 125us",         DECK_KIND_UAC,    1 },
    { "vendor iso, hs 1ms (diag)",  DECK_KIND_VENDOR, 4 },
    { "vendor iso, hs 125us (diag)", DECK_KIND_VENDOR, 1 },
    { "bulk only",                  DECK_KIND_NONE,   0 },
    { "UAC1 iso, full-speed only",  DECK_KIND_UAC,    4, 1 },
    { "vendor iso, full-speed (diag)", DECK_KIND_VENDOR, 4, 1 },
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

/* UAC1 endpoints are 9 bytes: bRefresh/bSynchAddress go first in extra, then the CS endpoint. */
static const unsigned char ep_iso_extra[] = {
    0, 0,
    7, 0x25, 0x01, 0, 0, LE16(0),
};

struct speed_set {
    struct EndpointDescriptor ep[4] __attribute__((aligned(16)));  /* last entry = terminator */
    struct InterfaceDescriptor ifs[5];
    struct InterfaceSettings set[4];
    struct ConfigDescriptor conf;
};

static struct speed_set g_hi, g_full;

struct UsbConfiguration deck_config_hi   = { &g_hi.conf, g_hi.set, g_hi.ifs, g_hi.ep };
struct UsbConfiguration deck_config_full = { &g_full.conf, g_full.set, g_full.ifs, g_full.ep };

struct DeviceDescriptor deck_dev_hi   = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };
struct DeviceDescriptor deck_dev_full = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };

/* Bytes in one iso packet for the given polling period. */
int deck_iso_packet(int per_second)
{
    return (MIC_RATE + per_second - 1) / per_second * 2;
}

static void build(struct speed_set *s, const struct deck_variant *v, int hs)
{
    struct EndpointDescriptor *ep = s->ep;
    struct InterfaceDescriptor *ifs = s->ifs;
    int bulk_mps = hs ? 512 : 64, total = 9 + 9 + 7 + 7;
    int interval = hs ? v->hs_interval : 1;
    int per_second = hs && v->hs_interval == 1 ? 8000 : 1000;
    int uac = v->kind == DECK_KIND_UAC;

    memset(s, 0, sizeof(*s));
    ep[0] = (struct EndpointDescriptor){ 7, 5, 0x81, 2, bulk_mps, 0, 0, 0 };
    ep[1] = (struct EndpointDescriptor){ 7, 5, 0x02, 2, bulk_mps, 0, 0, 0 };
    ifs[0] = (struct InterfaceDescriptor){ 9, 4, 0, 0, 2, 0xFF, 0x01, 0xFF, 0, &ep[0], 0, 0 };
    s->set[0] = (struct InterfaceSettings){ &ifs[0], 0, 1 };

    if (v->kind != DECK_KIND_NONE) {
        int stream_if = uac ? 2 : 1;

        ep[2] = (struct EndpointDescriptor){
            uac ? 9 : 7, 5, 0x83, 0x05, deck_iso_packet(per_second), interval,
            uac ? ep_iso_extra : 0, uac ? sizeof(ep_iso_extra) : 0,
        };
        if (uac) {
            ifs[1] = (struct InterfaceDescriptor){ 9, 4, 1, 0, 0, 0x01, 0x01, 0, 0, 0, ac_extra, sizeof(ac_extra) };
            s->set[1] = (struct InterfaceSettings){ &ifs[1], 0, 1 };
            total += 9 + sizeof(ac_extra) + sizeof(as_extra);
        }
        ifs[stream_if] = (struct InterfaceDescriptor){
            9, 4, stream_if, 0, 0, uac ? 0x01 : 0xFF, uac ? 0x02 : 0x02, 0, 0, 0, 0, 0 };
        ifs[stream_if + 1] = (struct InterfaceDescriptor){
            9, 4, stream_if, 1, 1, uac ? 0x01 : 0xFF, uac ? 0x02 : 0x02, 0, 0, &ep[2],
            uac ? as_extra : 0, uac ? sizeof(as_extra) : 0 };
        s->set[stream_if] = (struct InterfaceSettings){ &ifs[stream_if], 0, 2 };
        total += 9 + 9 + 7 + ep[2].extraLength;
    }
    s->conf = (struct ConfigDescriptor){ 9, 2, total, deck_variant_ifaces(v), 1, 0, 0x80, 250, s->set, 0, 0 };
}

void deck_desc_apply(int variant)
{
    build(&g_hi, &deck_variants[variant], 1);
    build(&g_full, &deck_variants[variant], 0);
}
