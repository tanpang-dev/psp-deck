/* USB descriptors: IF0 vendor bulk pair + an isochronous microphone stream (docs/DESIGN.md §4.1).
 * Phase 0 showed the PSP UDC only delivers iso data when polled every microframe (hi-speed bInterval 1),
 * which UAC1 cannot describe, so the microphone is USB Audio Class 2.0. */
#include "descriptors.h"

#define LE16(v) ((v) & 0xFF), (((v) >> 8) & 0xFF)
#define LE32(v) LE16(v), LE16((v) >> 16)

const struct deck_variant deck_variants[DECK_NVARIANTS] = {
    { "UAC2 mic, hs 125us",          DECK_KIND_UAC },
    { "vendor iso, hs 125us (diag)", DECK_KIND_VENDOR },
    { "bulk only",                   DECK_KIND_NONE },
};

/* Interface association: groups IF1 (control) and IF2 (streaming) into one audio function. Emitted
 * after IF0's last endpoint by hanging it off that endpoint's extra bytes. */
const unsigned char deck_iad[8] = { 8, 0x0B, 1, 2, 0x01, 0x00, 0x20, 0 };

const unsigned char deck_ac_extra[46] = {
    9, 0x24, 0x01, LE16(0x0200), 0x03, LE16(9 + 8 + 17 + 12), 0,         /* AC header v2, microphone */
    8, 0x24, 0x0A, UAC2_CLOCK_ID, 0x01, 0x01, 0, 0,                     /* clock: internal fixed, freq readable */
    17, 0x24, 0x02, 1, LE16(0x0201), 0, UAC2_CLOCK_ID, 1, LE32(0), 0, LE16(0), 0, /* input terminal: mic */
    12, 0x24, 0x03, 2, LE16(0x0101), 0, 1, UAC2_CLOCK_ID, LE16(0), 0,  /* output terminal: USB streaming */
};

const unsigned char deck_as_extra[22] = {
    16, 0x24, 0x01, 2, 0, 1, LE32(1), 1, LE32(0), 0,                    /* AS general v2: PCM, 1 ch */
    6, 0x24, 0x02, 1, 2, 16,                                            /* type I: 2-byte slots, 16 bit */
};

const unsigned char deck_ep_iso_extra[8] = { 8, 0x25, 0x01, 0, 0, 0, LE16(0) };

static struct deck_speed_set g_hi, g_full;

struct UsbConfiguration deck_config_hi   = { &g_hi.conf, g_hi.set, g_hi.ifs, g_hi.ep };
struct UsbConfiguration deck_config_full = { &g_full.conf, g_full.set, g_full.ifs, g_full.ep };

struct DeviceDescriptor deck_dev_hi   = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };
struct DeviceDescriptor deck_dev_full = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };

/* Bytes in one iso packet: the nominal sample count rounded up, plus one sample of headroom. */
int deck_iso_packet(int per_second)
{
    return ((MIC_RATE + per_second - 1) / per_second + 1) * 2;
}

void deck_desc_build(struct deck_speed_set *s, struct DeviceDescriptor *dev, const struct deck_variant *v, int hs)
{
    struct EndpointDescriptor *ep = s->ep;
    struct InterfaceDescriptor *ifs = s->ifs;
    int bulk_mps = hs ? 512 : 64, total = 9 + 9 + 7 + 7;
    int uac = v->kind == DECK_KIND_UAC;

    memset(s, 0, sizeof(*s));
    /* IAD-bearing devices must say so in the device descriptor (Misc / Common / IAD). */
    dev->bDeviceClass = uac ? 0xEF : 0;
    dev->bDeviceSubClass = uac ? 0x02 : 0;
    dev->bDeviceProtocol = uac ? 0x01 : 0;

    ep[0] = (struct EndpointDescriptor){ 7, 5, 0x81, 2, bulk_mps, 0, 0, 0 };
    ep[1] = (struct EndpointDescriptor){ 7, 5, 0x02, 2, bulk_mps, 0, uac ? deck_iad : 0, uac ? sizeof(deck_iad) : 0 };
    ifs[0] = (struct InterfaceDescriptor){ 9, 4, 0, 0, 2, 0xFF, 0x01, 0xFF, 0, &ep[0], 0, 0 };
    s->set[0] = (struct InterfaceSettings){ &ifs[0], 0, 1 };

    if (v->kind != DECK_KIND_NONE) {
        int stream_if = uac ? 2 : 1;
        unsigned char cls = uac ? 0x01 : 0xFF, proto = uac ? 0x20 : 0;

        ep[2] = (struct EndpointDescriptor){
            7, 5, 0x83, 0x05, deck_iso_packet(hs ? 8000 : 1000), 1,
            uac ? deck_ep_iso_extra : 0, uac ? sizeof(deck_ep_iso_extra) : 0,
        };
        if (uac) {
            ifs[1] = (struct InterfaceDescriptor){ 9, 4, 1, 0, 0, 0x01, 0x01, 0x20, 0, 0,
                                                   deck_ac_extra, sizeof(deck_ac_extra) };
            s->set[1] = (struct InterfaceSettings){ &ifs[1], 0, 1 };
            total += sizeof(deck_iad) + 9 + sizeof(deck_ac_extra) + sizeof(deck_as_extra);
        }
        ifs[stream_if] = (struct InterfaceDescriptor){ 9, 4, stream_if, 0, 0, cls, 0x02, proto, 0, 0, 0, 0 };
        ifs[stream_if + 1] = (struct InterfaceDescriptor){
            9, 4, stream_if, 1, 1, cls, 0x02, proto, 0, &ep[2],
            uac ? deck_as_extra : 0, uac ? sizeof(deck_as_extra) : 0 };
        s->set[stream_if] = (struct InterfaceSettings){ &ifs[stream_if], 0, 2 };
        total += 9 + 9 + 7 + ep[2].extraLength;
    }
    s->conf = (struct ConfigDescriptor){ 9, 2, total, deck_variant_ifaces(v), 1, 0, 0x80, 250, s->set, 0, 0 };
}

void deck_desc_apply(int variant)
{
    deck_desc_build(&g_hi, &deck_dev_hi, &deck_variants[variant], 1);
    deck_desc_build(&g_full, &deck_dev_full, &deck_variants[variant], 0);
}
