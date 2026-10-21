/* Host-side preflight for descriptors.c: serializes every layout the way the PSP bus driver does
 * (interface, its extra, then each endpoint's 7 bytes + extra) and checks it before macOS ever sees it.
 *   cc -DDECK_HOST_TEST desc_check.c descriptors.c -o /tmp/desc_check && /tmp/desc_check */
#include <assert.h>
#include <stdio.h>
#include "descriptors.h"

static unsigned char out[1024];
static int n;

static void put(const void *p, int len)
{
    assert(n + len <= (int)sizeof(out));
    memcpy(out + n, p, len);
    n += len;
}

static void serialize(struct deck_speed_set *s)
{
    struct ConfigDescriptor *c = &s->conf;

    n = 0;
    put(c, 4);
    put(&c->bNumInterfaces, 5);
    for (int i = 0; i < c->bNumInterfaces; i++) {
        for (unsigned a = 0; a < c->settings[i].numDescriptors; a++) {
            struct InterfaceDescriptor *d = &c->settings[i].descriptors[a];
            put(d, 9);
            if (d->extraLength)
                put(d->extra, d->extraLength);
            for (int e = 0; e < d->bNumEndpoints; e++) {
                struct EndpointDescriptor *ep = &d->endpoints[e];
                unsigned char b[7] = { ep->bLength, ep->bDescriptorType, ep->bEndpointAddress, ep->bmAttributes,
                                       ep->wMaxPacketSize & 0xFF, ep->wMaxPacketSize >> 8, ep->bInterval };
                put(b, 7);
                if (ep->extraLength)
                    put(ep->extra, ep->extraLength);
            }
        }
    }
}

/* Walks the bytes as a host would and checks the structural rules macOS relies on. */
static void check(const struct deck_variant *v, int hs)
{
    int ifaces_seen = 0, iad_before_if1 = 0, saw_if1 = 0, ac_total = 0, ac_sum = 0, in_ac = 0;
    int clock_ok = 0, ot_ok = 0, iso_mps = 0;

    assert(out[0] == 9 && out[1] == 2 && (out[2] | out[3] << 8) == n);
    for (int i = 9; i < n;) {
        int len = out[i], type = out[i + 1];
        assert(len >= 2 && i + len <= n);
        if (type == 4) {
            in_ac = out[i + 5] == 1 && out[i + 6] == 1;
            if (out[i + 3] == 0)
                ifaces_seen++;
            if (out[i + 2] == 1)
                saw_if1 = 1;
            assert(out[i + 5] != 1 || out[i + 7] == 0x20);
        } else if (type == 0x0B) {
            iad_before_if1 = !saw_if1 && out[i + 2] == 1 && out[i + 3] == 2;
        } else if (type == 0x24 && in_ac) {
            if (out[i + 2] == 1)
                ac_total = out[i + 6] | out[i + 7] << 8;
            if (out[i + 2] == 0x0A)
                clock_ok = out[i + 3] == UAC2_CLOCK_ID;
            if (out[i + 2] == 3)
                ot_ok = out[i + 3] == 2 && out[i + 8] == UAC2_CLOCK_ID;
            ac_sum += len;
        } else if (type == 5) {
            assert(len == 7);
            if (out[i + 2] == 0x83) {
                iso_mps = out[i + 4] | out[i + 5] << 8;
                assert((out[i + 3] & 3) == 1 && out[i + 6] == (hs ? v->hs_interval : 1));
            }
        }
        i += len;
    }
    assert(ifaces_seen == deck_variant_ifaces(v));
    if (v->kind == DECK_KIND_UAC) {
        assert(iad_before_if1 && clock_ok && ot_ok && ac_total == ac_sum);
        assert(deck_dev_hi.bDeviceClass == 0xEF);
    }
    if (v->kind != DECK_KIND_NONE)
        assert(iso_mps >= (MIC_RATE / deck_iso_rate(v, hs) + 1) * 2 && iso_mps <= (hs ? 1024 : 1023));
}

int main(void)
{
    static struct deck_speed_set s;
    struct DeviceDescriptor dev;

    for (int v = 0; v < DECK_NVARIANTS; v++) {
        for (int hs = 1; hs >= 0; hs--) {
            deck_desc_build(&s, &dev, &deck_variants[v], hs);
            deck_dev_hi = dev;
            serialize(&s);
            check(&deck_variants[v], hs);
            printf("%-28s %s  %3d bytes ok\n", deck_variants[v].name, hs ? "hs" : "fs", n);
            if (v == 0 && hs) {
                for (int i = 0; i < n;) {
                    printf("   ");
                    for (int k = 0; k < out[i]; k++)
                        printf(" %02x", out[i + k]);
                    printf("\n");
                    i += out[i];
                }
            }
        }
    }
    return 0;
}
