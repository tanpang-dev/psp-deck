/* Phase-0 diagnostic for the "vendor iso" layout: reads EP 0x83 with back-to-back libusb isochronous
 * transfers (no gaps between them) and reports packet sizes, status and whether the PSP's test tone
 * arrives unbroken - i.e. whether the USB link itself drops packets, independent of CoreAudio. */
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../proto/deck_proto.h"

#define NPKT    500      /* per transfer: 62.5 ms at 125 us */
#define NXFER   4        /* kept in flight */
#define TOTAL   128      /* transfers to receive: 8 s */

static FILE *g_dump;     /* optional: per-packet lengths, for offline analysis */
static short *g_stream;
static int g_nstream, g_cap, g_submitted, g_inflight, g_total, g_bad, g_nonzero, g_sizes[1025];

static void LIBUSB_CALL on_done(struct libusb_transfer *t)
{
    for (int i = 0; i < t->num_iso_packets; i++) {
        struct libusb_iso_packet_descriptor *p = &t->iso_packet_desc[i];
        unsigned char *d = libusb_get_iso_packet_buffer_simple(t, i);
        int nz = 0;

        g_total++;
        g_bad += p->status != LIBUSB_TRANSFER_COMPLETED;
        g_sizes[p->actual_length <= 1024 ? p->actual_length : 1024]++;
        if (g_dump)
            fwrite(&p->actual_length, 4, 1, g_dump);
        for (unsigned k = 0; k + 1 < p->actual_length; k += 2) {
            nz |= d[k] | d[k + 1];
            if (g_nstream < g_cap)
                g_stream[g_nstream++] = (short)(d[k] | d[k + 1] << 8);
        }
        g_nonzero += nz != 0;
    }
    if (g_submitted < TOTAL && libusb_submit_transfer(t) == 0) {
        g_submitted++;
        return;
    }
    g_inflight--;
    free(t->buffer);
    libusb_free_transfer(t);
}

/* A malformed descriptor once panicked the macOS USB stack; only proceed on the expected shape. */
static int layout_ok(libusb_device_handle *h)
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
    return ok;
}

int main(int argc, char **argv)
{
    libusb_device_handle *h;
    int r = 0, mps, breaks = 0, events = 0, last = -1000;

    if (libusb_init(NULL) || !(h = libusb_open_device_with_vid_pid(NULL, DECK_VID, DECK_PID))) {
        fprintf(stderr, "PSP not found\n");
        return 1;
    }
    if (!layout_ok(h)) {
        fprintf(stderr, "unexpected descriptors (not the vendor-iso layout?) - refusing to open EP 0x83\n");
        return 1;
    }
    mps = libusb_get_max_iso_packet_size(libusb_get_device(h), 0x83);
    printf("EP 0x83 max iso packet: %d\n", mps);
    if ((r = libusb_claim_interface(h, 1)) || (r = libusb_set_interface_alt_setting(h, 1, 1))) {
        fprintf(stderr, "setup failed: %s\n", libusb_error_name(r));
        return 1;
    }
    if (argc > 1) {
        char path[1024];
        snprintf(path, sizeof(path), "%s.len", argv[1]);
        g_dump = fopen(path, "wb");
    }
    g_cap = TOTAL * NPKT * (mps / 2);
    g_stream = malloc(g_cap * sizeof(short));

    for (int i = 0; i < NXFER; i++) {
        struct libusb_transfer *t = libusb_alloc_transfer(NPKT);

        libusb_fill_iso_transfer(t, h, 0x83, malloc(NPKT * mps), NPKT * mps, NPKT, on_done, NULL, 2000);
        libusb_set_iso_packet_lengths(t, mps);
        if (libusb_submit_transfer(t) == 0) {
            g_submitted++;
            g_inflight++;
        }
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (g_inflight > 0)
        libusb_handle_events(NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    /* The test tone repeats every 50 samples, so a lost or repeated packet breaks the period. */
    for (int i = 50; i < g_nstream; i++) {
        int diff = g_stream[i] - g_stream[i - 50];
        if (diff > 800 || diff < -800) {
            breaks++;
            if (i - last > 60)
                events++;
            last = i;
        }
    }
    printf("packets %d in %.2f s (%.0f per second), bad status %d, with data %d\n", g_total, secs, g_total / secs,
           g_bad, g_nonzero);
    printf("samples %d = %.0f per second, period breaks %d samples / %d events\n", g_nstream, g_nstream / secs,
           breaks, events);
    if (argc > 1) {
        char path[1024];
        snprintf(path, sizeof(path), "%s.pcm", argv[1]);
        FILE *f = fopen(path, "wb");
        fwrite(g_stream, sizeof(short), g_nstream, f);
        fclose(f);
        fclose(g_dump);
    }
    printf("actual_length histogram:");
    for (int i = 0; i <= 1024; i++)
        if (g_sizes[i])
            printf("  %d:%d", i, g_sizes[i]);
    printf("\n");
    libusb_set_interface_alt_setting(h, 1, 0);
    libusb_release_interface(h, 1);
    libusb_close(h);
    libusb_exit(NULL);
    return 0;
}
