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

int32_t byteBufferToInt32(const std::vector<uint8_t>::iterator &buffer) {
    int32_t value = 0;
    for (size_t i = 0; i < 4; i++) {
        value |= static_cast<int32_t>(*(buffer + i)) << (i * 8);
    }
    return value;
}
std::vector<uint8_t>::iterator int32ToByteBuffer(const int32_t value, std::vector<uint8_t>::iterator &buffer) {
    for (size_t i = 0; i < 4; i++) {
        *(buffer + i) = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    return buffer + 4;
}
uint16_t byteBufferToUInt16(const std::vector<uint8_t>::iterator &buffer) {
    uint16_t value = 0;
    for (size_t i = 0; i < 2; i++) {
        value |= static_cast<uint16_t>(*(buffer + i)) << (i * 8);
    }
    return value;
}
std::vector<uint8_t>::iterator uint16ToByteBuffer(const uint16_t value, std::vector<uint8_t>::iterator &buffer) {
    for (size_t i = 0; i < 2; i++) {
        *(buffer + i) = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    return buffer + 2;
}
int16_t byteBufferToInt16(const std::vector<uint8_t>::iterator &buffer) {
    int16_t value = 0;
    for (size_t i = 0; i < 2; i++) {
        value |= static_cast<int16_t>(*(buffer + i)) << (i * 8);
    }
    return value;
}
void int16ToByteBuffer(const int16_t value, std::vector<uint8_t>::iterator &buffer) {
    for (size_t i = 0; i < 2; i++) {
        *(buffer + i) = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}
std::string byteBufferToString(const std::vector<uint8_t>::iterator &buffer, const size_t length) {
    return std::string(buffer, buffer + length);
}
std::vector<uint8_t>::iterator stringToByteBuffer(const std::string &str, std::vector<uint8_t>::iterator &buffer) {
    for (size_t i = 0; i < str.size(); i++) {
        *(buffer + i) = static_cast<uint8_t>(str[i]);
    }
    return buffer + str.size();
}

struct ClientState {
private:
    NET_StreamSocket *socket;
    std::vector<uint8_t> buffer;
    size_t bufferPosition;
    bool own;
public:
    ClientState(NET_StreamSocket *socket, bool own) : socket(socket), buffer(), bufferPosition(0), own(own) {}
    ~ClientState() {
        if (this->own) {
            NET_DestroyStreamSocket(this->socket);
        }
    }

    void append(const uint8_t *data, size_t length) {
        this->buffer.insert(this->buffer.end(), data, data + length);
    }
    std::optional<std::vector<uint8_t>::iterator> read(size_t length) {
        if (bufferPosition + length > this->buffer.size()) {
            this->bufferPosition = 0;
            return std::nullopt;
        }
        std::vector<uint8_t>::iterator it = this->buffer.begin() + bufferPosition;
        bufferPosition += length;
        return it;
    }
    void clear() {
        this->buffer.clear();
        this->bufferPosition = 0;
    }
    void flush() {
        this->buffer.erase(this->buffer.begin(), this->buffer.begin() + this->bufferPosition);
        this->bufferPosition = 0;
    }
    size_t getBufferSize() const { return this->buffer.size(); }

    NET_StreamSocket *getSocket() const { return this->socket; }
};

int client(const char *ip, Uint16 port) {
    printf("Resolving host %s...\n", ip);
    sdl::NetAddress netAddress = sdl::NetAddress(NET_ResolveHostname(ip));
    if (!netAddress.isValid()) {
        fprintf(stderr, "Failed to resolve host: %s\n", SDL_GetError());
        return 1;
    }
    if (NET_WaitUntilResolved(netAddress.get(), -1) == NET_FAILURE) {
        fprintf(stderr, "Failed to resolve host: %s\n", SDL_GetError());
        return 1;
    }

    printf("Connecting to server on port %d...\n", port);
    sdl::NetStreamSocket clientSocket = sdl::NetStreamSocket(netAddress.get(), port, 0);
    if (!clientSocket.isValid()) {
        fprintf(stderr, "Failed to connect to server: %s\n", SDL_GetError());
        return 1;
    }
    if (NET_WaitUntilConnected(clientSocket.get(), -1) == NET_FAILURE) {
        fprintf(stderr, "Failed to connect to server: %s\n", SDL_GetError());
        return 1;
    }
    ClientState clientState = ClientState(clientSocket.get(), false);
    printf("Connected successfully!\n");

    float windowWidth = 1280, windowHeight = 720;
    const sdl::Window window = sdl::Window("CoTetris", static_cast<int>(windowWidth), static_cast<int>(windowHeight), SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window.isValid()) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        return 1;
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
        fprintf(stderr, "Failed to create OpenGL context: %s\n", SDL_GetError());
        return 1;
    }
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        return 1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);

    uint8_t mouseButtonsBitmask = 0;
    glm::vec2 mousePosition = glm::vec2();

    srand(static_cast<unsigned int>(SDL_GetTicksNS()));
    ClientMap map = ClientMap();
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
        std::vector<uint8_t> packet = std::vector<uint8_t>();
        packet.reserve(1 + Piece::width * Piece::height);
        packet.push_back(3);
        for (const std::array<uint8_t, Piece::width> &row : prefab) {
            for (const uint8_t cell : row) {
                packet.push_back(cell);
            }
        }
        NET_WriteToStreamSocket(clientState.getSocket(), packet.data(), packet.size());
    };
    Button pushPrefabButton = Button(TextureRegistry::getInstance().getPushButtonTexture(), [&](Button &self) {
        pushPrefabButtonCallback();
    });
    pushPrefabButton.size = glm::vec2(0.1f);

    float tickRate = 1.0f / 60.0f;
    float tickTimer = 0.0f;

    std::optional<uint8_t> playerType = std::nullopt;
    std::function<bool(ClientState &client)> packetCallback = [&](ClientState &client) -> bool {
        std::optional<std::vector<uint8_t>::iterator> packetId = client.read(1);
        if (!packetId.has_value()) { return false; }
        switch (*packetId.value()) {
            case 1: { // Client asked to move piece horizontally.
                std::optional<std::vector<uint8_t>::iterator> reserve = client.read(2);
                if (!reserve.has_value()) { return false; }
                const int16_t x = byteBufferToInt16(reserve.value());
                if (map.piece.has_value()) {
                    map.piece->x = x;
                }
                break;
            }
            case 2: { // Client asked to rotate piece.
                std::optional<std::vector<uint8_t>::iterator> reserve = client.read(1);
                if (!reserve.has_value()) { return false; }
                const uint8_t orientation = *reserve.value();
                if (map.piece.has_value()) {
                    map.piece->rotateTo(orientation);
                }
                break;
            }
            case 3: { // Client pushed prefab to the stack.
                if (playerType.has_value() && playerType.value() == 1) {
                    std::fill(prefab.begin(), prefab.end(), std::array<uint8_t, Piece::width>());
                }
                break;
            }
            case 4: { // Tick.
                std::optional<std::vector<uint8_t>::iterator> reserve = client.read(Map::width * Map::height + 1);
                if (!reserve.has_value()) { return false; }
                for (size_t row = 0; row < Map::height; row++) {
                    for (size_t col = 0; col < Map::width; col++) {
                        map.grid[row][col] = *reserve.value();
                        reserve.value()++;
                    }
                }
                bool pieceExists = static_cast<bool>(*reserve.value());
                if (!pieceExists) { map.piece = std::nullopt; }
                else {
                    reserve = client.read(5 + Piece::width * Piece::height);
                    if (!reserve.has_value()) { return false; }
                    if (!map.piece.has_value()) { map.piece.emplace(0, 0, 0); }

                    map.piece->x = byteBufferToInt16(reserve.value()); *reserve += 2;
                    map.piece->y = byteBufferToInt16(reserve.value()); *reserve += 2;
                    map.piece->orientation = *reserve.value(); (*reserve)++;
                    std::array<std::array<uint8_t, Piece::width>, Piece::height> tempPrefab = std::array<std::array<uint8_t, Piece::width>, Piece::height>();
                    for (size_t row = 0; row < Piece::height; row++) {
                        for (size_t col = 0; col < Piece::width; col++) {
                            tempPrefab[row][col] = *reserve.value();
                            (*reserve)++;
                        }
                    }
                    map.piece->copy(tempPrefab);
                }
                
                break;
            }
            case 5: { // Client type set.
                std::optional<std::vector<uint8_t>::iterator> reserve = client.read(1);
                if (!reserve.has_value()) { return false; }
                playerType = *reserve.value();
                break;
            }
            default: {
                printf("Received unknown packet with ID %d from client.\n", *packetId.value());
                break;
            }
        }

        client.flush();
        return true;
    };

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
                            if (!playerType.has_value() || playerType.value() != 0) { break; }
                            std::vector<uint8_t> packet = std::vector<uint8_t>();
                            packet.reserve(2);
                            packet.push_back(1);
                            packet.push_back(0);
                            NET_WriteToStreamSocket(clientState.getSocket(), packet.data(), packet.size());
                            map.movePieceLeft();
                            break;
                        }
                        case SDLK_RIGHT:
                        case SDLK_D: {
                            if (!playerType.has_value() || playerType.value() != 0) { break; }
                            std::vector<uint8_t> packet = std::vector<uint8_t>();
                            packet.reserve(2);
                            packet.push_back(1);
                            packet.push_back(1);
                            NET_WriteToStreamSocket(clientState.getSocket(), packet.data(), packet.size());
                            map.movePieceRight();
                            break;
                        }
                        case SDLK_UP:
                        case SDLK_W:
                        case SDLK_R: {
                            if (!playerType.has_value() || playerType.value() != 0) { break; }
                            uint8_t packetId = 2;
                            NET_WriteToStreamSocket(clientState.getSocket(), &packetId, 1);
                            map.rotatePiece();
                            break;
                        }
                        case SDLK_S:
                        case SDLK_DOWN:
                        case SDLK_SPACE:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT: {
                            if (!playerType.has_value() || playerType.value() != 0) { break; }
                            if (!event.key.repeat) {
                                std::vector<uint8_t> packet = std::vector<uint8_t>();
                                packet.reserve(2);
                                packet.push_back(0);
                                packet.push_back(1);
                                NET_WriteToStreamSocket(clientState.getSocket(), packet.data(), packet.size());
                            }
                            break;
                        }
                        case SDLK_RETURN: {
                            if (!playerType.has_value() || playerType.value() != 1) { break; }
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
                            if (!playerType.has_value() || playerType.value() != 0) { break; }
                            std::vector<uint8_t> packet = std::vector<uint8_t>();
                            packet.reserve(2);
                            packet.push_back(0);
                            packet.push_back(0);
                            NET_WriteToStreamSocket(clientState.getSocket(), packet.data(), packet.size());
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

            uint8_t buffer[4096];
            int bytesRead = NET_ReadFromStreamSocket(clientState.getSocket(), buffer, sizeof(buffer));
            if (bytesRead < 0) {
                printf("Client disconnected.\n");
                playerType = std::nullopt;
                map = ClientMap();
                std::fill(prefab.begin(), prefab.end(), std::array<uint8_t, Piece::width>());
                brush = 1;
                continue;
            } else if (bytesRead > 0) {
                clientState.append(buffer, static_cast<size_t>(bytesRead));
                while (packetCallback(clientState));
            }
        }

        const float aspectRatio = windowWidth / windowHeight;
        if (playerType.has_value() && playerType.value() == 1) {
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
        }

        const glm::mat4 projectionViewMatrix = glm::ortho(-aspectRatio, aspectRatio, -1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        map.draw(projectionViewMatrix, aspectRatio);

        if (playerType.has_value() && playerType.value() == 1) {
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
        }
        SDL_GL_SwapWindow(window.get());
    }
    return 0;
}
int server(const char *ip, Uint16 port) {
    // An empty address (or one of the "any" spellings) means "listen on every
    // interface", which SDL_net expresses by passing a null address. That is
    // also the only way to get both IPv4 and IPv6 loopback at once, since a
    // resolved address only ever binds the single interface it points at.
    const bool listenOnAllInterfaces = ip == nullptr || *ip == '\0' ||
        strcmp(ip, "any") == 0 || strcmp(ip, "*") == 0 ||
        strcmp(ip, "0.0.0.0") == 0 || strcmp(ip, "::") == 0;

    sdl::NetAddress netAddress = sdl::NetAddress(listenOnAllInterfaces ? nullptr : NET_ResolveHostname(ip));
    if (!listenOnAllInterfaces) {
        if (!netAddress.isValid()) {
            fprintf(stderr, "Failed to resolve host: %s\n", SDL_GetError());
            return 1;
        }
        // Resolving is asynchronous, even for numeric addresses like 127.0.0.1;
        // NET_CreateServer rejects an address that hasn't finished resolving.
        if (NET_WaitUntilResolved(netAddress.get(), -1) == NET_FAILURE) {
            fprintf(stderr, "Failed to resolve host: %s\n", SDL_GetError());
            return 1;
        }
    }
    sdl::NetServer server = sdl::NetServer(netAddress.get(), port, 0);
    if (!server.isValid()) {
        fprintf(stderr, "Failed to create server: %s\n", SDL_GetError());
        return 1;
    }
    printf("Server started on port %d.\n", port);

    ServerMap map = ServerMap();
    const float slowTickRate = 1.0f / 2.0f;
    const float fastTickRate = 1.0f / 12.0f;
    float tickRate = slowTickRate;

    std::vector<std::unique_ptr<ClientState>> clients = std::vector<std::unique_ptr<ClientState>>();
    std::array<ClientState*, 2> players = std::array<ClientState*, 2>();
    std::function<bool(ClientState &client)> packetCallback = [&](ClientState &client) -> bool {
        bool playersReady = true;
        for (const ClientState *player : players) {
            if (player == nullptr) {
                playersReady = false; break;
            }
        }
        std::optional<std::vector<uint8_t>::iterator> packetId = client.read(1);
        if (!packetId.has_value()) { return false; }
        switch (*packetId.value()) {
            case 0: { // Client asked for tick rate change.
                if (!playersReady) {
                    client.clear();
                    return true;
                }
                std::optional<std::vector<uint8_t>::iterator> reserve = client.read(1);
                if (!reserve.has_value()) { return false; }
                bool fast = static_cast<bool>(*reserve.value());
                tickRate = fast ? fastTickRate : slowTickRate;
                break;
            }
            case 1: { // Client asked to move piece horizontally.
                if (!playersReady) {
                    client.clear();
                    return true;
                }
                std::optional<std::vector<uint8_t>::iterator> reserve = client.read(1);
                if (!reserve.has_value()) { return false; }
                bool right = static_cast<bool>(*reserve.value());
                if (right) {
                    map.movePieceRight();
                } else {
                    map.movePieceLeft();
                }

                std::vector<uint8_t> packet = std::vector<uint8_t>();
                packet.reserve(3);
                packet.push_back(*packetId.value());
                const Piece *piece = map.getPiece();
                std::vector<uint8_t>::iterator tempIt = packet.end();
                packet.push_back(0); packet.push_back(0);
                int16ToByteBuffer(piece != nullptr ? piece->x : 0, tempIt);
                for (std::vector<std::unique_ptr<ClientState>>::iterator it = clients.begin(); it != clients.end(); ++it) {
                    ClientState &otherClient = *it->get();
                    if (&otherClient == &client) { continue; }
                    NET_WriteToStreamSocket(otherClient.getSocket(), packet.data(), packet.size());
                }
                break;
            }
            case 2: { // Client asked to rotate piece.
                if (!playersReady) {
                    client.clear();
                    return true;
                }
                map.rotatePiece();
                const Piece *piece = map.getPiece();
                if (piece != nullptr) {
                    std::vector<uint8_t> packet = std::vector<uint8_t>();
                    packet.reserve(2);
                    packet.push_back(*packetId.value());
                    packet.push_back(piece->orientation);
                    for (std::vector<std::unique_ptr<ClientState>>::iterator it = clients.begin(); it != clients.end(); ++it) {
                        ClientState &otherClient = *it->get();
                        if (&otherClient == &client) { continue; }
                        NET_WriteToStreamSocket(otherClient.getSocket(), packet.data(), packet.size());
                    }
                }
                break;
            }
            case 3: { // Client pushed prefab to the stack.
                if (!playersReady) {
                    client.clear();
                    return true;
                }
                std::optional<std::vector<uint8_t>::iterator> lengthIt = client.read(Piece::width * Piece::height);
                if (!lengthIt.has_value()) { return false; }
                std::array<std::array<uint8_t, Piece::width>, Piece::height> prefab = std::array<std::array<uint8_t, Piece::width>, Piece::height>();
                std::vector<uint8_t>::iterator it = lengthIt.value();
                for (size_t y = 0; y < Piece::height; y++) {
                    for (size_t x = 0; x < Piece::width; x++) {
                        prefab[y][x] = *(it++);
                    }
                }
                if (map.pushPrefab(prefab)) {
                    NET_WriteToStreamSocket(client.getSocket(), &(*packetId.value()), 1);
                }
                break;
            }
            default: {
                printf("Received unknown packet with ID %d from client.\n", *packetId.value());
                break;
            }
        }

        client.flush();
        return true;
    };

    float tickTimer = 0.0f;
    Uint64 lastTime = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        NET_StreamSocket *socket = nullptr;
        while (NET_AcceptClient(server.get(), &socket) && socket != nullptr) {
            clients.emplace_back(std::make_unique<ClientState>(socket, true));
            for (size_t i = 0; i < players.size(); i++) {
                if (players[i] == nullptr) {
                    players[i] = clients.back().get();
                    std::vector<uint8_t> packet = std::vector<uint8_t>();
                    packet.reserve(2);
                    packet.push_back(5);
                    packet.push_back(static_cast<uint8_t>(i));
                    NET_WriteToStreamSocket(clients.back()->getSocket(), packet.data(), packet.size());
                    break;
                }
            }
            printf("New client connected!\n");
            socket = nullptr;
        }

        for (std::vector<std::unique_ptr<ClientState>>::iterator it = clients.begin(); it != clients.end(); ) {
            ClientState &client = *it->get();

            uint8_t buffer[4096];
            int bytesRead = NET_ReadFromStreamSocket(client.getSocket(), buffer, sizeof(buffer));
            
            if (bytesRead < 0) {
                printf("Client disconnected.\n");
                for (size_t i = 0; i < players.size(); i++) {
                    if (players[i] == &client) {
                        players[i] = nullptr;
                        break;
                    }
                }
                it = clients.erase(it);
                bool anyPlayersLeft = false;
                for (const ClientState *player : players) {
                    if (player != nullptr) {
                        anyPlayersLeft = true;
                        break;
                    }
                }
                if (!anyPlayersLeft) {
                    map = ServerMap();
                }
                continue;
            } else if (bytesRead > 0) {
                client.append(buffer, static_cast<size_t>(bytesRead));
                while (packetCallback(client));
            }
            ++it;
        }
        
        Uint64 currentTime = SDL_GetTicksNS();
        const float deltaTime = static_cast<float>(currentTime - lastTime) / 1e9f;
        lastTime = currentTime;
        tickTimer += deltaTime;
        if (tickTimer >= tickRate) {
            tickTimer -= tickRate;
            bool playersReady = true;
            for (const ClientState *player : players) {
                if (player == nullptr) {
                    playersReady = false;
                    break;
                }
            }
            if (playersReady) {
                map.tick();
    
                std::vector<uint8_t> packet = std::vector<uint8_t>();
                packet.reserve(1 + Map::width * Map::height + 6 + Piece::width * Piece::height);
                packet.push_back(4);
                for (const std::array<uint8_t, Map::width> &row : map.getGrid()) {
                    for (const uint8_t cell : row) {
                        packet.push_back(cell);
                    }
                }
                const Piece *piece = map.getPiece();
                if (piece != nullptr) {
                    packet.push_back(1);
                    std::vector<uint8_t>::iterator it = packet.end();
                    for (size_t i = 0; i < 4; i++) {
                        packet.push_back(0);
                    }
                    int16ToByteBuffer(piece->x, it); it += 2;
                    int16ToByteBuffer(piece->y, it); it += 2;
                    packet.push_back(piece->orientation);
                    for (size_t row = 0; row < Piece::height; row++) {
                        for (size_t col = 0; col < Piece::width; col++) {
                            packet.push_back(piece->getPrefabCell(row, col));
                        }
                    }
                } else {
                    packet.push_back(0);
                }
    
                for (std::vector<std::unique_ptr<ClientState>>::iterator it = clients.begin(); it != clients.end(); ++it) {
                    ClientState &client = *it->get();
                    NET_WriteToStreamSocket(client.getSocket(), packet.data(), packet.size());
                }
            }
        }

        SDL_Delay(static_cast<Uint32>(1000.0f / 60.0f));
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s server/client ip port\n", argv[0]);
        fprintf(stderr, "  (as a server, use 'any' as the ip to listen on every interface)\n");
        return 1;
    }
    uint8_t netMode = 0;
    if (strcmp(argv[1], "server") == 0) {
        netMode = 1;
    } else if (strcmp(argv[1], "client") == 0) {
        netMode = 2;
    } else {
        fprintf(stderr, "Invalid argument: %s. Must be either 'server' or 'client'.\n", argv[1]);
        return 1;
    }

    std::string ip = std::string(argv[2]);
    Uint16 port = 0;
    try {
        int tempPort = std::stoi(argv[3]);
        if (tempPort < 0 || tempPort > 65535) {
            throw std::out_of_range("Port number out of range");
        }
        port = static_cast<Uint16>(tempPort);
    } catch (const std::invalid_argument &e) {
        fprintf(stderr, "Invalid port number: %s. Must be a valid integer.\n", argv[3]);
        return 1;
    } catch (const std::out_of_range &e) {
        fprintf(stderr, "Port number out of range: %s. Must be between 0 and 65535.\n", argv[3]);
        return 1;
    }

    if (netMode == 1) {
        const sdl::Context context = sdl::Context(SDL_INIT_EVENTS);
        if (!context.isValid()) {
            fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
            return 1;
        }
        const sdl::NetContext netContext = sdl::NetContext();
        if (!netContext.isValid()) {
            fprintf(stderr, "Failed to initialize SDL_net: %s\n", SDL_GetError());
            return 1;
        }

        return server(ip.data(), port);
    } else if (netMode == 2) {
        const sdl::Context context = sdl::Context(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        if (!context.isValid()) {
            fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
            return 1;
        }
        const sdl::NetContext netContext = sdl::NetContext();
        if (!netContext.isValid()) {
            fprintf(stderr, "Failed to initialize SDL_net: %s\n", SDL_GetError());
            return 1;
        }

        return client(ip.data(), port);
    }

    return 0;
}