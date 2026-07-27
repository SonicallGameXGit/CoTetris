#include "core/sdl.hpp"
#include "core/gl.hpp"
#include "game/piece.hpp"
#include "game/map.hpp"
#include "game/button.hpp"
#include "game/network.hpp"

float pixelToWorldX(const float x, const float windowWidth, const float windowHeight) {
    return (x / windowWidth * 2.0f - 1.0f) * (windowWidth / windowHeight);
}
float pixelToWorldY(const float y, const float windowHeight) {
    return (1.0f - y / windowHeight) * 2.0f - 1.0f;
}

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
    PacketReceiver client = PacketReceiver(clientSocket.get(), false);
    printf("Connected successfully!\n");

    {
        HandshakePacket handshakePacket = HandshakePacket();
        handshakePacket.protocolVersion = Network::PROTOCOL_VERSION;
        PacketSender sender = PacketSender(PacketId::SC_Handshake, std::vector<uint8_t>());
        handshakePacket.build(sender.getMutableBuffer());
        sender.send(client.getSocket());
    }


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
        PrefabPushPacket packet = PrefabPushPacket();
        packet.prefab = prefab;
        PacketSender sender = PacketSender(PacketId::C_PrefabPush, std::vector<uint8_t>());
        packet.build(sender.getMutableBuffer());
        sender.send(client.getSocket());
    };
    Button pushPrefabButton = Button(TextureRegistry::getInstance().getPushButtonTexture(), [&](Button &self) {
        pushPrefabButtonCallback();
    });
    pushPrefabButton.size = glm::vec2(0.1f);

    float tickRate = 1.0f / 60.0f;
    float tickTimer = 0.0f;

    std::optional<ClientType> clientType = std::nullopt;
    std::function<bool(PacketReceiver &client)> packetCallback = [&](PacketReceiver &client) -> bool {
        const std::optional<PacketId> packetId = client.getPacketId();
        if (!packetId.has_value()) { return false; }
        switch (packetId.value()) {
            case PacketId::SC_Handshake: {
                HandshakePacket packet = HandshakePacket();
                if (!packet.parse(client)) { return false; }
                if (packet.protocolVersion != Network::PROTOCOL_VERSION) {
                    fprintf(stderr, "Protocol version mismatch: server is using version %d, but client is using version %d\n", packet.protocolVersion, Network::PROTOCOL_VERSION);
                }
                break;
            }
            case PacketId::S_ClientTypeSet: {
                ClientTypeSetPacket packet = ClientTypeSetPacket();
                if (!packet.parse(client)) { return false; }
                clientType = packet.type;
                printf("Client type set to %s.\n", clientType.value() == ClientType::Player ? "Player" : "Builder");
                break;
            }
            case PacketId::S_PiecePosition: {
                PiecePositionPacket packet = PiecePositionPacket();
                if (!packet.parse(client)) { return false; }
                if (!map.piece.has_value()) { map.piece.emplace(0, 0, 0); }
                map.piece->x = packet.x;
                break;
            }
            case PacketId::SC_PieceRotate: {
                PieceRotatePacket packet = PieceRotatePacket();
                if (!packet.parse(client)) { return false; }
                if (!map.piece.has_value()) { map.piece.emplace(0, 0, 0); }
                map.piece->rotateTo(packet.orientation);
                break;
            }
            case PacketId::S_PrefabPushSucceed: {
                PrefabPushSucceedPacket packet = PrefabPushSucceedPacket();
                if (!packet.parse(client)) { return false; }
                std::fill(prefab.begin(), prefab.end(), std::array<uint8_t, Piece::width>());
                break;
            }
            case PacketId::S_Tick: {
                TickPacket packet = TickPacket();
                if (!packet.parse(client)) { return false; }
                map.grid = packet.grid;
                map.piece = packet.piece;
                break;
            }
            default: {
                printf("Received unknown packet with ID %d from client.\n", static_cast<uint8_t>(packetId.value_or(PacketId::Invalid)));
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
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            PieceMovePacket packet = PieceMovePacket();
                            packet.direction = PieceMovePacket::Direction::Left;
                            PacketSender sender = PacketSender(PacketId::C_PieceMove, std::vector<uint8_t>());
                            packet.build(sender.getMutableBuffer());
                            sender.send(client.getSocket());
                            map.movePieceLeft();
                            break;
                        }
                        case SDLK_RIGHT:
                        case SDLK_D: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            PieceMovePacket packet = PieceMovePacket();
                            packet.direction = PieceMovePacket::Direction::Right;
                            PacketSender sender = PacketSender(PacketId::C_PieceMove, std::vector<uint8_t>());
                            packet.build(sender.getMutableBuffer());
                            sender.send(client.getSocket());
                            map.movePieceRight();
                            break;
                        }
                        case SDLK_UP:
                        case SDLK_W:
                        case SDLK_R: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            map.rotatePiece();

                            PieceRotatePacket packet = PieceRotatePacket();
                            packet.orientation = map.piece.has_value() ? map.piece->orientation : 0;
                            PacketSender sender = PacketSender(PacketId::SC_PieceRotate, std::vector<uint8_t>());
                            packet.build(sender.getMutableBuffer());
                            sender.send(client.getSocket());
                            break;
                        }
                        case SDLK_S:
                        case SDLK_DOWN:
                        case SDLK_SPACE:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            TickRateChangePacket packet = TickRateChangePacket();
                            packet.fast = true;
                            PacketSender sender = PacketSender(PacketId::C_TickRateChange, std::vector<uint8_t>());
                            packet.build(sender.getMutableBuffer());
                            sender.send(client.getSocket());
                            break;
                        }
                        case SDLK_RETURN: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
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
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            TickRateChangePacket packet = TickRateChangePacket();
                            packet.fast = false;
                            PacketSender sender = PacketSender(PacketId::C_TickRateChange, std::vector<uint8_t>());
                            packet.build(sender.getMutableBuffer());
                            sender.send(client.getSocket());
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
            int bytesRead = NET_ReadFromStreamSocket(client.getSocket(), buffer, sizeof(buffer));
            if (bytesRead < 0) {
                printf("Client disconnected.\n");
                clientType = std::nullopt;
                map = ClientMap();
                std::fill(prefab.begin(), prefab.end(), std::array<uint8_t, Piece::width>());
                brush = 1;
                continue;
            } else if (bytesRead > 0) {
                client.receive(buffer, static_cast<size_t>(bytesRead));
                while (packetCallback(client));
                client.rewind();
            }
        }

        const float aspectRatio = windowWidth / windowHeight;
        if (clientType.has_value() && clientType.value() == ClientType::Builder) {
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

        if (clientType.has_value() && clientType.value() == ClientType::Builder) {
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

    std::optional<ServerMap> map = std::nullopt;
    const float slowTickRate = 1.0f / 2.0f;
    const float fastTickRate = 1.0f / 12.0f;
    float tickRate = slowTickRate;

    std::vector<std::unique_ptr<PacketReceiver>> clients = std::vector<std::unique_ptr<PacketReceiver>>();
    PacketReceiver *player = nullptr, *builder = nullptr;

    std::function<bool(PacketReceiver &client)> packetCallback = [&](PacketReceiver &client) -> bool {
        std::optional<PacketId> packetId = client.getPacketId();
        if (!packetId.has_value()) { return false; }
        switch (packetId.value()) {
            case PacketId::SC_Handshake: {
                HandshakePacket packet = HandshakePacket();
                if (!packet.parse(client)) { return false; }
                if (&client == player || &client == builder) { break; }

                const uint32_t clientProtocolVersion = packet.protocolVersion;
                packet.protocolVersion = Network::PROTOCOL_VERSION;
                PacketSender sender = PacketSender(PacketId::SC_Handshake, std::vector<uint8_t>());
                packet.build(sender.getMutableBuffer());
                sender.send(client.getSocket());

                if (clientProtocolVersion != Network::PROTOCOL_VERSION) {
                    printf("Client has incompatible protocol version %d, expected %d.\n", clientProtocolVersion, Network::PROTOCOL_VERSION);
                } else {
                    printf("Client has compatible protocol version %d.\n", clientProtocolVersion);
                    ClientTypeSetPacket responsePacket = ClientTypeSetPacket();
                    if (player == nullptr) {
                        player = &client;
                        responsePacket.type = ClientType::Player;
                    } else if (builder == nullptr) {
                        builder = &client;
                        responsePacket.type = ClientType::Builder;
                    } else {
                        responsePacket.type = ClientType::Spectator;
                    }
                    if (player != nullptr && builder != nullptr && !map.has_value()) {
                        map.emplace();
                    }
                    PacketSender responseSender = PacketSender(PacketId::S_ClientTypeSet, std::vector<uint8_t>());
                    responsePacket.build(responseSender.getMutableBuffer());
                    responseSender.send(client.getSocket());
                }
                break;
            }
            case PacketId::C_PieceMove: {
                PieceMovePacket packet = PieceMovePacket();
                if (!packet.parse(client)) { return false; }
                if (!map.has_value() || &client != player) { break; }

                if (packet.direction == PieceMovePacket::Direction::Right) {
                    map->movePieceRight();
                } else {
                    map->movePieceLeft();
                }

                const Piece *piece = map->getPiece();
                if (piece == nullptr) { break; }

                PiecePositionPacket piecePositionPacket = PiecePositionPacket();
                piecePositionPacket.x = piece->x;
                PacketSender sender = PacketSender(PacketId::S_PiecePosition, std::vector<uint8_t>());
                piecePositionPacket.build(sender.getMutableBuffer());
                for (std::vector<std::unique_ptr<PacketReceiver>>::iterator it = clients.begin(); it != clients.end(); ++it) {
                    PacketReceiver *otherClient = it->get();
                    if (otherClient == &client) { continue; }
                    sender.send(otherClient->getSocket());
                }
                break;
            }
            case PacketId::SC_PieceRotate: {
                PieceRotatePacket packet = PieceRotatePacket();
                if (!packet.parse(client)) { return false; }
                if (!map.has_value() || &client != player) { break; }

                map->rotatePiece();
                const Piece *piece = map->getPiece();
                if (piece == nullptr) { break; }

                packet.orientation = piece->orientation;
                PacketSender sender = PacketSender(PacketId::SC_PieceRotate, std::vector<uint8_t>());
                packet.build(sender.getMutableBuffer());
                for (std::vector<std::unique_ptr<PacketReceiver>>::iterator it = clients.begin(); it != clients.end(); ++it) {
                    PacketReceiver &otherClient = *it->get();
                    if (&otherClient == &client) { continue; }
                    sender.send(otherClient.getSocket());
                }
                break;
            }
            case PacketId::C_PrefabPush: {
                PrefabPushPacket packet = PrefabPushPacket();
                if (!packet.parse(client)) { return false; }
                if (!map.has_value() || &client != builder) { break; }

                if (map->pushPrefab(packet.prefab)) {
                    PrefabPushSucceedPacket responsePacket = PrefabPushSucceedPacket();
                    PacketSender sender = PacketSender(PacketId::S_PrefabPushSucceed, std::vector<uint8_t>());
                    responsePacket.build(sender.getMutableBuffer());
                    sender.send(client.getSocket());
                }
                break;
            }
            case PacketId::C_TickRateChange: {
                TickRateChangePacket packet = TickRateChangePacket();
                if (!packet.parse(client)) { return false; }
                if (!map.has_value() || &client != player) { break; }
                tickRate = packet.fast ? fastTickRate : slowTickRate;
                break;
            }
            default: {
                printf("Received unknown packet with ID %d from client.\n", static_cast<uint8_t>(packetId.value_or(PacketId::Invalid)));
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
            clients.emplace_back(std::make_unique<PacketReceiver>(socket, true));
            printf("New client connected!\n");
            socket = nullptr;
        }
        for (std::vector<std::unique_ptr<PacketReceiver>>::iterator it = clients.begin(); it != clients.end(); ) {
            PacketReceiver &client = *it->get();

            uint8_t buffer[4096];
            int bytesRead = NET_ReadFromStreamSocket(client.getSocket(), buffer, sizeof(buffer));
            
            if (bytesRead < 0) {
                printf("Client disconnected.\n");
                if (&client == player) {
                    player = nullptr;
                } else if (&client == builder) {
                    builder = nullptr;
                }
                it = clients.erase(it);
                if (player == nullptr && builder == nullptr) { map = ServerMap(); }
                continue;
            } else if (bytesRead > 0) {
                client.receive(buffer, static_cast<size_t>(bytesRead));
                while (packetCallback(client));
                client.rewind();
            }
            ++it;
        }
        
        Uint64 currentTime = SDL_GetTicksNS();
        const float deltaTime = static_cast<float>(currentTime - lastTime) / 1e9f;
        lastTime = currentTime;
        tickTimer += deltaTime;
        if (tickTimer >= tickRate) {
            tickTimer -= tickRate;
            if (map.has_value()) {
                map->tick();
    
                // FIXME: The ideas: send only the changes, allow the client to move the piece locally and only fix it when it's far enough from the real position - look below for more details:
                // 1. Instead of sending the entire grid and piece every tick, we should only send the changes (deltas) to reduce bandwidth usage.
                // 2. Instead of requiring the client to just ask the server to move the piece and receive the new world state, we should allow the client to move the piece locally and only send the new world state when the piece is far enough from the real position, to reduce latency and make the game feel more responsive, while keeping it safe from cheating.
                TickPacket tickPacket = TickPacket();
                tickPacket.grid = map->getGrid();
                tickPacket.piece = *map->getPiece();
    
                PacketSender sender = PacketSender(PacketId::S_Tick, std::vector<uint8_t>());
                tickPacket.build(sender.getMutableBuffer());
                for (std::vector<std::unique_ptr<PacketReceiver>>::iterator it = clients.begin(); it != clients.end(); ++it) {
                    PacketReceiver &client = *it->get();
                    sender.send(client.getSocket());
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