/* PSP Deck wire protocol v1 — shared by the PSP (C) and the Mac (Swift via DeckProto). See docs/DESIGN.md §6. */
#ifndef DECK_PROTO_H
#define DECK_PROTO_H

#include <stdint.h>
#include <string.h>

#define DECK_VID            0x054C
#define DECK_PID            0x01CA
#define DECK_PROTO_VERSION  1
#define DECK_MAGIC          0x4B44
#define DECK_HDR_SIZE       8
#define DECK_MAX_FRAME      65536
#define DECK_MAX_PAYLOAD    (DECK_MAX_FRAME - DECK_HDR_SIZE - 1)

enum {
    DECK_P_HELLO        = 0x01,
    DECK_P_BUTTON       = 0x02,
    DECK_P_TAB_STEP     = 0x03,
    DECK_P_APP_ACTIVATE = 0x04,
    DECK_P_STICK        = 0x07,

    DECK_M_HELLO        = 0x81,
    DECK_M_STATUS       = 0x82,
    DECK_M_APP          = 0x83,
    DECK_M_LABELS       = 0x85,
    DECK_M_MODE         = 0x86,
};

typedef struct {
    uint16_t magic;
    uint8_t  type;
    uint8_t  reserved;
    uint32_t len;
} deck_hdr;

/* Builds a frame in buf (>= DECK_MAX_FRAME bytes). Returns the number of bytes to put in ONE bulk
 * transfer: a trailing 0x00 is added when the size is a multiple of 64 so the transfer always ends
 * with a short packet. Returns -1 if the payload is too large. */
static inline int deck_frame(uint8_t *buf, uint8_t type, const void *payload, uint32_t len)
{
    deck_hdr h = { DECK_MAGIC, type, 0, len };
    int total;

    if (len > DECK_MAX_PAYLOAD)
        return -1;
    memcpy(buf, &h, sizeof(h));
    if (len)
        memmove(buf + DECK_HDR_SIZE, payload, len);
    total = DECK_HDR_SIZE + (int)len;
    if ((total & 63) == 0)
        buf[total++] = 0;
    return total;
}

/* Parses one received transfer. Returns the payload length, or -1 if the frame is invalid. */
static inline int deck_parse(const uint8_t *buf, int n, deck_hdr *h, const uint8_t **payload)
{
    if (n < DECK_HDR_SIZE)
        return -1;
    memcpy(h, buf, sizeof(*h));
    if (h->magic != DECK_MAGIC || h->len > (uint32_t)(n - DECK_HDR_SIZE))
        return -1;
    *payload = buf + DECK_HDR_SIZE;
    return (int)h->len;
}

#endif
