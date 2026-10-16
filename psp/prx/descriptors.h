#ifndef DECK_DESCRIPTORS_H
#define DECK_DESCRIPTORS_H

#include <string.h>
#include "usbbd.h"

#define MIC_RATE       44100
#define MIC_MAX_PACKET (((MIC_RATE + 999) / 1000 + 1) * 2)  /* full-speed packet, the largest */
#define UAC2_CLOCK_ID  0x10

enum { DECK_KIND_UAC, DECK_KIND_VENDOR, DECK_KIND_NONE };

struct deck_variant {
    const char *name;
    unsigned char kind;
};

#define DECK_NVARIANTS 3

struct deck_speed_set {
    struct EndpointDescriptor ep[4] __attribute__((aligned(16)));  /* last entry = terminator */
    struct InterfaceDescriptor ifs[5];
    struct InterfaceSettings set[4];
    struct ConfigDescriptor conf;
};

extern const struct deck_variant deck_variants[DECK_NVARIANTS];
extern struct DeviceDescriptor deck_dev_hi, deck_dev_full;
extern struct UsbConfiguration deck_config_hi, deck_config_full;

static inline int deck_variant_ifaces(const struct deck_variant *v)
{
    return v->kind == DECK_KIND_UAC ? 3 : v->kind == DECK_KIND_VENDOR ? 2 : 1;
}

void deck_desc_apply(int variant);
void deck_desc_build(struct deck_speed_set *s, struct DeviceDescriptor *dev, const struct deck_variant *v, int hs);
int deck_iso_packet(int per_second);

#endif
