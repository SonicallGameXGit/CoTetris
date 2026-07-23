#pragma once
#include <SDL3/SDL.h>

namespace sdl {
    struct Context {
    private:
        bool valid;
    public:
        Context(SDL_InitFlags flags);
        ~Context();
        bool isValid() const;
    };
    struct Window {
    private:
        SDL_Window *window;
    public:
        Window(const char *title, int width, int height, SDL_WindowFlags flags);
        ~Window();
        SDL_Window *get() const;
        bool isValid() const;
    };
    struct GLContext {
    private:
        SDL_GLContext glContext;
    public:
        GLContext(SDL_Window *window);
        ~GLContext();
        SDL_GLContext get() const;
        bool isValid() const;
    };
}