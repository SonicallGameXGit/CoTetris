#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include <SDL3_net/SDL_net.h>

namespace Network {
    // ! Protocol version number. Increment this when making changes to the protocol that are not backwards compatible.
    static const uint32_t PROTOCOL_VERSION = 2;
}

// Prefixes:
//  - C = client -> server
//  - S = server -> client
//  - SC = server <-> client
enum class PacketId : uint8_t {
    SC_Handshake, S_ClientTypeSet,
    C_PieceMove, S_PiecePosition, SC_PieceRotate,
    C_PrefabPush, S_PrefabPushSucceed,
    S_Tick, C_TickDelta, C_TickRateChange,
    S_SpawnPiece,
    Invalid = 255,
};

struct PacketReceiver {
private:
    NET_StreamSocket *socket;
    std::optional<PacketId> packetId;
    std::vector<uint8_t> buffer;
    size_t pointer;
    bool own;
public:
    PacketReceiver(NET_StreamSocket *socket, bool own) : socket(socket), packetId(std::nullopt), buffer(), pointer(0), own(own) {}
    ~PacketReceiver() {
        if (this->own) {
            NET_DestroyStreamSocket(this->socket);
        }
    }

    void receive(const uint8_t *data, size_t length) {
        if (!this->packetId.has_value()) {
            this->packetId.emplace(static_cast<PacketId>(data[0]));
            this->buffer.insert(this->buffer.end(), data + 1, data + length);
        } else {
            this->buffer.insert(this->buffer.end(), data, data + length);
        }
    }
    void flush() {
        if (this->buffer.size() > this->pointer) {
            this->packetId = std::optional<PacketId>(static_cast<PacketId>(this->buffer[this->pointer]));
            this->pointer++;
        } else {
            this->packetId = std::nullopt;
        }
        this->buffer.erase(this->buffer.begin(), this->buffer.begin() + this->pointer);
        this->pointer = 0;
    }
    void rewind() {
        this->pointer = 0;
    }

    const std::optional<std::vector<uint8_t>::const_iterator> read(const size_t length) {
        if (this->buffer.size() < this->pointer + length) { return std::nullopt; }
        const std::vector<uint8_t>::const_iterator it = this->buffer.begin() + this->pointer;
        this->pointer += length;
        return it;
    }

    std::optional<PacketId> getPacketId() { return this->packetId; }
    NET_StreamSocket *getSocket() const { return this->socket; }
};
struct PacketSender {
private:
    std::vector<uint8_t> buffer;
public:
    PacketSender(const PacketId packetId, const std::vector<uint8_t> &data) : buffer() {
        buffer.reserve(1 + data.size());
        buffer.push_back(static_cast<uint8_t>(packetId));
        buffer.insert(buffer.end(), data.begin(), data.end());
    }
    virtual ~PacketSender() = default;

    void send(NET_StreamSocket *socket) {
        NET_WriteToStreamSocket(socket, this->buffer.data(), this->buffer.size());
    }
    std::vector<uint8_t> &getMutableBuffer() { return this->buffer; }
};

class AbstractPacket {
protected:
    static void pushUInt8(std::vector<uint8_t> &buffer, const uint8_t value) {
        buffer.push_back(value);
    }
    static void pushUInt32(std::vector<uint8_t> &buffer, const uint32_t value) {
        buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }
    static void pushInt16(std::vector<uint8_t> &buffer, const int16_t value) {
        buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }
    static uint8_t parseUInt8(std::vector<uint8_t>::const_iterator &it) {
        const uint8_t value = *it;
        it++;
        return value;
    }
    static int16_t parseInt16(std::vector<uint8_t>::const_iterator &it) {
        const int16_t value =
            static_cast<int16_t>(static_cast<uint16_t>(*it) |
            (static_cast<uint16_t>(*(it + 1)) << 8))
        ;
        it += 2;
        return value;
    }
    static uint32_t parseUInt32(std::vector<uint8_t>::const_iterator &it) {
        const uint32_t value =
            static_cast<uint32_t>(static_cast<uint32_t>(*it) |
            (static_cast<uint32_t>(*(it + 1)) << 8) |
            (static_cast<uint32_t>(*(it + 2)) << 16) |
            (static_cast<uint32_t>(*(it + 3)) << 24))
        ;
        it += 4;
        return value;
    }
public:
    AbstractPacket() {}
    virtual ~AbstractPacket() = default;
    
