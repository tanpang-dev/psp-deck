/* Running-app carousel (DESIGN §4.5): icons on an arc, scaled and faded by depth, driven by a
 * critically damped spring so held-down repeats and retargeting never stutter. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "apps.h"
#include "ui.h"

#define ICON 96            /* as sent by the Mac */
#define TEX 128            /* GU textures are power-of-two sized; the icon sits in the top-left corner */
#define MAX_APPS 64
#define STEP_RAD 0.5236f   /* 30° between neighbours */
#define HIDE_RAD 1.75f     /* beyond ~100° an icon is behind the arc */
#define SPRING_W 18.0f
#define REPEAT_DELAY_US 350000u
#define REPEAT_RATE_US 120000u
#define SNAP_BACK_US 2000000u
#define SETTLE_US 250000u      /* switch apps once the carousel has rested this long */
#define CENTER_X 240.0f
#define CENTER_Y 116.0f
#define RADIUS 170.0f

typedef struct {
    unsigned pid;
    char name[64];
    unsigned int *tex;
} app_t;

typedef struct {
    int count, filled;
    app_t app[MAX_APPS];
} app_list;

static app_list *g_cur, *g_ready, *g_staging;  /* g_ready is handed from the link thread under g_lock */
static SceUID g_lock = -1;
static float g_pos, g_vel;
static int g_sel;
static unsigned g_last_nav, g_repeat_at;
static int g_pending;  /* the user moved the carousel and that app has not been brought forward yet */

static void list_free(app_list *l)
{
    int i;

    if (!l)
        return;
    for (i = 0; i < l->count; i++)
        free(l->app[i].tex);
    free(l);
}

static unsigned le32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

void apps_init(void)
{
    g_lock = sceKernelCreateSema("apps", 0, 1, 1, NULL);
}

void apps_on_message(const uint8_t *p, int len)
{
    int index, count, k;
    app_t *a;

    if (len < 8)
        return;
    index = p[0];
    count = p[1] < MAX_APPS ? p[1] : MAX_APPS;
    if (index == 0) {
        list_free(g_staging);
        g_staging = calloc(1, sizeof(app_list));
        if (!g_staging)
            return;
        g_staging->count = count;
    }
    if (!g_staging || index >= g_staging->count || len < 8 + ICON * ICON * 4)
        goto publish;

    a = &g_staging->app[index];
    a->pid = le32(p + 4);
    a->tex = memalign(16, TEX * TEX * 4);
    if (a->tex) {
        memset(a->tex, 0, TEX * TEX * 4);
        for (k = 0; k < ICON; k++)
            memcpy(a->tex + k * TEX, p + 8 + k * ICON * 4, ICON * 4);
        sceKernelDcacheWritebackRange(a->tex, TEX * TEX * 4);
    }
    {
        const uint8_t *s = p + 8 + ICON * ICON * 4, *end = p + len;
        int n = 0;
        while (s < end && *s && n < (int)sizeof(a->name) - 1)
            a->name[n++] = *s++;
        a->name[n] = 0;
    }
    g_staging->filled++;

publish:
    if (g_staging && g_staging->filled >= g_staging->count) {
        sceKernelWaitSema(g_lock, 1, NULL);
        list_free(g_ready);  /* a newer list arrived before the main loop took this one */
        g_ready = g_staging;
        sceKernelSignalSema(g_lock, 1);
        g_staging = NULL;
    }
}

static int find_pid(const app_list *l, unsigned pid)
{
    int i;

    for (i = 0; l && i < l->count; i++)
        if (l->app[i].pid == pid)
            return i;
    return -1;
}

void apps_frame_begin(void)
{
    app_list *next = NULL, *old;
    int keep;

    sceKernelWaitSema(g_lock, 1, NULL);
    next = g_ready;
    g_ready = NULL;
    sceKernelSignalSema(g_lock, 1);
    if (!next)
        return;
    /* Keep the selected app selected, and shift the animation with it so nothing jumps. */
    old = g_cur;
    keep = old && g_sel < old->count ? find_pid(next, old->app[g_sel].pid) : -1;
    if (keep >= 0) {
        g_pos += keep - g_sel;
        g_sel = keep;
    }
    g_cur = next;
    list_free(old);
}

int apps_count(void)
{
    return g_cur ? g_cur->count : 0;
}

static void move(int dir)
{
    int n = apps_count();

    g_sel += dir;
    if (g_sel < 0)
        g_sel = 0;
    if (g_sel > n - 1)
        g_sel = n - 1;
}

