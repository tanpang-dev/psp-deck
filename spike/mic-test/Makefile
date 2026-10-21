TARGET = mictest
OBJS = main.o DeckUsb.o
PRXDIR = ../../psp/prx
INCDIR = $(PRXDIR)
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspaudio
BUILD_PRX = 1
PSP_FW_VERSION = 661
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Deck Mic Test

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

DeckUsb.o: $(PRXDIR)/exports.exp
	cd $(PRXDIR) && psp-build-exports -s exports.exp
	$(CC) $(CFLAGS) -I$(PSPSDK)/include -c $(PRXDIR)/DeckUsb.S -o $@

dist: EBOOT.PBP
	mkdir -p dist/PSPDECKMIC
	cp EBOOT.PBP dist/PSPDECKMIC/
