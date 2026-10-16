/* Phase-0 diagnostic for the "vendor iso" layouts: reads EP 0x83 with libusb isochronous transfers and
 * reports what actually arrives (packet sizes, status, whether the samples are the PSP's test tone). */
#include <libusb.h>
#include <stdio.h>
#include <string.h>
#include "../../proto/deck_proto.h"

#define NPKT  200
#define ROUNDS 25

static volatile int g_done;

static void LIBUSB_CALL on_done(struct libusb_transfer *t) { g_done = 1; }

int main(void)
{
    static unsigned char buf[NPKT * 1024];
    libusb_device_handle *h;
    int mps, r = 0, sizes[1025] = {0}, status_bad = 0, total = 0, nonzero = 0, shown = 0;
    long bytes = 0;

    if (libusb_init(NULL) || !(h = libusb_open_device_with_vid_pid(NULL, DECK_VID, DECK_PID))) {
        fprintf(stderr, "PSP not found\n");
        return 1;
    }
    /* A malformed descriptor once panicked the macOS USB stack here; only proceed on the expected shape. */
    {
        struct libusb_config_descriptor *c = NULL;
        const struct libusb_interface_descriptor *alt;
        int ok = !libusb_get_active_config_descriptor(libusb_get_device(h), &c);

        ok = ok && c->bNumInterfaces == 2 && c->interface[1].num_altsetting == 2;
        alt = ok ? &c->interface[1].altsetting[1] : NULL;
        ok = ok && alt->bInterfaceClass == 0xFF && alt->bNumEndpoints == 1 &&
             alt->endpoint[0].bEndpointAddress == 0x83 && (alt->endpoint[0].bmAttributes & 3) == 1 &&
             alt->endpoint[0].wMaxPacketSize > 0 && alt->endpoint[0].wMaxPacketSize <= 1024 &&
             alt->endpoint[0].bInterval >= 1 && alt->endpoint[0].bInterval <= 16;
        if (c)
            libusb_free_config_descriptor(c);
        if (!ok) {
            fprintf(stderr, "unexpected descriptors (not a vendor-iso layout?) - refusing to open EP 0x83\n");
            return 1;
        }
    }
    mps = libusb_get_max_iso_packet_size(libusb_get_device(h), 0x83);
    printf("EP 0x83 max iso packet: %d\n", mps);
    if (mps <= 0 || mps > 1024 || (r = libusb_claim_interface(h, 1)) || (r = libusb_set_interface_alt_setting(h, 1, 1))) {
        fprintf(stderr, "setup failed (mps %d): %s\n", mps, libusb_error_name(r));
        return 1;
    }

    for (int round = 0; round < ROUNDS; round++) {
        struct libusb_transfer *t = libusb_alloc_transfer(NPKT);

        memset(buf, 0, sizeof(buf));
        libusb_fill_iso_transfer(t, h, 0x83, buf, NPKT * mps, NPKT, on_done, NULL, 2000);
        libusb_set_iso_packet_lengths(t, mps);
        g_done = 0;
        if ((r = libusb_submit_transfer(t))) {
            fprintf(stderr, "submit: %s\n", libusb_error_name(r));
            return 1;
        }
        while (!g_done)
            libusb_handle_events(NULL);
        if (round == 0)
            printf("transfer status: %d\n", t->status);
        for (int i = 0; i < NPKT; i++) {
            struct libusb_iso_packet_descriptor *p = &t->iso_packet_desc[i];
            unsigned char *d = libusb_get_iso_packet_buffer_simple(t, i);
            int nz = 0;

            total++;
            status_bad += p->status != LIBUSB_TRANSFER_COMPLETED;
            sizes[p->actual_length <= 1024 ? p->actual_length : 1024]++;
            bytes += p->actual_length;
            for (unsigned k = 0; k < p->actual_length; k++)
                nz |= d[k];
            nonzero += nz != 0;
            if (nz && shown < 3) {
                printf("packet %d (%u bytes):", i, p->actual_length);
                for (unsigned k = 0; k < p->actual_length && k < 24; k++)
                    printf(" %02x", d[k]);
                printf("\n");
                shown++;
            }
        }
        libusb_free_transfer(t);
    }

    printf("packets %d, bad status %d, with non-zero data %d, bytes %ld\n", total, status_bad, nonzero, bytes);
    printf("actual_length histogram:");
    for (int i = 0; i <= 1024; i++)
        if (sizes[i])
            printf("  %d:%d", i, sizes[i]);
    printf("\n");
    libusb_set_interface_alt_setting(h, 1, 0);
    libusb_release_interface(h, 1);
    libusb_close(h);
    libusb_exit(NULL);
    return 0;
}
