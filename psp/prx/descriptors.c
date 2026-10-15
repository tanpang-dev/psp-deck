/* USB descriptors: IF0 vendor bulk pair + IF1/IF2 USB Audio Class 1.0 microphone (docs/DESIGN.md §4.1).
 * Build with NO_UAC=1 to expose IF0 only. */
#include "descriptors.h"

#define LE16(v) ((v) & 0xFF), (((v) >> 8) & 0xFF)

#ifndef NO_UAC
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
#endif

#define IFACES (1 + 2 * DECK_UAC)

#define DESCRIPTORS(sfx, bulk_mps, iso_interval)                                              \
static struct EndpointDescriptor ep_##sfx[3] __attribute__((aligned(16))) = {                \
    { 7, 5, 0x81, 2, bulk_mps, 0, 0, 0 },                                                     \
    { 7, 5, 0x02, 2, bulk_mps, 0, 0, 0 },                                                     \
    ISO_EP(iso_interval)                                                                      \
};                                                                                            \
static struct InterfaceDescriptor if_##sfx[4] = {                                            \
    { 9, 4, 0, 0, 2, 0xFF, 0x01, 0xFF, 0, &ep_##sfx[0], 0, 0 },                                \
    UAC_IFS(sfx)                                                                              \
};                                                                                            \
static struct InterfaceSettings set_##sfx[3] = {                                             \
    { &if_##sfx[0], 0, 1 },                                                                   \
    UAC_SETTINGS(sfx)                                                                         \
};                                                                                            \
static struct ConfigDescriptor conf_##sfx = {                                                \
    9, 2, CONFIG_TOTAL, IFACES, 1, 0, 0x80, 250, set_##sfx, 0, 0                              \
};                                                                                            \
struct UsbConfiguration deck_config_##sfx = { &conf_##sfx, set_##sfx, if_##sfx, ep_##sfx };

#ifndef NO_UAC
#define ISO_EP(interval) { 9, 5, 0x83, 0x05, MIC_MAX_PACKET, interval, ep_iso_extra, sizeof(ep_iso_extra) },
#define UAC_IFS(sfx)                                                                          \
    { 9, 4, 1, 0, 0, 0x01, 0x01, 0, 0, 0, ac_extra, sizeof(ac_extra) },                       \
    { 9, 4, 2, 0, 0, 0x01, 0x02, 0, 0, 0, 0, 0 },                                              \
    { 9, 4, 2, 1, 1, 0x01, 0x02, 0, 0, &ep_##sfx[2], as_extra, sizeof(as_extra) },
#define UAC_SETTINGS(sfx) { &if_##sfx[1], 0, 1 }, { &if_##sfx[2], 0, 2 },
#define CONFIG_TOTAL (9 + 9 + 7 + 7 + 9 + sizeof(ac_extra) + 9 + 9 + sizeof(as_extra) + 7 + sizeof(ep_iso_extra))
#else
#define ISO_EP(interval)
#define UAC_IFS(sfx)
#define UAC_SETTINGS(sfx)
#define CONFIG_TOTAL (9 + 9 + 7 + 7)
#endif

DESCRIPTORS(hi, 512, 4)
DESCRIPTORS(full, 64, 1)

struct DeviceDescriptor deck_dev_hi   = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };
struct DeviceDescriptor deck_dev_full = { 18, 1, 0x200, 0, 0, 0, 64, 0, 0, 0x100, 0, 0, 0, 1 };
