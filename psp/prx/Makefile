TARGET = deck
OBJS = driver.o descriptors.o exports.o

USE_KERNEL_LIBS = 1
BUILD_PRX = 1
PRX_EXPORTS = exports.exp

CFLAGS = -Os -G0 -Wall
LIBS = -lpspusb_driver -lpspusbbus_driver -lpsppower_driver -lpspdisplay_driver

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak

DeckUsb.S: exports.exp
	psp-build-exports -s $<

# Host preflight of the descriptor layouts (run before trying a new layout on real hardware)
check:
	cc -Wall -DDECK_HOST_TEST desc_check.c descriptors.c -o /tmp/deck_desc_check && /tmp/deck_desc_check
