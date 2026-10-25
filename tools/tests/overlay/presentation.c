/* Exercise the actual backend composition with a synthetic scanout and settings
   layer. Including it gives the test access to the loop and an offscreen software
   render target, so output pixels can differ from window points on any CI host. */
#include <assert.h>
#include <math.h>
#include <SDL3/SDL.h>
static bool test_present(SDL_Renderer *renderer);
#define SDL_RenderPresent test_present
#include "../../../runtime/port/platform/sdl3/plat_sdl3.c"
#undef SDL_RenderPresent

/* Avoid a CRT assertion dialog in unattended Windows checks. */
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s (%s)\n", __LINE__, #c, SDL_GetError()); exit(1); } } while (0)

static int shown, calls, mode = 1, lw = 320, lh = 200;
static PlatOverlay last;
static uint8_t *presented;
static size_t presented_size;
int (*settings_pick_folder)(const char *);
int settings_open(void) { return shown; }
void settings_show(int on) { shown = on; }
void settings_key(int k) { (void)k; }
void settings_pointer(int x, int y, int down) { (void)x; (void)y; (void)down; }
void settings_wheel(int steps) { (void)steps; }
void settings_folder_chosen(const char *p) { (void)p; }
void port_pause(int on) { (void)on; }
void port_crash_handlers(void) { }
int settings_draw(uint32_t *p)
{
    int i;
    if (!shown) return 0;
    for (i = 0; i < SET_W * SET_H; i++) p[i] = 0xff00ff00;
    return 1;
}
static void scanout(uint8_t *p, int *w, int *h, uint8_t pal[768])
{
    *w = lw; *h = lh;
    memset(p, 1, (size_t)lw * lh);
    memset(pal, 0, 768);
    pal[3] = 16; pal[4] = 32; pal[5] = 48; /* 65,130,195 */
}
static void put(const PlatOverlay *f, int x, int y, int a)
{
    uint8_t *p = f->rgba + (size_t)y * f->pitch + (size_t)x * 4;
    p[0] = 255; p[1] = 0; p[2] = 0; p[3] = (uint8_t)a;
}
static int overlay(const PlatOverlay *f, void *data)
{
    size_t n = (size_t)f->pitch * f->height, i;
    int x = (int)(f->viewport_x + f->viewport_w / 2);
    int y = (int)(f->viewport_y + f->viewport_h / 2);
    assert(data == &mode);
    assert(f->pitch == f->width * 4);
    assert(f->logical_width == lw && f->logical_height == lh);
    for (i = 0; i < n; i++) assert(f->rgba[i] == 0); /* cleared every frame */
    calls++;
    last = *f; last.rgba = NULL; /* metadata only, never retain the buffer */
    if (mode == 1) {
        put(f, x, y, 255);       /* one OUTPUT pixel, not one game pixel */
        put(f, x + 2, y, 128);
        put(f, 0, 0, 255);       /* output-space content may cover letterbox */
    }
    if (mode == 2) memset(f->rgba, 255, n); /* return-zero must suppress it */
    return mode != 2;
}
static uint8_t *read_pixels(SDL_Renderer *renderer)
{
    SDL_Surface *s = SDL_RenderReadPixels(renderer, NULL);
    SDL_Surface *c;
    uint8_t *p;
    int y;
    assert(s);
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGB24);
    assert(c);
    presented_size = (size_t)c->w * c->h * 3;
    p = malloc(presented_size); assert(p);
    for (y = 0; y < c->h; y++) memcpy(p + (size_t)y * c->w * 3,
        (uint8_t *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 3);
    SDL_DestroySurface(c); SDL_DestroySurface(s);
    return p;
}
/* Read before Present: a host swapchain's backbuffer is undefined afterwards. */
static bool test_present(SDL_Renderer *renderer)
{
    free(presented);
    presented = read_pixels(renderer);
    return SDL_RenderPresent(renderer);
}
static uint8_t *capture(void)
{
    uint8_t *p = malloc(presented_size); assert(p && presented);
    memcpy(p, presented, presented_size); return p;
}
static void rgb(const uint8_t *p, int w, int x, int y, int r, int g, int b)
{
    const uint8_t *v = p + ((size_t)y * w + x) * 3;
    if (abs(v[0] - r) > 2 || abs(v[1] - g) > 2 || abs(v[2] - b) > 2)
        fprintf(stderr, "pixel (%d,%d): %u,%u,%u expected %d,%d,%d\n", x, y, v[0], v[1], v[2], r, g, b);
    assert(abs(v[0] - r) <= 2 && abs(v[1] - g) <= 2 && abs(v[2] - b) <= 2);
}
static void frame(void)
{
    L.hidden_win = 0;
    loop_step();
}
static void check(int output_w, int output_h)
{
    uint8_t *base, *painted, *empty;
    int x, y, before = calls;
    float ah = live.aspect ? lw * 0.75f : (float)lh;
    float scale = fminf((float)output_w / lw, (float)output_h / ah);
    if (live.integer_scale && scale >= 1) scale = floorf(scale);
    ((PlatHooks *)hooks)->overlay = NULL;
    frame(); base = capture();
    ((PlatHooks *)hooks)->overlay = overlay;
    mode = 1; frame(); painted = capture();
    assert(calls == before + 1 && last.width == output_w && last.height == output_h);
    assert(fabsf(last.viewport_w - lw * scale) < .01f);
    assert(fabsf(last.viewport_h - ah * scale) < .01f);
    assert(fabsf(last.viewport_x - (output_w - lw * scale) / 2) < .01f);
    assert(fabsf(last.viewport_y - (output_h - ah * scale) / 2) < .01f);
    x = (int)(last.viewport_x + last.viewport_w / 2);
    y = (int)(last.viewport_y + last.viewport_h / 2);
    rgb(painted, output_w, x, y, 255, 0, 0);
    rgb(painted, output_w, x + 1, y, 65, 130, 195);
    rgb(painted, output_w, x + 2, y, 160, 65, 97);
    rgb(painted, output_w, 0, 0, 255, 0, 0);
    mode = 2; frame(); empty = capture();
    assert(!memcmp(base, empty, (size_t)output_w * output_h * 3)); free(empty);
    mode = 0; frame(); empty = capture(); /* all-transparent equals no hook */
    assert(!memcmp(base, empty, (size_t)output_w * output_h * 3)); free(empty);
    mode = 1; shown = 1; frame(); empty = capture();
    rgb(empty, output_w, x, y, 0, 255, 0); /* settings above overlay */
    free(empty); shown = 0;
    free(base); free(painted);
}
static void release(void)
{
    if (L.tex) SDL_DestroyTexture(L.tex);
    if (L.ltex) SDL_DestroyTexture(L.ltex);
    if (L.otex) SDL_DestroyTexture(L.otex);
    free(L.overlay);
    SDL_DestroyRenderer(L.ren); SDL_DestroyWindow(L.win);
    memset(&L, 0, sizeof L); g_win = NULL;
    free(presented); presented = NULL; presented_size = 0;
}
static int idle(void *p) { (void)p; return 0; }

