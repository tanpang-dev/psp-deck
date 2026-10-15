#ifndef DECK_DESCRIPTORS_H
#define DECK_DESCRIPTORS_H

#include "usbbd.h"

#ifdef NO_UAC
#define DECK_UAC 0
#else
#define DECK_UAC 1
#endif

#define MIC_RATE       44100
#define MIC_MAX_PACKET ((MIC_RATE + 999) / 1000 * 2)

extern struct DeviceDescriptor deck_dev_hi, deck_dev_full;
extern struct UsbConfiguration deck_config_hi, deck_config_full;

#endif
