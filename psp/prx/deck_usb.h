/* User-callable API of deck.prx (syscall exports, library "DeckUsb"). */
#ifndef DECK_USB_H
#define DECK_USB_H

#define DECK_ENOTCONN  (-1)
#define DECK_EINVAL    (-2)
#define DECK_EIO       (-3)
#define DECK_ETIMEDOUT (-4)

typedef struct {
    int speed;                 /* 0 = detached, 1 = full, 2 = hi */
    int mic_alt;               /* alternate setting of the AudioStreaming interface */
    int rx_frames, tx_frames, tx_timeouts;
    int iso_sent, iso_err, iso_last_ret;
    int ret_register, ret_bus, ret_driver, ret_activate;
    int ret_charge_syscon, ret_charge_power;
    int variant;               /* index into the PRX's descriptor layouts (see descriptors.c) */
    int variant_ret[8];        /* sceUsbStart result per layout tried */
    int ep3_num, iso_timeouts, iso_nlog, uac_answered, iso_behind;
    int ep0_sent, ep0_done, ep0_ret;
    int iso_log[8][3];         /* last completions: returnCode, transmitted, microseconds */
    int nreq;                  /* control requests seen; last 8 kept raw in req_log */
    unsigned char req_log[8][8];
} deck_usb_info;

int deckUsbStart(void);
int deckUsbStop(void);
int deckUsbRead(void *buf, int max, int timeout_ms);  /* bytes, 0 on timeout, <0 on error */
int deckUsbWrite(const void *buf, int len);           /* bytes, <0 on error (500 ms timeout) */
int deckUsbInfo(deck_usb_info *out);

/* Phase-0 diagnostics */
#define DECK_ISO_FIXED  1      /* always send full 90-byte packets */
#define DECK_ISO_SINGLE 2      /* one iso request in flight instead of two */
int deckUsbIsoMode(int flags);
int deckUsbFirstLayout(int v); /* call before deckUsbStart */

#endif
