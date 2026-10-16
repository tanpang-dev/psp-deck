#ifndef DECK_DESCRIPTORS_H
#define DECK_DESCRIPTORS_H

#include <string.h>
#include "usbbd.h"

#define MIC_RATE       44100
#define MIC_MAX_PACKET ((MIC_RATE + 999) / 1000 * 2)

enum { DECK_KIND_UAC, DECK_KIND_VENDOR, DECK_KIND_NONE };

struct deck_variant {
    const char *name;
    unsigned char kind;
    unsigned char hs_interval;  /* bInterval at hi-speed: 4 = 1 ms, 1 = 125 us */
    unsigned char fs_only;      /* no hi-speed descriptors handed to the bus driver */
};

#define DECK_NVARIANTS 7

extern const struct deck_variant deck_variants[DECK_NVARIANTS];
extern struct DeviceDescriptor deck_dev_hi, deck_dev_full;
extern struct UsbConfiguration deck_config_hi, deck_config_full;

static inline int deck_variant_ifaces(const struct deck_variant *v)
{
    return v->kind == DECK_KIND_UAC ? 3 : v->kind == DECK_KIND_VENDOR ? 2 : 1;
}

void deck_desc_apply(int variant);
int deck_iso_packet(int per_second);

#endif
