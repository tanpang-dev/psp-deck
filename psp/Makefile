TARGET = pspdeck
OBJS = main.o ui.o DeckUsb.o
PRXDIR = prx

INCDIR = $(PRXDIR)
CFLAGS = -O2 -G0 -Wall
LIBS = -lintrafont -lpspgum -lpspgu -lpsppower -lm

BUILD_PRX = 1
PSP_FW_VERSION = 661
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSP Deck

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

DeckUsb.o: $(PRXDIR)/exports.exp
	cd $(PRXDIR) && psp-build-exports -s exports.exp
	$(CC) $(CFLAGS) -I$(PSPSDK)/include -c $(PRXDIR)/DeckUsb.S -o $@

# dist/PSPDECK/ is the folder to copy to ms0:/PSP/GAME/
dist: EBOOT.PBP
	$(MAKE) -C $(PRXDIR) clean all
	mkdir -p dist/PSPDECK
	cp EBOOT.PBP $(PRXDIR)/deck.prx dist/PSPDECK/