unsigned apps_update(unsigned pressed, unsigned held, int use_circle, unsigned front_pid)
{
    unsigned now = sceKernelGetSystemTimeLow(), go = 0;
    int n = apps_count(), dir = 0, front;
    const float dt = 1.0f / 60;

    if (n == 0)
        return 0;
    if (g_sel > n - 1)
        g_sel = n - 1;

    if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
        dir = pressed & PSP_CTRL_LEFT ? -1 : 1;
        g_repeat_at = now + REPEAT_DELAY_US;
    } else if ((held & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) && (int)(now - g_repeat_at) >= 0) {
        dir = held & PSP_CTRL_LEFT ? -1 : 1;
        g_repeat_at = now + REPEAT_RATE_US;
    }
    if (dir) {
        move(dir);
        g_last_nav = now;
        g_pending = 1;
    }
    if (use_circle && (pressed & PSP_CTRL_CIRCLE)) {
        go = g_cur->app[g_sel].pid;
        g_last_nav = now;
        g_pending = 0;
    }
    /* No confirm button needed: once the user stops on an app, bring it to the front. Waiting for the
     * carousel to rest keeps a held-down D-pad from flashing every app it passes. */
    if (g_pending && now - g_last_nav > SETTLE_US && !(held & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
        g_pending = 0;
        if (g_cur->app[g_sel].pid != front_pid)
            go = g_cur->app[g_sel].pid;
    }
    /* After a while without navigation, follow whatever the Mac has in front. */
    front = find_pid(g_cur, front_pid);
    if (front >= 0 && front != g_sel && now - g_last_nav > SNAP_BACK_US)
        g_sel = front;

    /* Critically damped spring towards g_sel (velocity carries over when the target changes). */
    g_vel += (SPRING_W * SPRING_W * (g_sel - g_pos) - 2 * SPRING_W * g_vel) * dt;
    g_pos += g_vel * dt;
    return go;
}

typedef struct {
    float u, v;
    unsigned int color;
    float x, y, z;
} tex_vertex;

static void draw_icon(const unsigned int *tex, float cx, float cy, float size, float alpha)
{
    unsigned a = (unsigned)(alpha * 255) & 0xFF;
    tex_vertex *v = sceGuGetMemory(2 * sizeof(tex_vertex));

    /* Icons are premultiplied, so the vertex color scales RGB and A together. */
    v[0] = (tex_vertex){ 0, 0, a << 24 | a << 16 | a << 8 | a, cx - size / 2, cy - size / 2, 0 };
    v[1] = (tex_vertex){ ICON, ICON, a << 24 | a << 16 | a << 8 | a, cx + size / 2, cy + size / 2, 0 };
    sceGuTexImage(0, TEX, TEX, TEX, tex);
    sceGuTexFlush();
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}

void apps_draw(unsigned front_pid)
{
    int n = apps_count(), order[16], count = 0, i, j, lo, hi, mid;

    if (n == 0)
        return;
    /* Slot glow behind the centre icon. */
    ui_rect(CENTER_X - 58, CENTER_Y - 58, 116, 116, 0x30FFC070);

    lo = (int)floorf(g_pos) - 4;
    hi = (int)ceilf(g_pos) + 4;
    for (i = lo < 0 ? 0 : lo; i <= hi && i < n && count < 16; i++)
        order[count++] = i;
    /* Back to front: farthest from the centre first. */
    for (i = 1; i < count; i++)
        for (j = i; j > 0 && fabsf(order[j] - g_pos) > fabsf(order[j - 1] - g_pos); j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }

    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0xFFFFFFFF, 0);
    for (i = 0; i < count; i++) {
        app_t *a = &g_cur->app[order[i]];
        float th = (order[i] - g_pos) * STEP_RAD, c = cosf(th), size;

        if (fabsf(th) > HIDE_RAD || !a->tex)
            continue;
        size = ICON * (0.5f + 0.5f * c);
        draw_icon(a->tex, CENTER_X + RADIUS * sinf(th), CENTER_Y, size, 0.3f + 0.7f * (c > 0 ? c : 0));
    }
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    /* Front-app marker: drawn after the icons because ui_rect switches texturing off. */
    for (i = 0; i < count; i++) {
        float th = (order[i] - g_pos) * STEP_RAD, c = cosf(th);
        if (g_cur->app[order[i]].pid == front_pid && fabsf(th) <= HIDE_RAD)
            ui_rect(CENTER_X + RADIUS * sinf(th) - 3, CENTER_Y + ICON * (0.5f + 0.5f * c) / 2 + 5, 6, 6, 0xFFFFC070);
    }

    mid = (int)floorf(g_pos + 0.5f);
    if (mid >= 0 && mid < n)
        ui_text_center(CENTER_X, CENTER_Y + 80, 0.85f, 0xFFF5EEEB, 300, g_cur->app[mid].name);
}
