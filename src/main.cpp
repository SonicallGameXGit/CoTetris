#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "core/sdl.hpp"
#include "core/gl.hpp"
#include "game/piece.hpp"
#include "game/map.hpp"
#include "game/button.hpp"
#include "game/network.hpp"
#include "tinyfiledialogs/tinyfiledialogs.h"

namespace RelayConfig {
    // TODO: point this at the VPS's public IP/hostname once the relay is deployed.
    // 127.0.0.1 only works for testing two instances against each other on one machine.
    static constexpr const char *HOST = "173.242.54.254";
    static constexpr Uint16 PORT = 25565;
}

float pixelToWorldX(const float x, const float windowWidth, const float windowHeight) {
    return (x / windowWidth * 2.0f - 1.0f) * (windowWidth / windowHeight);
}
float pixelToWorldY(const float y, const float windowHeight) {
    return (1.0f - y / windowHeight) * 2.0f - 1.0f;
}

namespace GameProperties {
    static constexpr float SLOW_TICK_RATE = 1.0f / 2.0f;
    static constexpr float FAST_TICK_RATE = 1.0f / 12.0f;
}

// Blocks until the relay sends the packet id we're waiting for. This only runs
// before the game window and its per-frame socket polling exist, for the
// handful of request/response exchanges (handshake ack, room create/join) that
// have to resolve before there's anything to show on screen.
bool waitForPacket(PacketReceiver &connection, PacketId expected) {
    while (true) {
        uint8_t buffer[4096];
        const int bytesRead = NET_ReadFromStreamSocket(connection.getSocket(), buffer, sizeof(buffer));
        if (bytesRead < 0) { return false; }
        if (bytesRead > 0) {
            connection.receive(buffer, static_cast<size_t>(bytesRead));
            const std::optional<PacketId> packetId = connection.getPacketId();
            if (packetId.has_value()) {
                if (packetId.value() == expected) { return true; }
                // Not what this wait is for; drop it and keep listening. Nothing
                // else should arrive this early, but skipping is free either way.
                connection.flush();
            }
        }
        SDL_Delay(10);
    }
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

    // The relay has to confirm the handshake, and then a room, before there is
    // anything worth putting a game window on screen for. Both are resolved
    // through native popups so this works with no in-game text rendering.
    if (!waitForPacket(client, PacketId::SC_Handshake)) {
        tinyfd_messageBox("CoTetris", "Lost connection to the relay while handshaking.", "ok", "error", 1);
        return 1;
    }
    {
        HandshakePacket ack = HandshakePacket();
        ack.parse(client);
        client.flush();
        if (ack.protocolVersion != Network::PROTOCOL_VERSION) {
            tinyfd_messageBox("CoTetris", "This build is out of date with the relay server. Please update the game.", "ok", "error", 1);
            return 1;
        }
    }

    // Loops back to the New/Join choice whenever the player cancels out of
    // something that choice led to (e.g. the room code entry), since that
    // screen is this one's parent. Cancelling the choice itself has no parent
    // to fall back to, so that is the only case that quits outright.
    while (true) {
        const int roomChoice = tinyfd_messageBox("CoTetris", "Create a new room, or join one a friend already started?\n\nYes = New Room\nNo = Join Room", "yesnocancel", "question", 1);
        if (roomChoice == 0) { return 0; }

        if (roomChoice == 1) {
            CreateRoomPacket packet = CreateRoomPacket();
            PacketSender sender = PacketSender(PacketId::C_CreateRoom, std::vector<uint8_t>());
            packet.build(sender.getMutableBuffer());
            sender.send(client.getSocket());

            if (!waitForPacket(client, PacketId::S_RoomCreated)) {
                tinyfd_messageBox("CoTetris", "Lost connection to the relay while creating a room.", "ok", "error", 1);
                return 1;
            }
            RoomCreatedPacket created = RoomCreatedPacket();
            created.parse(client);
            client.flush();

            const std::string code(created.code.begin(), created.code.end());
            SDL_SetClipboardText(code.c_str());
            // No apostrophes or quotes here: tinyfiledialogs refuses to show a
            // message containing one, since it cannot safely escape it for
            // every backend it shells out to.
            const std::string message = "Room created! Code: " + code + "\n\nIt has been copied to your clipboard - send it to whoever you want to play with.";
            tinyfd_messageBox("CoTetris", message.c_str(), "ok", "info", 1);
            break;
        }

        // Join Room. Cancelling the code prompt falls through to the bottom of
        // this loop, which re-shows the New/Join choice instead of quitting.
        bool joined = false;
        while (true) {
            char *input = tinyfd_inputBox("CoTetris", "Enter the room code your friend sent you:", "");
            if (input == nullptr) { break; }

            std::string code(input);
            for (char &c : code) { c = static_cast<char>(toupper(static_cast<unsigned char>(c))); }
            if (code.length() != Network::ROOM_CODE_LENGTH) {
                tinyfd_messageBox("CoTetris", "That code does not look right - codes are 5 characters.", "ok", "warning", 1);
                continue;
            }

            JoinRoomPacket packet = JoinRoomPacket();
            std::copy(code.begin(), code.end(), packet.code.begin());
            PacketSender sender = PacketSender(PacketId::C_JoinRoom, std::vector<uint8_t>());
            packet.build(sender.getMutableBuffer());
            sender.send(client.getSocket());

            if (!waitForPacket(client, PacketId::S_RoomJoinResult)) {
                tinyfd_messageBox("CoTetris", "Lost connection to the relay while joining.", "ok", "error", 1);
                return 1;
            }
            RoomJoinResultPacket result = RoomJoinResultPacket();
            result.parse(client);
            client.flush();

            if (!result.success) {
                tinyfd_messageBox("CoTetris", "No room found with that code. Double check it and try again.", "ok", "warning", 1);
                continue;
            }
            joined = true;
            break;
        }
        if (joined) { break; }
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
    glClearColor(9.0f / 255.0f, 15.0f / 255.0f, 37.0f / 255.0f, 1.0f);

    uint8_t mouseButtonsBitmask = 0;
    glm::vec2 mousePosition = glm::vec2();

    ClientMap map = ClientMap();
    std::array<std::array<uint8_t, Piece::width>, Piece::height> prefab = std::array<std::array<uint8_t, Piece::width>, Piece::height>();
    uint8_t brush = 1;

    std::vector<Toggle> toggles = std::vector<Toggle>();
    toggles.reserve(7);
    std::function<void(Toggle &self)> toggleCallback = [&](Toggle &self) {
        self.state = true;
        self.uv.x = 1.0f / 3.0f;
        for (size_t i = 0; i < toggles.size(); i++) {
            if (&toggles[i] != &self) {
                toggles[i].state = false;
                toggles[i].uv.x = 0.0f;
            } else {
                brush = static_cast<uint8_t>(i + 1);
            }
        }
    };
    for (uint8_t i = 0; i < 7; i++) {
        Toggle &toggle = toggles.emplace_back(TextureRegistry::getInstance().getAtlasTexture(), toggleCallback);
        toggle.uv = glm::vec4(0.0f, 0.0f, 1.0f / 3.0f - 0.005f, 1.0f / 3.0f - 0.005f);
        toggle.size = glm::vec2(0.1f);
        toggle.hsv = map_properties::BRICK_HSVS[i];
    }
    toggleCallback(toggles[0]);
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
        PacketSender sender = PacketSender(PacketId::SC_PrefabPush, std::vector<uint8_t>());
        packet.build(sender.getMutableBuffer());
        sender.send(client.getSocket());
    };
    Button pushPrefabButton = Button(TextureRegistry::getInstance().getPushButtonTexture(), [&](Button &self) {
        pushPrefabButtonCallback();
    });
    pushPrefabButton.size = glm::vec2(0.1f);

    float networkTickRate = 1.0f / 60.0f, networkTickTimer = 0.0f;
    float tickRate = GameProperties::SLOW_TICK_RATE, tickTimer = 0.0f;

    // Only meaningful for a player: it holds the one authoritative simulation, and
    // may not start running it until the server has handed over the seed.
    bool gameStarted = false;
    // Set by anything that changes the simulation, so the resulting state goes out
    // once per network tick instead of once per input.
    bool stateDirty = false;

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
                switch (clientType.value()) {
                    case ClientType::Player:    printf("Client type set to Player.\n"); break;
                    case ClientType::Builder:   printf("Client type set to Builder.\n"); break;
                    case ClientType::Spectator: printf("Client type set to Spectator.\n"); break;
                    default:                    printf("Client type set to an unknown value.\n"); break;
                }
                break;
            }
            case PacketId::S_GameStart: {
                GameStartPacket packet = GameStartPacket();
                if (!packet.parse(client)) { return false; }

                // The seed is the whole handover. From here the piece sequence is
                // ours to generate, so nothing in the game needs a round trip again.
                map = ClientMap();
                map.reseed(packet.seed);
                map.showPiece();
                map.rebuildGhost();
                tickRate = GameProperties::SLOW_TICK_RATE;
                tickTimer = 0.0f;
                gameStarted = true;
                stateDirty = true;
                printf("Game started with seed %u.\n", packet.seed);
                break;
            }
            case PacketId::S_PrefabPushSucceed: {
                PrefabPushSucceedPacket packet = PrefabPushSucceedPacket();
                if (!packet.parse(client)) { return false; }
                std::fill(prefab.begin(), prefab.end(), std::array<uint8_t, Piece::width>());
                break;
            }
            case PacketId::SC_PrefabPush: {
                PrefabPushPacket packet = PrefabPushPacket();
                if (!packet.parse(client)) { return false; }
                // ! Only the player keeps a prefab stack, because only the player
                // ! spawns pieces out of it. Anyone else receiving this would be
                // ! feeding a simulation they do not run.
                if (!gameStarted || !clientType.has_value() || clientType.value() != ClientType::Player) { break; }

                // Whether it fits is ours to decide and the builder's to hear about,
                // so the answer goes back the way the prefab came.
                PrefabPushResultPacket responsePacket = PrefabPushResultPacket();
                responsePacket.accepted = map.pushPrefab(packet.prefab);
                PacketSender sender = PacketSender(PacketId::C_PrefabPushResult, std::vector<uint8_t>());
                responsePacket.build(sender.getMutableBuffer());
                sender.send(client.getSocket());
                break;
            }
            case PacketId::SC_MapState: {
                MapStatePacket packet = MapStatePacket();
                if (!packet.parse(client)) { return false; }
                // ! A player must never take a relayed state: it is the authority, and
                // ! overwriting its own simulation with an echo is exactly the jitter
                // ! this whole design exists to remove.
                if (clientType.has_value() && clientType.value() == ClientType::Player) { break; }

                map.grid = packet.grid;
                map.piece = packet.piece;
                map.rebuildGhost();
                break;
            }
            default: {
                // Nothing to do: the length prefix tells flush() exactly how many
                // bytes this packet occupies, so it is skipped whole and the stream
                // carries on undisturbed.
                printf("Skipping unknown packet with ID %d and %d bytes of payload.\n",
                    static_cast<int>(packetId.value_or(PacketId::Invalid)), static_cast<int>(client.getPacketLength()));
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
                        // Every one of these acts on the local simulation and nothing
                        // else. There is no request, no acknowledgement and no wait:
                        // the piece has already moved by the time the frame is drawn.
                        case SDLK_LEFT:
                        case SDLK_A: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            map.movePieceLeft();
                            map.rebuildGhost();
                            stateDirty = true;
                            break;
                        }
                        case SDLK_RIGHT:
                        case SDLK_D: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            map.movePieceRight();
                            map.rebuildGhost();
                            stateDirty = true;
                            break;
                        }
                        case SDLK_UP:
                        case SDLK_W:
                        case SDLK_R: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            map.rotatePiece();
                            map.rebuildGhost();
                            stateDirty = true;
                            break;
                        }
                        case SDLK_S:
                        case SDLK_DOWN:
                        case SDLK_SPACE:
                        case SDLK_LSHIFT:
                        case SDLK_RSHIFT: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Player) { break; }
                            // Nobody else needs to know how fast we are dropping; the
                            // resulting positions are reported like any other change.
                            tickRate = GameProperties::FAST_TICK_RATE;
                            break;
                        }
                        case SDLK_RETURN: {
                            if (event.key.repeat) { break; }
                            if (!clientType.has_value() || clientType.value() != ClientType::Builder) { break; }
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
                            tickRate = GameProperties::SLOW_TICK_RATE;
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

        networkTickTimer += deltaTime;
        if (networkTickTimer >= networkTickRate) {
            networkTickTimer -= networkTickRate;

            uint8_t buffer[4096];
            int bytesRead = NET_ReadFromStreamSocket(client.getSocket(), buffer, sizeof(buffer));
            if (bytesRead < 0) {
                printf("Client disconnected.\n");
                clientType = std::nullopt;
                map = ClientMap();
                std::fill(prefab.begin(), prefab.end(), std::array<uint8_t, Piece::width>());
                brush = 1;
                gameStarted = false;
                stateDirty = false;
                continue;
            } else if (bytesRead > 0) {
                client.receive(buffer, static_cast<size_t>(bytesRead));
                while (packetCallback(client));
                client.rewind();
            }

            // Report the simulation at most once per network tick, so a flurry of
            // inputs inside one frame costs a single packet rather than one each.
            if (stateDirty && clientType.has_value() && clientType.value() == ClientType::Player) {
                stateDirty = false;

                MapStatePacket packet = MapStatePacket();
                packet.grid = map.grid;
                packet.piece = map.piece;
                PacketSender sender = PacketSender(PacketId::SC_MapState, std::vector<uint8_t>());
                packet.build(sender.getMutableBuffer());
                sender.send(client.getSocket());
            }
        }

        if (gameStarted && clientType.has_value() && clientType.value() == ClientType::Player) {
            tickTimer += deltaTime / tickRate;
            if (tickTimer >= 1.0f) {
                tickTimer -= 1.0f;
                // The next piece comes straight out of the local sequence, so a lock
                // no longer costs a round trip before the player can act again.
                if (!map.piece.has_value()) {
                    map.showPiece();
                    map.rebuildGhost();
                }
                map.tick();
                stateDirty = true;
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
                    
                    BasicShader::getInstance().setUV(glm::vec4(0.0f, 2.0f / 3.0f, 1.0f / 3.0f - 0.005f, 1.0f / 3.0f - 0.005f));
                    BasicShader::getInstance().setHSV(HSV(0.0f, 0.0f, 0.0f));
                    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                    if (value > 0) {
                        BasicShader::getInstance().setUV(glm::vec4(0.0f, 0.0f, 1.0f / 3.0f - 0.005f, 1.0f / 3.0f - 0.005f));
                        BasicShader::getInstance().setHSV(map_properties::BRICK_HSVS[value - 1]);
                        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
                        BasicShader::getInstance().setUV(glm::vec4(0.0f, 1.0f / 3.0f, 1.0f / 3.0f - 0.005f, 1.0f / 3.0f - 0.005f));
                        BasicShader::getInstance().setHSV(HSV(0.0f, 0.0f, 0.0f));
                        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                    }
                }
            }
            for (Toggle &toggle : toggles) {
                // FIXME: That's pretty ugly, lol
                toggle.uv.y = 0.0f;
                toggle.draw(projectionViewMatrix);
                toggle.uv.y = 1.0f / 3.0f;
                HSV hsv = toggle.hsv;
                toggle.hsv = HSV(0.0f, 0.0f, 0.0f);
                toggle.draw(projectionViewMatrix);
                toggle.hsv = hsv;
            }
            pushPrefabButton.draw(projectionViewMatrix);
        }
        SDL_GL_SwapWindow(window.get());
    }
    return 0;
}
// One relay process hosts many concurrent games, so what used to be the
// server's global state (one player, one builder, one cached board) now
// belongs to a Room instead. PacketReceivers are still owned by the
// connection list in server(); a Room only holds raw pointers to whichever
// of them have joined it.
struct Room {
    std::vector<PacketReceiver*> clients = std::vector<PacketReceiver*>();
    PacketReceiver *player = nullptr;
    PacketReceiver *builder = nullptr;
    std::optional<MapStatePacket> lastPlayerState = std::nullopt;
    bool gameStarted = false;
    std::string lastValidationWarning;
};

