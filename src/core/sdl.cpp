#include "sdl.hpp"

namespace sdl {
    Context::Context(SDL_InitFlags flags) : valid(false) {
        this->valid = SDL_Init(flags);
    }
    Context::~Context() {
        SDL_Quit();
    }
    bool Context::isValid() const { return this->valid; }

    NetContext::NetContext() : valid(false) {
        this->valid = NET_Init();
    }
    NetContext::~NetContext() {
        NET_Quit();
    }
    bool NetContext::isValid() const { return this->valid; }

    NetServer::NetServer(NET_Address *addr, Uint16 port, SDL_PropertiesID props) : server(nullptr) {
        this->server = NET_CreateServer(addr, port, props);
    }
    NetServer::~NetServer() {
        NET_DestroyServer(this->server);
    }
    NET_Server *NetServer::get() const { return this->server; }
    bool NetServer::isValid() const { return this->server != nullptr; }

    NetAddress::NetAddress(NET_Address *address) : address(address) {}
    NetAddress::~NetAddress() {
        NET_UnrefAddress(this->address);
    }
    NET_Address *NetAddress::get() const {
        return this->address;
    }
    bool NetAddress::isValid() const { return this->address != nullptr; }

    NetStreamSocket::NetStreamSocket(NET_Address *addr, Uint16 port, SDL_PropertiesID props) : socket(nullptr) {
        this->socket = NET_CreateClient(addr, port, props);
    }
    NetStreamSocket::~NetStreamSocket() {
        NET_DestroyStreamSocket(this->socket);
    }
    NET_StreamSocket *NetStreamSocket::get() const { return this->socket; }
    bool NetStreamSocket::isValid() const { return this->socket != nullptr; }

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