int main(int argc, char **argv)
{
    PlatHooks h = {0};
    int a, integer, size, n = 0;
    const int dims[][2] = {{960,720}, {1001,733}, {240,150}, {1600,1200}};
    h.scanout = scanout; h.overlay_data = &mode;
    hooks = &h;
    if (argc > 1 && !strcmp(argv[1], "hidden")) {
        PlatConfig c = {0}; c.hidden = 1; c.exit_after_ms = 100;
        h.overlay = overlay;
        assert(plat_run(&c, &h, idle, NULL) == 0);
        assert(!calls && !L.overlay && !L.otex);
        puts("PASS hidden: no overlay allocation or callback"); return 0;
    }
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    for (size = 0; size < 4; size++) {
        SDL_Surface *target = SDL_CreateSurface(dims[size][0], dims[size][1], SDL_PIXELFORMAT_RGBA32);
        assert(target);
        L.win = SDL_CreateWindow("Overlay synthetic pixels", 400, 300, SDL_WINDOW_HIDDEN);
        assert(L.win);
        L.ren = SDL_CreateSoftwareRenderer(target); assert(L.ren);
        L.ltex = SDL_CreateTexture(L.ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, SET_W, SET_H);
        assert(L.ltex); SDL_SetTextureBlendMode(L.ltex, SDL_BLENDMODE_BLEND);
        L.start = SDL_GetTicks(); L.frame_ns = 1;
        for (a = 0; a < 2; a++) for (integer = 0; integer < 2; integer++) {
            live.aspect = a; live.integer_scale = integer;
            check(dims[size][0], dims[size][1]); n++;
            assert(last.window_width == 400 && last.window_height == 300);
        }
        /* A scanout mode change must update the logical metadata and mapping. */
        lw = 640; lh = 480; check(dims[size][0], dims[size][1]); n++;
        lw = 320; lh = 200;
        release(); SDL_DestroySurface(target);
    }
    /* Real host renderer, resize/fullscreen/return. No synthetic HiDPI claim here. */
    L.win = SDL_CreateWindow("Overlay resize probe", 640, 480,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    assert(L.win); g_win = L.win;
    L.ren = SDL_CreateRenderer(L.win, NULL); assert(L.ren);
    L.ltex = SDL_CreateTexture(L.ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, SET_W, SET_H);
    assert(L.ltex); SDL_SetTextureBlendMode(L.ltex, SDL_BLENDMODE_BLEND);
    L.frame_ns = 1; L.start = SDL_GetTicks();
    for (size = 0; size < 4; size++) {
        int w, hh;
        SDL_SetWindowSize(L.win, size % 2 ? 853 : 640, size % 2 ? 517 : 480);
        SDL_SyncWindow(L.win); SDL_PumpEvents();
        live.aspect = size / 2; live.integer_scale = size % 2;
        assert(SDL_GetCurrentRenderOutputSize(L.ren, &w, &hh));
        check(w, hh); n++;
    }
    assert(SDL_SetWindowFullscreen(L.win, true));
    SDL_SyncWindow(L.win); SDL_PumpEvents();
    assert(SDL_GetWindowFlags(L.win) & SDL_WINDOW_FULLSCREEN);
    { int w, hh; SDL_GetCurrentRenderOutputSize(L.ren, &w, &hh); check(w, hh); n++; }
    plat_set_display(0, 2, 1, 1); SDL_SyncWindow(L.win); SDL_PumpEvents();
    assert(!(SDL_GetWindowFlags(L.win) & SDL_WINDOW_FULLSCREEN));
    { int w, hh; SDL_GetCurrentRenderOutputSize(L.ren, &w, &hh); check(w, hh); n++; }
    if (argc > 1) {
        live.screenshot_after_ms = 1; live.screenshot_path = "overlay-scanout.png";
        live.window_shot_path = argv[1]; L.shot = 0; mode = 1;
        frame();
    }
    release(); SDL_Quit();
    printf("PASS %d composition scenarios: alpha, native pixel, mapping, clear, off, settings, resize, fullscreen\n", n);
    return 0;
}
