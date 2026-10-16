TARGET = deck
OBJS = driver.o descriptors.o exports.o

USE_KERNEL_LIBS = 1
BUILD_PRX = 1
PRX_EXPORTS = exports.exp

CFLAGS = -Os -G0 -Wall
LIBS = -lpspusb_driver -lpspusbbus_driver -lpsppower_driver

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak

DeckUsb.S: exports.exp
	psp-build-exports -s $<