// Excludes 0/O, 1/I/L and other easily-confused characters, since a player
// reads this code off a friend over voice chat rather than copy-pasting it.
static constexpr char roomCodeAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
static constexpr size_t roomCodeAlphabetSize = sizeof(roomCodeAlphabet) - 1;

std::string generateRoomCode(Random &rng, const std::unordered_map<std::string, Room> &rooms) {
    std::string code;
    do {
        code.clear();
        for (size_t i = 0; i < Network::ROOM_CODE_LENGTH; i++) {
            code.push_back(roomCodeAlphabet[rng.nextBelow(roomCodeAlphabetSize)]);
        }
    } while (rooms.find(code) != rooms.end());
    return code;
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

    // ! The server does not simulate. Each room's player client owns that room's
    // ! one and only simulation, and everything here is either a relay or a cache
    // ! of what the player last reported, so that late joiners have something to
    // ! draw. Rooms are created and looked up by their code; a client is not in
    // ! any room until it sends C_CreateRoom or C_JoinRoom.
    std::unordered_map<std::string, Room> rooms;
    std::unordered_map<PacketReceiver*, std::string> clientRoomCode;
    // ! A version mismatch is only ever logged, never disconnected outright, but a
    // ! mismatched client must still be kept out of a room: it may not agree with
    // ! this build on packet shapes, and letting it in could desync a real player.
    std::unordered_set<PacketReceiver*> incompatibleClients;
    Random serverRandom = Random(static_cast<uint32_t>(SDL_GetTicksNS()));

    std::vector<std::unique_ptr<PacketReceiver>> clients = std::vector<std::unique_ptr<PacketReceiver>>();

    std::function<Room*(PacketReceiver*)> findRoom = [&](PacketReceiver *client) -> Room* {
        const std::unordered_map<PacketReceiver*, std::string>::iterator codeIt = clientRoomCode.find(client);
        if (codeIt == clientRoomCode.end()) { return nullptr; }
        const std::unordered_map<std::string, Room>::iterator roomIt = rooms.find(codeIt->second);
        return roomIt == rooms.end() ? nullptr : &roomIt->second;
    };

    // ! This is bug containment, not anti-cheat. A co-op game has nobody to cheat
    // ! against, and correcting the player is exactly the behaviour that made the
    // ! game feel bad in the first place, so a failed check is only ever reported.
    // ! Compares against room.lastPlayerState, so it must run before that is replaced.
    std::function<void(Room &, const MapStatePacket &)> validatePlayerState = [&](Room &room, const MapStatePacket &state) {
        char warning[256];
        warning[0] = '\0';

        // The cells are drawn from a 3x3 atlas, so 8 is the highest value that has
        // a texture behind it.
        static constexpr uint8_t maxCellValue = 8;
        size_t filled = 0;
        for (const std::array<uint8_t, Map::width> &row : state.grid) {
            for (const uint8_t cell : row) {
                if (cell != 0) { filled++; }
                if (cell > maxCellValue && warning[0] == '\0') {
                    snprintf(warning, sizeof(warning), "grid holds cell value %d, above the %d the atlas can draw", static_cast<int>(cell), static_cast<int>(maxCellValue));
                }
            }
        }

        if (warning[0] == '\0' && state.piece.has_value()) {
            const int x = static_cast<int>(state.piece->x), y = static_cast<int>(state.piece->y);
            if (
                x < -static_cast<int>(Piece::width) || x > static_cast<int>(Map::width) ||
                y < -static_cast<int>(Piece::height) || y > static_cast<int>(Map::height)
            ) {
                snprintf(warning, sizeof(warning), "piece sits outside the board at (%d, %d)", x, y);
            }
        }

        if (warning[0] == '\0' && room.lastPlayerState.has_value()) {
            size_t previousFilled = 0;
            for (const std::array<uint8_t, Map::width> &row : room.lastPlayerState->grid) {
                for (const uint8_t cell : row) {
                    if (cell != 0) { previousFilled++; }
                }
            }
            // A single update can only ever lock one piece into the grid. Line clears
            // shrink the count rather than growing it, so a jump bigger than a piece
            // means the two sides disagree about something.
            const size_t maxGrowth = Piece::width * Piece::height;
            if (filled > previousFilled + maxGrowth) {
                snprintf(warning, sizeof(warning), "grid gained %zu cells in one update, more than the %zu a single piece can add", filled - previousFilled, maxGrowth);
            }
        }

        // A state update arrives up to sixty times a second, so repeating an unchanged
        // complaint would bury every other message. Each distinct one is said once.
        if (warning[0] != '\0') {
            if (room.lastValidationWarning != warning) {
                room.lastValidationWarning = warning;
                printf("[validation] %s\n", warning);
            }
        } else {
            room.lastValidationWarning.clear();
        }
    };

    std::function<bool(PacketReceiver &client)> packetCallback = [&](PacketReceiver &client) -> bool {
        std::optional<PacketId> packetId = client.getPacketId();
        if (!packetId.has_value()) { return false; }
        switch (packetId.value()) {
            case PacketId::SC_Handshake: {
                HandshakePacket packet = HandshakePacket();
                if (!packet.parse(client)) { return false; }
                if (clientRoomCode.find(&client) != clientRoomCode.end()) { break; }

                const uint32_t clientProtocolVersion = packet.protocolVersion;
                packet.protocolVersion = Network::PROTOCOL_VERSION;
                {
                    PacketSender sender = PacketSender(PacketId::SC_Handshake, std::vector<uint8_t>());
                    packet.build(sender.getMutableBuffer());
                    sender.send(client.getSocket());
                }

                if (clientProtocolVersion != Network::PROTOCOL_VERSION) {
                    printf("Client has incompatible protocol version %d, expected %d.\n", clientProtocolVersion, Network::PROTOCOL_VERSION);
                    incompatibleClients.insert(&client);
                } else {
                    printf("Client has compatible protocol version %d.\n", clientProtocolVersion);
                }
                break;
            }
            case PacketId::C_CreateRoom: {
                CreateRoomPacket packet = CreateRoomPacket();
                if (!packet.parse(client)) { return false; }
                if (clientRoomCode.find(&client) != clientRoomCode.end() || incompatibleClients.count(&client) > 0) { break; }

                const std::string code = generateRoomCode(serverRandom, rooms);
                Room &room = rooms[code];
                room.clients.push_back(&client);
                room.player = &client;
                clientRoomCode[&client] = code;
                printf("Room %s created.\n", code.c_str());

                {
                    RoomCreatedPacket responsePacket = RoomCreatedPacket();
                    std::copy(code.begin(), code.end(), responsePacket.code.begin());
                    PacketSender sender = PacketSender(PacketId::S_RoomCreated, std::vector<uint8_t>());
                    responsePacket.build(sender.getMutableBuffer());
                    sender.send(client.getSocket());
                }
                {
                    ClientTypeSetPacket responsePacket = ClientTypeSetPacket();
                    responsePacket.type = ClientType::Player;
                    PacketSender sender = PacketSender(PacketId::S_ClientTypeSet, std::vector<uint8_t>());
                    responsePacket.build(sender.getMutableBuffer());
                    sender.send(client.getSocket());
                }
                break;
            }
            case PacketId::C_JoinRoom: {
                JoinRoomPacket packet = JoinRoomPacket();
                if (!packet.parse(client)) { return false; }
                if (clientRoomCode.find(&client) != clientRoomCode.end() || incompatibleClients.count(&client) > 0) { break; }

                const std::string code(packet.code.begin(), packet.code.end());
                const std::unordered_map<std::string, Room>::iterator roomIt = rooms.find(code);
                const bool found = roomIt != rooms.end();
                {
                    RoomJoinResultPacket responsePacket = RoomJoinResultPacket();
                    responsePacket.success = found;
                    PacketSender sender = PacketSender(PacketId::S_RoomJoinResult, std::vector<uint8_t>());
                    responsePacket.build(sender.getMutableBuffer());
                    sender.send(client.getSocket());
                }
                if (!found) { break; }

                Room &room = roomIt->second;
                room.clients.push_back(&client);
                clientRoomCode[&client] = code;

                ClientTypeSetPacket responsePacket = ClientTypeSetPacket();
                if (room.player == nullptr) {
                    room.player = &client;
                    responsePacket.type = ClientType::Player;
                } else if (room.builder == nullptr) {
                    room.builder = &client;
                    responsePacket.type = ClientType::Builder;
                } else {
                    responsePacket.type = ClientType::Spectator;
                }
                {
                    PacketSender sender = PacketSender(PacketId::S_ClientTypeSet, std::vector<uint8_t>());
                    responsePacket.build(sender.getMutableBuffer());
                    sender.send(client.getSocket());
                }

                // Anyone who is not the player only ever watches, so hand a
                // newcomer the last known board immediately instead of leaving
                // it staring at an empty field until the player next moves.
                if (&client != room.player && room.lastPlayerState.has_value()) {
                    PacketSender sender = PacketSender(PacketId::SC_MapState, std::vector<uint8_t>());
                    room.lastPlayerState->build(sender.getMutableBuffer());
                    sender.send(client.getSocket());
                }

                // The game exists only while somebody is simulating it, so it
                // begins as soon as there is both a player to run it and a
                // builder to feed it. The seed is all the player needs to
                // produce the whole piece sequence on its own.
                if (room.player != nullptr && room.builder != nullptr && !room.gameStarted) {
                    room.gameStarted = true;
                    room.lastPlayerState.reset();

                    GameStartPacket gameStartPacket = GameStartPacket();
                    gameStartPacket.seed = serverRandom.next();
                    PacketSender sender = PacketSender(PacketId::S_GameStart, std::vector<uint8_t>());
                    gameStartPacket.build(sender.getMutableBuffer());
                    sender.send(room.player->getSocket());
                    printf("Room %s started with seed %u.\n", code.c_str(), gameStartPacket.seed);
                }
                break;
            }
            case PacketId::SC_MapState: {
                MapStatePacket packet = MapStatePacket();
                if (!packet.parse(client)) { return false; }
                Room *room = findRoom(&client);
                // ! Only the player simulates. Anyone else claiming to is ignored,
                // ! which is what keeps a builder or spectator from driving the board.
                if (room == nullptr || &client != room->player) { break; }

                validatePlayerState(*room, packet);
                room->lastPlayerState = packet;

                PacketSender sender = PacketSender(PacketId::SC_MapState, std::vector<uint8_t>());
                packet.build(sender.getMutableBuffer());
                for (PacketReceiver *otherClient : room->clients) {
                    if (otherClient == room->player) { continue; }
                    sender.send(otherClient->getSocket());
                }
                break;
            }
            case PacketId::SC_PrefabPush: {
                PrefabPushPacket packet = PrefabPushPacket();
                if (!packet.parse(client)) { return false; }
                Room *room = findRoom(&client);
                if (room == nullptr || &client != room->builder || room->player == nullptr) { break; }

                // The server no longer owns the prefab stack, so it cannot say
                // whether this fits. Hand it to the player and let it answer.
                PacketSender sender = PacketSender(PacketId::SC_PrefabPush, std::vector<uint8_t>());
                packet.build(sender.getMutableBuffer());
                sender.send(room->player->getSocket());
                break;
            }
            case PacketId::C_PrefabPushResult: {
                PrefabPushResultPacket packet = PrefabPushResultPacket();
                if (!packet.parse(client)) { return false; }
                Room *room = findRoom(&client);
                if (room == nullptr || &client != room->player || room->builder == nullptr || !packet.accepted) { break; }

                PrefabPushSucceedPacket responsePacket = PrefabPushSucceedPacket();
                PacketSender sender = PacketSender(PacketId::S_PrefabPushSucceed, std::vector<uint8_t>());
                responsePacket.build(sender.getMutableBuffer());
                sender.send(room->builder->getSocket());
                break;
            }
            default: {
                // Nothing to do: the length prefix tells flush() exactly how many
                // bytes this packet occupies, so it is skipped whole and the stream
                // carries on undisturbed.
                printf("Skipping unknown packet with ID %d and %d bytes of payload.\n",
                    static_cast<int>(packetId.value_or(PacketId::Invalid)), static_cast<int>(client.getPacketLength()));
                break;
            }
        }

        client.flush();
        return true;
    };

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
                const std::unordered_map<PacketReceiver*, std::string>::iterator codeIt = clientRoomCode.find(&client);
                if (codeIt != clientRoomCode.end()) {
                    const std::unordered_map<std::string, Room>::iterator roomIt = rooms.find(codeIt->second);
                    if (roomIt != rooms.end()) {
                        Room &room = roomIt->second;
                        if (&client == room.player) {
                            // The player held the only simulation, so losing it ends
                            // the game. Clearing the pointer lets the next player to
                            // join start a fresh one with a new seed.
                            room.player = nullptr;
                            room.gameStarted = false;
                        } else if (&client == room.builder) {
                            room.builder = nullptr;
                        }
                        room.clients.erase(std::remove(room.clients.begin(), room.clients.end(), &client), room.clients.end());
                        // Nobody is left to relay to or cache state for, so the room
                        // itself goes away and its code becomes free to reuse.
                        if (room.clients.empty()) {
                            rooms.erase(roomIt);
                        }
                    }
                    clientRoomCode.erase(codeIt);
                }
                incompatibleClients.erase(&client);
                it = clients.erase(it);
                continue;
            } else if (bytesRead > 0) {
                client.receive(buffer, static_cast<size_t>(bytesRead));
                while (packetCallback(client));
                client.rewind();
            }
            ++it;
        }

        // Nothing here advances the game any more: without a simulation the server
        // only has to wake up often enough to move packets along.
        SDL_Delay(static_cast<Uint32>(1000.0f / 60.0f));
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // No arguments is the normal way to play: connect straight to the relay and
    // let the New Room/Join Room popups take it from there. The explicit
    // server/client ip port form still exists underneath for manual testing
    // (e.g. running the relay itself, or pointing a client at one by hand).
    if (argc == 1) {
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

        return client(RelayConfig::HOST, RelayConfig::PORT);
    }

    if (argc != 4) {
        fprintf(stderr, "Usage: %s [server ip port | client ip port]\n", argv[0]);
        fprintf(stderr, "  (as a server, use 'any' as the ip to listen on every interface)\n");
        fprintf(stderr, "  (run with no arguments at all to just play against the default relay)\n");
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