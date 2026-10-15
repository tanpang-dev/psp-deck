#ifndef DECK_DESCRIPTORS_H
#define DECK_DESCRIPTORS_H

#include <string.h>
#include "usbbd.h"

#define MIC_RATE       44100
#define MIC_MAX_PACKET ((MIC_RATE + 999) / 1000 * 2)

struct deck_variant {
    const char *name;
    unsigned char iso_attr;  /* bmAttributes of EP 0x83 */
    unsigned char ep9;       /* 9-byte UAC endpoint via extra */
    unsigned char alts;      /* AudioStreaming alternate settings (2 = zero-bandwidth alt0 + alt1) */
    unsigned char uac;       /* 0 = IF0 only */
};

#define DECK_NVARIANTS 6
#define DECK_VARIANT_NO_UAC (DECK_NVARIANTS - 1)

extern const struct deck_variant deck_variants[DECK_NVARIANTS];
extern struct DeviceDescriptor deck_dev_hi, deck_dev_full;
extern struct UsbConfiguration deck_config_hi, deck_config_full;

void deck_desc_apply(int variant);

#endif
