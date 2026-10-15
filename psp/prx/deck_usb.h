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
    int nreq;                  /* control requests seen; last 8 kept raw in req_log */
    unsigned char req_log[8][8];
} deck_usb_info;

int deckUsbStart(void);
int deckUsbStop(void);
int deckUsbRead(void *buf, int max, int timeout_ms);  /* bytes, 0 on timeout, <0 on error */
int deckUsbWrite(const void *buf, int len);           /* bytes, <0 on error (500 ms timeout) */
int deckUsbInfo(deck_usb_info *out);

#endif
