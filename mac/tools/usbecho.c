/* S1 host tool: dumps the descriptors, round-trips frames of edge-case sizes through the PSP echo
 * spike and measures throughput.  usbecho [--no-read]   (--no-read: send one frame and quit, S1-e) */
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../proto/deck_proto.h"

static uint8_t out[DECK_MAX_FRAME], in[DECK_MAX_FRAME], payload[DECK_MAX_FRAME];

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int roundtrip(libusb_device_handle *h, uint32_t len, int check)
{
    int n = deck_frame(out, DECK_M_STATUS, payload, len), done = 0, r;
    deck_hdr hdr;
    const uint8_t *p;

    if ((r = libusb_bulk_transfer(h, 0x02, out, n, &done, 2000)) || done != n) {
        fprintf(stderr, "  OUT len=%u: %s (%d/%d)\n", len, libusb_error_name(r), done, n);
        return -1;
    }
    if ((r = libusb_bulk_transfer(h, 0x81, in, sizeof(in), &done, 2000))) {
        fprintf(stderr, "  IN  len=%u: %s\n", len, libusb_error_name(r));
        return -1;
    }
    if (check && (deck_parse(in, done, &hdr, &p) != (int)len || memcmp(p, payload, len))) {
        fprintf(stderr, "  len=%u: echo mismatch (got %d bytes)\n", len, done);
        return -1;
    }
    return 0;
}

static void dump_config(libusb_device_handle *h)
{
    uint8_t d[512];
    int n = libusb_get_descriptor(h, LIBUSB_DT_CONFIG, 0, d, sizeof(d));

    printf("config descriptor (%d bytes):\n", n);
    for (int i = 0; i < n;) {
        int l = d[i] ? d[i] : 1;
        printf("  ");
        for (int k = 0; k < l && i + k < n; k++)
            printf("%02x ", d[i + k]);
        printf("\n");
        i += l;
    }
}

int main(int argc, char **argv)
{
    static const uint32_t lens[] = { 0, 1, 7, 55, 56, 57, 63, 64, 504, 512, 4096, DECK_MAX_PAYLOAD };
    libusb_device_handle *h;
    int r, ok = 1;

    for (unsigned i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i * 31 + 7);
    if (libusb_init(NULL) || !(h = libusb_open_device_with_vid_pid(NULL, DECK_VID, DECK_PID))) {
        fprintf(stderr, "PSP (%04x:%04x) not found\n", DECK_VID, DECK_PID);
        return 1;
    }
    printf("speed: %d (1=low 2=full 3=high)\n", libusb_get_device_speed(libusb_get_device(h)));
    dump_config(h);
    if ((r = libusb_claim_interface(h, 0))) {
        fprintf(stderr, "claim IF0: %s\n", libusb_error_name(r));
        return 1;
    }

    if (argc > 1 && !strcmp(argv[1], "--no-read")) {
        int n = deck_frame(out, DECK_M_STATUS, payload, 100), done;
        r = libusb_bulk_transfer(h, 0x02, out, n, &done, 2000);
        printf("sent one frame without reading the echo: %s\n", libusb_error_name(r));
        return 0;
    }

    printf("S1-c edge sizes:\n");
    for (unsigned i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        int bad = roundtrip(h, lens[i], 1);
        printf("  payload %5u (+hdr %5u): %s\n", lens[i], lens[i] + DECK_HDR_SIZE, bad ? "FAIL" : "ok");
        ok &= !bad;
    }

    printf("S1-d throughput (4 MB each way, echoed):\n");
    double t = now();
    int count = (4 << 20) / DECK_MAX_PAYLOAD;
    for (int i = 0; i < count && ok; i++)
        ok &= !roundtrip(h, DECK_MAX_PAYLOAD, 0);
    t = now() - t;
    printf("  %.2f s, %.2f MB/s per direction\n", t, count * (double)DECK_MAX_PAYLOAD / t / 1e6);

    libusb_release_interface(h, 0);
    libusb_close(h);
    libusb_exit(NULL);
    printf(ok ? "RESULT: ok\n" : "RESULT: FAIL\n");
    return !ok;
}
