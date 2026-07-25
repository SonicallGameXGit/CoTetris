#include "core/sdl.hpp"
#include "core/gl.hpp"
#include "game/piece.hpp"
#include "game/map.hpp"
#include "game/button.hpp"

float pixelToWorldX(const float x, const float windowWidth, const float windowHeight) {
    return (x / windowWidth * 2.0f - 1.0f) * (windowWidth / windowHeight);
}
float pixelToWorldY(const float y, const float windowHeight) {
    return (1.0f - y / windowHeight) * 2.0f - 1.0f;
}

int main() {
    const sdl::Context context = sdl::Context(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    if (!context.isValid()) {
        printf("Failed to initialize SDL: %s\n", SDL_GetError());
        return -1;
    }
    float windowWidth = 1280, windowHeight = 720;
    const sdl::Window window = sdl::Window("CoTetris", static_cast<int>(windowWidth), static_cast<int>(windowHeight), SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window.isValid()) {
        printf("Failed to create window: %s\n", SDL_GetError());
        return -1;
    }
    {
        const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
        windowWidth *= pixelDensity;
        windowHeight *= pixelDensity;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    const sdl::GLContext glContext = sdl::GLContext(window.get());
    if (!glContext.isValid()) {
        printf("Failed to create OpenGL context: %s\n", SDL_GetError());
        return -1;
    }
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        printf("Failed to initialize GLAD\n");
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);

    uint8_t mouseButtonsBitmask = 0;
    glm::vec2 mousePosition = glm::vec2();

    srand(static_cast<unsigned int>(SDL_GetTicksNS()));
    Map map = Map();
    std::array<std::array<uint8_t, Piece::width>, Piece::height> prefab = std::array<std::array<uint8_t, Piece::width>, Piece::height>();
    uint8_t brush = 1;

    std::vector<Toggle> toggles = std::vector<Toggle>();
    toggles.reserve(6);
    std::function<void(Toggle &self)> toggleCallback = [&](Toggle &self) {
        self.state = true;
        self.texture = TextureRegistry::getInstance().getAtlasHighlightedTexture();
        for (size_t i = 0; i < toggles.size(); i++) {
            if (&toggles[i] != &self) {
                toggles[i].state = false;
                toggles[i].texture = TextureRegistry::getInstance().getAtlasTexture();
            } else {
                brush = static_cast<uint8_t>(i + 1);
            }
        }
    };
    const float uvSize = 1.0f / 3.0f;
    for (uint8_t i = 1; i < 7; i++) {
        Toggle &toggle = toggles.emplace_back(TextureRegistry::getInstance().getAtlasTexture(), toggleCallback);
        const float uvX = std::fmodf(static_cast<float>(i), 3.0f) * uvSize;
        const float uvY = std::floorf(static_cast<float>(i) / 3.0f) * uvSize;
        toggle.uv = glm::vec4(uvX, uvY, uvSize, uvSize);
        toggle.size = glm::vec2(0.1f);
    }
    std::function<void()> pushPrefabButtonCallback = [&]() {
        for (const std::array<uint8_t, Piece::width> &row : prefab) {
            for (const uint8_t cell : row) {
                if (cell != 0) {
                    goto success;
                }
            }
        }
        return;

        success:
        if (map.pushPrefab(prefab)) {
            printf("Pushed prefab to map.\n");
            std::fill(prefab.begin(), prefab.end(), std::array<uint8_t, Piece::width>());
        }
    };
    Button pushPrefabButton = Button(TextureRegistry::getInstance().getPushButtonTexture(), [&](Button &self) {
        pushPrefabButtonCallback();
    });
    pushPrefabButton.size = glm::vec2(0.1f);

    const float slowTickRate = 1.0f / 2.0f;
    const float fastTickRate = 1.0f / 12.0f;
    float tickRate = slowTickRate;
    float tickTimer = 0.0f;

    Uint64 lastTime = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT: {
                    running = false;
                    break;
                }
                case SDL_EVENT_WINDOW_RESIZED: {
                    const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
                    windowWidth = static_cast<float>(event.window.data1) * pixelDensity;
                    windowHeight = static_cast<float>(event.window.data2) * pixelDensity;
                    glViewport(0, 0, static_cast<int>(windowWidth), static_cast<int>(windowHeight));
                    break;
                }
                case SDL_EVENT_KEY_DOWN: {
                    switch (event.key.key) {
                        case SDLK_LEFT:
                        case SDLK_A: {
                            map.movePieceLeft();
                            break;
                        }
                        case SDLK_RIGHT:
                        case SDLK_D: {
                            map.movePieceRight();
                            break;
                        }
                        case SDLK_UP:
                        case SDLK_W:
                        case SDLK_R: {
                            map.rotatePiece();
                            break;
                        }
                        case SDLK_S:
                        case SDLK_DOWN:
                        case SDLK_SPACE:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT: {
                            if (!event.key.repeat) {
                                tickTimer = 0.0f;
                                tickRate = fastTickRate;
                                map.tick();
                            }
                            break;
                        }
                        case SDLK_RETURN: {
                            pushPrefabButtonCallback();
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    break;
                }
                case SDL_EVENT_KEY_UP: {
                    switch (event.key.key) {
                        case SDLK_S:
                        case SDLK_DOWN:
                        case SDLK_SPACE:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT: {
                            tickRate = slowTickRate;
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    mouseButtonsBitmask |= SDL_BUTTON_MASK(event.button.button);
                    const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
                    mousePosition.x = pixelToWorldX(static_cast<float>(event.button.x) * pixelDensity, windowWidth, windowHeight);
                    mousePosition.y = pixelToWorldY(static_cast<float>(event.button.y) * pixelDensity, windowHeight);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    mouseButtonsBitmask &= ~SDL_BUTTON_MASK(event.button.button);
                    const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
                    mousePosition.x = pixelToWorldX(static_cast<float>(event.button.x) * pixelDensity, windowWidth, windowHeight);
                    mousePosition.y = pixelToWorldY(static_cast<float>(event.button.y) * pixelDensity, windowHeight);
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION: {
                    const float pixelDensity = SDL_GetWindowPixelDensity(window.get());
                    mousePosition.x = pixelToWorldX(static_cast<float>(event.motion.x) * pixelDensity, windowWidth, windowHeight);
                    mousePosition.y = pixelToWorldY(static_cast<float>(event.motion.y) * pixelDensity, windowHeight);
                    break;
                }
                default: {
                    break;
                }
            }
        }

        Uint64 currentTime = SDL_GetTicksNS();
        const float deltaTime = static_cast<float>(currentTime - lastTime) / 1e9f;
        lastTime = currentTime;

        tickTimer += deltaTime;
        if (tickTimer >= tickRate) {
            tickTimer -= tickRate;
            map.tick();
        }

        const float aspectRatio = windowWidth / windowHeight;
        for (size_t i = 0; i < toggles.size(); i++) {
            Toggle &toggle = toggles[i];
            // FIXME: For now here and in many places in the code, we assume that there's a canvas for prefab drawing at these coordinates and of a specific size, but in future it should be moved into a separate class and share info like it's position and size with the rest of the code.
            const float cellSize = 1.0f / static_cast<float>(Piece::height);
            const float xOffset = 1.0f / static_cast<float>(Map::height) * static_cast<float>(Map::width) + 0.05f;
            const float yOffset = -0.05f;
            toggle.position = glm::vec2(
                xOffset + static_cast<float>(i) * (cellSize * static_cast<float>(Piece::width) / static_cast<float>(toggles.size())),
                yOffset - toggle.size.y - 0.05f
            );
            toggle.update(mousePosition.x, mousePosition.y, (mouseButtonsBitmask & SDL_BUTTON_LMASK) > 0);
        }
        {
            const float xOffset = 1.0f / static_cast<float>(Map::height) * static_cast<float>(Map::width) + 0.05f;
            const float yOffset = -0.05f;
            pushPrefabButton.position = glm::vec2(
                xOffset,
                yOffset - toggles[0].size.y - pushPrefabButton.size.y - 0.1f
            );
            pushPrefabButton.update(mousePosition.x, mousePosition.y, (mouseButtonsBitmask & SDL_BUTTON_LMASK) > 0);
        }
        if ((mouseButtonsBitmask & SDL_BUTTON_LMASK) > 0 || (mouseButtonsBitmask & SDL_BUTTON_RMASK) > 0) {
            const uint8_t maskedBrush = (mouseButtonsBitmask & SDL_BUTTON_LMASK) ? brush : 0;

            const float cellSize = 1.0f / static_cast<float>(Piece::height);
            const float xOffset = 1.0f / static_cast<float>(Map::height) * static_cast<float>(Map::width) + 0.05f;
            const float yOffset = -0.05f;

            const float relativeX = mousePosition.x - xOffset;
            const float relativeY = mousePosition.y - yOffset;

            const int32_t cellX = static_cast<int32_t>(std::floorf(relativeX / cellSize));
            const int32_t cellY = static_cast<int32_t>(std::floorf(relativeY / cellSize));

            if (cellX >= 0 && cellX < static_cast<int32_t>(Piece::width) && cellY >= 0 && cellY < static_cast<int32_t>(Piece::height)) {
                prefab[cellY][cellX] = maskedBrush;
            }
        }

        const glm::mat4 projectionViewMatrix = glm::ortho(-aspectRatio, aspectRatio, -1.0f, 1.0f);

        glClear(GL_COLOR_BUFFER_BIT);
        map.draw(projectionViewMatrix, aspectRatio);

        BasicShader::getInstance().bind();
        glBindVertexArray(MeshRegistry::getInstance().getQuad());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, TextureRegistry::getInstance().getAtlasTexture());
        const float cellSize = 1.0f / static_cast<float>(Piece::height);
        const float xOffset = 1.0f / static_cast<float>(Map::height) * static_cast<float>(Map::width) + 0.05f;
        const float yOffset = -0.05f;
        for (uint32_t x = 0; x < Piece::width; x++) {
            for (uint32_t y = 0; y < Piece::height; y++) {
                const float posX = static_cast<float>(x) * cellSize + xOffset;
                const float posY = static_cast<float>(y) * cellSize + yOffset;
                const glm::mat4 modelMatrix = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(posX, posY, 0.0f)), glm::vec3(cellSize, cellSize, 1.0f));
                BasicShader::getInstance().setProjectionViewModelMatrix(projectionViewMatrix * modelMatrix);
                const uint8_t value = prefab[y][x];
                const float uvX = std::fmodf(static_cast<float>(value), 3.0f) * uvSize;
                const float uvY = std::floorf(static_cast<float>(value) / 3.0f) * uvSize;
                BasicShader::getInstance().setUV(glm::vec4(uvX, uvY, uvSize, uvSize));
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }
        }
        for (Toggle &toggle : toggles) {
            toggle.draw(projectionViewMatrix);
        }
        pushPrefabButton.draw(projectionViewMatrix);
        SDL_GL_SwapWindow(window.get());
    }
}