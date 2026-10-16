/* L+R tab list (DESIGN §4.4-4.5): Chrome's tabs across all windows, scrolled with a spring. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tabs.h"
#include "ui.h"

#define MAX_TABS 400
#define ROW_H 17.0f
#define TOP 30.0f
#define ROWS 13
#define SPRING_W 20.0f
#define REPEAT_DELAY_US 300000u
#define REPEAT_RATE_US 70000u

typedef struct {
    unsigned window;
    unsigned short index;
    unsigned char active;
    char title[256];
} tab_t;

typedef struct {
    int count;
    tab_t tab[MAX_TABS];
} tab_list;

static tab_list *g_ready;   /* handed over from the link thread under g_lock */
static tab_list *g_cur;
static SceUID g_lock;
static int g_open, g_loading, g_sel;
static float g_scroll, g_vel;
static unsigned g_repeat_at;

void tabs_init(void)
{
    g_lock = sceKernelCreateSema("tabs", 0, 1, 1, NULL);
}

void tabs_on_message(const uint8_t *p, int len)
{
    const uint8_t *end = p + len;
    tab_list *l;
    int count, i;

    if (len < 4 || !(l = calloc(1, sizeof(tab_list))))
        return;
    count = p[0] | p[1] << 8;
    p += 4;
    for (i = 0; i < count && i < MAX_TABS && end - p >= 8; i++) {
        tab_t *t = &l->tab[i];
        int n = p[7];

        t->window = p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
        t->index = p[4] | p[5] << 8;
        t->active = p[6];
        p += 8;
        if (n > end - p)
            n = end - p;
        memcpy(t->title, p, n);
        t->title[n] = 0;
        p += n;
    }
    l->count = i;
    sceKernelWaitSema(g_lock, 1, NULL);
    free(g_ready);
    g_ready = l;
    sceKernelSignalSema(g_lock, 1);
}

void tabs_open(void)
{
    g_open = 1;
    g_loading = 1;
}

int tabs_is_open(void)
{
    return g_open;
}

/* Takes a newly arrived list; the cursor starts on the active tab of the frontmost window. */
static void take_ready(void)
{
    tab_list *l;
    int i;

    sceKernelWaitSema(g_lock, 1, NULL);
    l = g_ready;
    g_ready = NULL;
    sceKernelSignalSema(g_lock, 1);
    if (!l)
        return;
    free(g_cur);
    g_cur = l;
    g_loading = 0;
    g_sel = 0;
    for (i = 0; i < l->count; i++)
        if (l->tab[i].active) {
            g_sel = i;
            break;
        }
    g_scroll = g_sel - ROWS / 2;
    g_vel = 0;
}

int tabs_update(unsigned pressed, unsigned held, unsigned *window, unsigned *index)
{
    unsigned now = sceKernelGetSystemTimeLow();
    int n, step = 0, target;

    take_ready();
    if (pressed & PSP_CTRL_CROSS) {
        g_open = 0;
        return 0;
    }
    n = g_cur && !g_loading ? g_cur->count : 0;
    if (n == 0)
        return 0;

    if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
        step = pressed;
        g_repeat_at = now + REPEAT_DELAY_US;
    } else if ((held & (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) &&
               (int)(now - g_repeat_at) >= 0) {
        step = held;
        g_repeat_at = now + REPEAT_RATE_US;
    }
    if (step & PSP_CTRL_UP)
        g_sel--;
    else if (step & PSP_CTRL_DOWN)
        g_sel++;
    else if (step & PSP_CTRL_LEFT)
        g_sel -= ROWS - 1;
    else if (step & PSP_CTRL_RIGHT)
        g_sel += ROWS - 1;
    if (g_sel < 0)
        g_sel = 0;
    if (g_sel > n - 1)
        g_sel = n - 1;

    /* Keep the cursor away from the edges; the list glides there on a spring. */
    target = g_sel - ROWS / 2;
    if (target > n - ROWS)
        target = n - ROWS;
    if (target < 0)
        target = 0;
    g_vel += (SPRING_W * SPRING_W * (target - g_scroll) - 2 * SPRING_W * g_vel) / 60;
    g_scroll += g_vel / 60;

    if (pressed & PSP_CTRL_CIRCLE) {
        *window = g_cur->tab[g_sel].window;
        *index = g_cur->tab[g_sel].index;
        g_open = 0;
        return 1;
    }
    return 0;
}

void tabs_draw(void)
{
    int n = g_cur && !g_loading ? g_cur->count : 0, i, first;
    char head[64];

    ui_rect(0, 0, UI_W, UI_H, 0xF0201814);
    ui_rect(0, 0, UI_W, 24, UI_RGB(34, 38, 52));
    snprintf(head, sizeof(head), n ? "Chrome のタブ（%d）" : "Chrome のタブ", n);
    ui_text(8, 17, 0.7f, UI_RGB(110, 190, 255), 300, head);
    ui_text(UI_W - 150, 17, 0.55f, UI_RGB(140, 148, 165), 150, "○ 開く  × 閉じる");

    if (g_loading) {
        ui_text(24, 130, 0.9f, UI_RGB(140, 148, 165), UI_W - 48, "読み込み中…");
        return;
    }
    if (n == 0) {
        ui_text(24, 130, 0.9f, UI_RGB(140, 148, 165), UI_W - 48, "Chrome が起動していないか、タブがありません");
        return;
    }
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0, (int)TOP, UI_W, (int)(TOP + ROWS * ROW_H));
    first = (int)g_scroll - 1;
    for (i = first < 0 ? 0 : first; i < n && i <= g_scroll + ROWS; i++) {
        float y = TOP + (i - g_scroll) * ROW_H;
        const tab_t *t = &g_cur->tab[i];

        if (i == g_sel)
            ui_rect(4, y, UI_W - 8, ROW_H, UI_RGB(52, 84, 140));
        if (i > 0 && t->window != g_cur->tab[i - 1].window)
            ui_rect(12, y, UI_W - 24, 1, UI_RGB(90, 96, 110));  /* next window */
        if (t->active)
            ui_rect(10, y + 6, 5, 5, UI_RGB(110, 190, 255));
        ui_text(22, y + 13, 0.62f, UI_RGB(235, 238, 245), UI_W - 34, t->title[0] ? t->title : "（無題）");
    }
    sceGuScissor(0, 0, UI_W, UI_H);
}
