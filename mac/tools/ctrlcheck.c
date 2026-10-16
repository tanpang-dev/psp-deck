/* Phase-0 diagnostic: sends the UAC2 clock queries macOS uses (GET_CUR / GET_RANGE of the sampling
 * frequency) as plain control transfers and prints what comes back. Opens no streaming endpoint. */
#include <libusb.h>
#include <stdio.h>
#include "../../proto/deck_proto.h"

static void query(libusb_device_handle *h, const char *what, int req, int len)
{
    unsigned char buf[64];
    int n = libusb_control_transfer(h, 0xA1, req, 0x0100, 0x1001, buf, len, 1000);

    printf("%-9s ", what);
    if (n < 0) {
        printf("failed: %s\n", libusb_error_name(n));
        return;
    }
    printf("%2d bytes:", n);
    for (int i = 0; i < n; i++)
        printf(" %02x", buf[i]);
    printf("\n");
}

int main(void)
{
    libusb_device_handle *h;

    if (libusb_init(NULL) || !(h = libusb_open_device_with_vid_pid(NULL, DECK_VID, DECK_PID))) {
        fprintf(stderr, "PSP not found\n");
        return 1;
    }
    for (int i = 0; i < 3; i++) {
        query(h, "GET_CUR", 0x01, 4);
        query(h, "GET_RANGE", 0x02, 14);
    }
    libusb_close(h);
    libusb_exit(NULL);
    return 0;
}
