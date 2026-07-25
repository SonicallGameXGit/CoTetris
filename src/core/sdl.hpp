#pragma once
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

namespace sdl {
    struct Context {
    private:
        bool valid;
    public:
        Context(SDL_InitFlags flags);
        ~Context();
        bool isValid() const;
    };
    struct NetContext {
    private:
        bool valid;
    public:
        NetContext();
        ~NetContext();
        bool isValid() const;
    };
    struct NetServer {
    private:
        NET_Server *server;
    public:
        NetServer(NET_Address *addr, Uint16 port, SDL_PropertiesID props);
        ~NetServer();
        NET_Server *get() const;
        bool isValid() const;
    };
    struct NetAddress {
    private:
        NET_Address *address;
    public:
        NetAddress(NET_Address *address);
        ~NetAddress();
        NET_Address *get() const;
        bool isValid() const;
    };
    struct NetStreamSocket {
    private:
        NET_StreamSocket *socket;
    public:
        NetStreamSocket(NET_Address *addr, Uint16 port, SDL_PropertiesID props);
        ~NetStreamSocket();
        NET_StreamSocket *get() const;
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