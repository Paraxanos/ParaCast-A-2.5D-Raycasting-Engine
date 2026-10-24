#include "platform/sdl.h"

#include <SDL.h>
#include <stdio.h>

struct pc_window {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    int           w, h;
};

static struct pc_window g_window;

pc_window *pc_window_open(const char *title, int w, int h, int scale, int vsync)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    pc_window *win = &g_window;
    win->w = w;
    win->h = h;
    win->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   w * scale, h * scale, SDL_WINDOW_SHOWN);
    if (!win->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return NULL;
    }

    Uint32 flags = vsync ? SDL_RENDERER_PRESENTVSYNC : 0;
    win->renderer = SDL_CreateRenderer(win->window, -1, flags);
    if (!win->renderer)
        win->renderer = SDL_CreateRenderer(win->window, -1, SDL_RENDERER_SOFTWARE);
    if (!win->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        pc_window_close(win);
        return NULL;
    }
    SDL_RenderSetLogicalSize(win->renderer, w, h);

    /* ABGR8888 packs as A<<24 | B<<16 | G<<8 | R, which is bytes R, G, B, A in memory
     * on little-endian machines: exactly what pc_present_rgba writes. */
    win->texture = SDL_CreateTexture(win->renderer, SDL_PIXELFORMAT_ABGR8888,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!win->texture) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        pc_window_close(win);
        return NULL;
    }
    return win;
}

int pc_window_poll(pc_window *win, pc_input *in, pc_ui *ui)
{
    (void)win;
    int running = 1;
    SDL_Event e;
    ui->automap = ui->clipview = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT ||
            (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE))
            running = 0;
        else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.scancode == SDL_SCANCODE_TAB)
                ui->automap = 1;
            else if (e.key.keysym.scancode == SDL_SCANCODE_C)
                ui->clipview = 1;
        }
    }

    const Uint8 *k = SDL_GetKeyboardState(NULL);
    in->forward = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - k[SDL_SCANCODE_S] - k[SDL_SCANCODE_DOWN];
    in->strafe = k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A];
    in->turn = (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_E]) - (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_Q]);

    /* Opposite keys held together, or W and Up at once, still yield one step. */
    in->forward = in->forward > 0 ? 1 : in->forward < 0 ? -1 : 0;
    return running;
}

void pc_window_present(pc_window *win, const uint32_t *rgba)
{
    SDL_UpdateTexture(win->texture, NULL, rgba, win->w * (int)sizeof(uint32_t));
    SDL_RenderClear(win->renderer);
    SDL_RenderCopy(win->renderer, win->texture, NULL, NULL);
    SDL_RenderPresent(win->renderer);
}

void pc_window_set_title(pc_window *win, const char *title)
{
    SDL_SetWindowTitle(win->window, title);
}

void pc_window_close(pc_window *win)
{
    if (win->texture)
        SDL_DestroyTexture(win->texture);
    if (win->renderer)
        SDL_DestroyRenderer(win->renderer);
    if (win->window)
        SDL_DestroyWindow(win->window);
    win->texture = NULL;
    win->renderer = NULL;
    win->window = NULL;
    SDL_Quit();
}