    virtual void build(std::vector<uint8_t> &buffer) const = 0;
    virtual bool parse(PacketReceiver &client) = 0;
};

struct HandshakePacket : public AbstractPacket {
    uint32_t protocolVersion;
public:
    HandshakePacket() : AbstractPacket(), protocolVersion(Network::PROTOCOL_VERSION) {}
    virtual ~HandshakePacket() = default;
    
    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt32(buffer, this->protocolVersion);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(4);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->protocolVersion = AbstractPacket::parseUInt32(reserve);
        return true;
    }
};
enum class ClientType : uint8_t {
    Player = 0, Builder = 1, Spectator = 2, Invalid = 255
};
struct ClientTypeSetPacket : public AbstractPacket {
public:
    ClientType type;
    ClientTypeSetPacket() : AbstractPacket(), type(ClientType::Invalid) {}
    virtual ~ClientTypeSetPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt8(buffer, static_cast<uint8_t>(this->type));
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(1);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->type = static_cast<ClientType>(AbstractPacket::parseUInt8(reserve));
        return true;
    }
};
struct PieceMovePacket : public AbstractPacket {
public:
    enum class Direction : uint8_t {
        Left = 0,
        Right = 1
    };
    Direction direction;

    PieceMovePacket() : AbstractPacket(), direction(Direction::Left) {}
    virtual ~PieceMovePacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt8(buffer, static_cast<uint8_t>(this->direction));
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(1);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();

