#pragma once
#include <optional>
#include <SDL3_image/SDL_image.h>
#include "../core/gl.hpp"

struct TextureRegistry {
private:
    gl::Texture errorTexture;
    std::optional<gl::Texture> atlasTexture, atlasHighlightedTexture, pushButtonTexture;

    static std::optional<gl::Texture> loadTexture(const char *path) {
        SDL_Surface *surface = IMG_Load(path);
        if (surface == nullptr) {
            fprintf(stderr, "Failed to load texture: %s\n", SDL_GetError());
            return std::nullopt;
        }

        SDL_Surface *convertedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        if (convertedSurface == nullptr) {
            fprintf(stderr, "Failed to convert surface: %s\n", SDL_GetError());
            SDL_DestroySurface(surface);
            return std::nullopt;
        }

        gl::Texture texture = gl::Texture();
        glBindTexture(GL_TEXTURE_2D, texture.get());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, convertedSurface->w, convertedSurface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, convertedSurface->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        SDL_DestroySurface(convertedSurface);
        SDL_DestroySurface(surface);
        return texture;
    }
public:
    TextureRegistry() :
        errorTexture(),
        atlasTexture(std::nullopt), atlasHighlightedTexture(std::nullopt),
        pushButtonTexture(std::nullopt)
    {
        glBindTexture(GL_TEXTURE_2D, this->errorTexture.get());
        {
            const uint8_t errorPixels[] = {
                255,   0, 255, 255, /* */   0,   0,   0, 255,
                  0,   0,   0, 255, /* */ 255,   0, 255, 255,
            };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, errorPixels);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        this->atlasTexture = TextureRegistry::loadTexture("assets/atlas.png");
        if (!this->atlasTexture.has_value()) { fprintf(stderr, "Failed to load texture atlas.\n"); }
        this->atlasHighlightedTexture = TextureRegistry::loadTexture("assets/atlas_highlighted.png");
        if (!this->atlasHighlightedTexture.has_value()) { fprintf(stderr, "Failed to load highlighted texture atlas.\n"); }
        this->pushButtonTexture = TextureRegistry::loadTexture("assets/push_button.png");
        if (!this->pushButtonTexture.has_value()) { fprintf(stderr, "Failed to load push button texture.\n"); }
    }
    ~TextureRegistry() = default;

    static TextureRegistry &getInstance() {
        static TextureRegistry instance;
        return instance;
    }

    GLuint getAtlasTexture() const {
        return this->atlasTexture.has_value() ? this->atlasTexture->get() : this->errorTexture.get();
    }
    GLuint getAtlasHighlightedTexture() const {
        return this->atlasHighlightedTexture.has_value() ? this->atlasHighlightedTexture->get() : this->errorTexture.get();
    }
    GLuint getPushButtonTexture() const {
        return this->pushButtonTexture.has_value() ? this->pushButtonTexture->get() : this->errorTexture.get();
    }
};