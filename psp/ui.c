/* GU double buffering + intraFont text (docs/DESIGN.md §4.5). */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <string.h>
#include "ui.h"

#define BUF_W 512
#define FB_SIZE (BUF_W * UI_H * 4)

intraFont *ui_font;
static unsigned int g_list[64 * 1024] __attribute__((aligned(16)));

typedef struct {
    unsigned int color;
    short x, y, z;
} color_vertex;

int ui_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(UI_W, UI_H, (void *)FB_SIZE, BUF_W);
    sceGuOffset(2048 - UI_W / 2, 2048 - UI_H / 2);
    sceGuViewport(2048, 2048, UI_W, UI_H);
    sceGuScissor(0, 0, UI_W, UI_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    intraFontInit();
    ui_font = intraFontLoad("flash0:/font/jpn0.pgf", INTRAFONT_STRING_UTF8 | INTRAFONT_CACHE_LARGE);
    return ui_font ? 0 : -1;
}

void ui_term(void)
{
    if (ui_font)
        intraFontUnload(ui_font);
    intraFontShutdown();
    sceGuTerm();
}

void ui_begin(unsigned int clear_color)
{
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(clear_color);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

void ui_end(void)
{
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

void ui_rect(float x, float y, float w, float h, unsigned int color)
{
    color_vertex *v = sceGuGetMemory(2 * sizeof(color_vertex));

    v[0] = (color_vertex){ color, (short)x, (short)y, 0 };
    v[1] = (color_vertex){ color, (short)(x + w), (short)(y + h), 0 };
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

/* Byte length of the UTF-8 prefix that, followed by "…", fits in max_w. */
static int fit_prefix(const char *s, int len, float max_w)
{
    static char tmp[512];

    while (len > 0) {
        do
            len--;
        while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80);  /* back up to a character start */
        memcpy(tmp, s, len);
        strcpy(tmp + len, "…");
        if (intraFontMeasureText(ui_font, tmp) <= max_w)
            break;
    }
    return len;
}

float ui_text(float x, float y, float size, unsigned int color, float max_w, const char *utf8)
{
    static char buf[512];
    int len = strlen(utf8);

    if (!ui_font)
        return 0;
    if (len > (int)sizeof(buf) - 4)
        len = sizeof(buf) - 4;
    intraFontSetStyle(ui_font, size, color, 0, 0, 0);
    memcpy(buf, utf8, len);
    buf[len] = 0;
    if (intraFontMeasureText(ui_font, buf) > max_w) {
        len = fit_prefix(utf8, len, max_w);
        strcpy(buf + len, "…");
    }
    return intraFontPrint(ui_font, x, y, buf) - x;
}