        this->direction = static_cast<Direction>(AbstractPacket::parseUInt8(reserve));
        return true;
    }
};
struct PiecePositionPacket : public AbstractPacket {
public:
    int16_t x;
    PiecePositionPacket() : AbstractPacket(), x(0) {}
    virtual ~PiecePositionPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushInt16(buffer, this->x);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(2);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->x = AbstractPacket::parseInt16(reserve);
        return true;
    }
};
struct PieceRotatePacket : public AbstractPacket {
public:
    uint8_t orientation;
    PieceRotatePacket() : AbstractPacket(), orientation(0) {}
    virtual ~PieceRotatePacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt8(buffer, this->orientation);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(1);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->orientation = AbstractPacket::parseUInt8(reserve);
        return true;
    }
};
struct PrefabPushPacket : public AbstractPacket {
public:
    std::array<std::array<uint8_t, Piece::width>, Piece::height> prefab;
    PrefabPushPacket() : AbstractPacket(), prefab() {}
    virtual ~PrefabPushPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        for (size_t row = 0; row < Piece::height; row++) {
            for (size_t col = 0; col < Piece::width; col++) {
                AbstractPacket::pushUInt8(buffer, this->prefab[row][col]);
            }
        }
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(Piece::width * Piece::height);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator it = reserveTry.value();
        for (size_t row = 0; row < Piece::height; row++) {
            for (size_t col = 0; col < Piece::width; col++) {
                this->prefab[row][col] = AbstractPacket::parseUInt8(it);
            }
        }
        return true;
    }
};
struct PrefabPushSucceedPacket : public AbstractPacket {
public:
    PrefabPushSucceedPacket() : AbstractPacket() {}
    virtual ~PrefabPushSucceedPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {}
    virtual bool parse(PacketReceiver &client) override { return true; }
};
struct TickPacket : public AbstractPacket {
public:
    std::array<std::array<uint8_t, Map::width>, Map::height> grid;
    std::optional<Piece> piece;
    TickPacket() : AbstractPacket(), grid(), piece(std::nullopt) {}
    virtual ~TickPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        for (const std::array<uint8_t, Map::width> &row : this->grid) {
            for (const uint8_t cell : row) {
                AbstractPacket::pushUInt8(buffer, cell);
            }
        }
        AbstractPacket::pushUInt8(buffer, static_cast<uint8_t>(this->piece.has_value() ? 1 : 0));
        if (this->piece.has_value()) {
            AbstractPacket::pushInt16(buffer, this->piece->x);
            AbstractPacket::pushInt16(buffer, this->piece->y);
            AbstractPacket::pushUInt8(buffer, this->piece->orientation);
            for (size_t row = 0; row < Piece::height; row++) {
                for (size_t col = 0; col < Piece::width; col++) {
                    AbstractPacket::pushUInt8(buffer, this->piece->getPrefabCell(row, col));
                }
            }
        }
    }
    virtual bool parse(PacketReceiver &client) override {
        std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(Map::width * Map::height + 1);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        for (size_t row = 0; row < Map::height; row++) {
            for (size_t col = 0; col < Map::width; col++) {
                this->grid[row][col] = AbstractPacket::parseUInt8(reserve);
            }
        }
        bool pieceExists = AbstractPacket::parseUInt8(reserve) != 0;
        if (!pieceExists) { this->piece = std::nullopt; }
        else {
            reserveTry = client.read(5 + Piece::width * Piece::height);
            if (!reserveTry.has_value()) { return false; }
            reserve = reserveTry.value();
            if (!this->piece.has_value()) { this->piece.emplace(0, 0, 0); }
            this->piece->x = AbstractPacket::parseInt16(reserve);
            this->piece->y = AbstractPacket::parseInt16(reserve);
            this->piece->orientation = AbstractPacket::parseUInt8(reserve);
            std::array<std::array<uint8_t, Piece::width>, Piece::height> tempPrefab = std::array<std::array<uint8_t, Piece::width>, Piece::height>();
            for (size_t row = 0; row < Piece::height; row++) {
                for (size_t col = 0; col < Piece::width; col++) {
                    tempPrefab[row][col] = AbstractPacket::parseUInt8(reserve);
                }
            }
            this->piece->copy(tempPrefab);
        }
        return true;
    }
};
struct TickDeltaPacket : public AbstractPacket {
public:
    int16_t x, y;
    uint8_t orientation;
    TickDeltaPacket() : AbstractPacket(), x(0), y(0), orientation(0) {}
    virtual ~TickDeltaPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushInt16(buffer, this->x);
        AbstractPacket::pushInt16(buffer, this->y);
        AbstractPacket::pushUInt8(buffer, this->orientation);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(5);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->x = AbstractPacket::parseInt16(reserve);
        this->y = AbstractPacket::parseInt16(reserve);
        this->orientation = AbstractPacket::parseUInt8(reserve);
        return true;
    }
};
struct TickRateChangePacket : public AbstractPacket {
public:
    bool fast;
    TickRateChangePacket() : AbstractPacket(), fast(false) {}
    virtual ~TickRateChangePacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt8(buffer, this->fast ? 1 : 0);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(1);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->fast = AbstractPacket::parseUInt8(reserve) != 0;
        return true;
    }
};
struct SpawnPiecePacket : public AbstractPacket {
public:
    Piece piece;
    SpawnPiecePacket() : AbstractPacket(), piece(0, 0, 0) {}
    virtual ~SpawnPiecePacket() = default;
    
    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushInt16(buffer, this->piece.x);
        AbstractPacket::pushInt16(buffer, this->piece.y);
        AbstractPacket::pushUInt8(buffer, this->piece.orientation);
        for (size_t row = 0; row < Piece::height; row++) {
            for (size_t col = 0; col < Piece::width; col++) {
                AbstractPacket::pushUInt8(buffer, this->piece.getPrefabCell(row, col));
            }
        }
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(5 + Piece::width * Piece::height);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->piece.x = AbstractPacket::parseInt16(reserve);
        this->piece.y = AbstractPacket::parseInt16(reserve);
        this->piece.orientation = AbstractPacket::parseUInt8(reserve);
        std::array<std::array<uint8_t, Piece::width>, Piece::height> tempPrefab = std::array<std::array<uint8_t, Piece::width>, Piece::height>();
        for (size_t row = 0; row < Piece::height; row++) {
            for (size_t col = 0; col < Piece::width; col++) {
                tempPrefab[row][col] = AbstractPacket::parseUInt8(reserve);
            }
        }
        this->piece.copy(tempPrefab);
        return true;
    }
};