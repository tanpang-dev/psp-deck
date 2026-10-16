TARGET = deckecho
OBJS = main.o DeckUsb.o
PRXDIR = ../../psp/prx

INCDIR = $(PRXDIR)
CFLAGS = -O2 -G0 -Wall
LIBS = -lpsppower

BUILD_PRX = 1
PSP_FW_VERSION = 661
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Deck USB Echo

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

DeckUsb.o: $(PRXDIR)/exports.exp
	cd $(PRXDIR) && psp-build-exports -s exports.exp
	$(CC) $(CFLAGS) -I$(PSPSDK)/include -c $(PRXDIR)/DeckUsb.S -o $@

# dist/PSPDECKECHO/ is the folder to copy to ms0:/PSP/GAME/
dist: EBOOT.PBP
	mkdir -p dist/PSPDECKECHO
	$(MAKE) -C $(PRXDIR) clean all && cp $(PRXDIR)/deck.prx dist/PSPDECKECHO/deck.prx
	cp EBOOT.PBP dist/PSPDECKECHO/
