#include "sdl.hpp"

namespace sdl {
    Context::Context(SDL_InitFlags flags) : valid(false) {
        this->valid = SDL_Init(flags);
    }
    Context::~Context() {
        SDL_Quit();
    }
    bool Context::isValid() const { return this->valid; }

    Window::Window(const char *title, int width, int height, SDL_WindowFlags flags) : window(nullptr) {
        this->window = SDL_CreateWindow(title, width, height, flags);
    }
    Window::~Window() {
        SDL_DestroyWindow(this->window);
    }
    SDL_Window *Window::get() const { return this->window; }
    bool Window::isValid() const { return this->window != nullptr; }
    
    GLContext::GLContext(SDL_Window *window) : glContext(nullptr) {
        this->glContext = SDL_GL_CreateContext(window);
    }
    GLContext::~GLContext() {
        SDL_GL_DestroyContext(this->glContext);
    }
    SDL_GLContext GLContext::get() const { return this->glContext; }
    bool GLContext::isValid() const { return this->glContext != nullptr; }
}