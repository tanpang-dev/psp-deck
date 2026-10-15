#include <assert.h>
#include <stdio.h>
#include "deck_proto.h"

static uint8_t buf[DECK_MAX_FRAME], src[DECK_MAX_FRAME];

int main(void)
{
    static const uint32_t lens[] = { 0, 1, 55, 56, 57, 504, 4096, DECK_MAX_PAYLOAD };
    deck_hdr h;
    const uint8_t *p;

    for (unsigned i = 0; i < sizeof(src); i++)
        src[i] = (uint8_t)(i * 7);

    for (unsigned i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        int n = deck_frame(buf, DECK_M_STATUS, src, lens[i]);
        assert(n > 0 && n <= DECK_MAX_FRAME);
        assert(n % 64 != 0);
        assert(deck_parse(buf, n, &h, &p) == (int)lens[i]);
        assert(h.type == DECK_M_STATUS && memcmp(p, src, lens[i]) == 0);
    }

    assert(deck_frame(buf, 1, src, DECK_MAX_PAYLOAD + 1) == -1);
    assert(deck_parse(buf, 7, &h, &p) == -1);
    deck_frame(buf, 1, src, 100);
    assert(deck_parse(buf, 50, &h, &p) == -1);
    buf[0] ^= 1;
    assert(deck_parse(buf, 108, &h, &p) == -1);

    puts("test_proto: ok");
    return 0;
}
