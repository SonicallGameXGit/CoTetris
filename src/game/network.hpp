#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <SDL3_net/SDL_net.h>

namespace Network {
    // ! Protocol version number. Increment this when making changes to the protocol that are not backwards compatible.
    static const uint32_t PROTOCOL_VERSION = 4;
    // Room codes are fixed-length so both sides can frame them without a length
    // prefix of their own. The relay generates them from an unambiguous alphabet
    // (no 0/O, 1/I, etc), since a human has to read one off a friend out loud.
    static constexpr size_t ROOM_CODE_LENGTH = 5;
    using RoomCode = std::array<char, ROOM_CODE_LENGTH>;
}

// GG, Claude Code! It completely makes sense.
// Prefixes:
//  - C = client -> server
//  - S = server -> client
//  - SC = server <-> client
// The player's client owns the one simulation. The server hands it a seed, relays
// its board to everyone else, and passes the builder's prefabs back to it; nothing
// here asks the server to move a piece, because nothing waits on the answer.
enum class PacketId : uint8_t {
    SC_Handshake,
    // Sent right after the handshake, before any role is assigned: the relay
    // hosts many concurrent rooms, so a connection has to say which one it
    // belongs to before it can be told whether it's the player, the builder,
    // or a spectator.
    C_CreateRoom, S_RoomCreated, C_JoinRoom, S_RoomJoinResult,
    S_ClientTypeSet, S_GameStart,
    SC_MapState,
    SC_PrefabPush, C_PrefabPushResult, S_PrefabPushSucceed,
    Invalid = 255,
};

// Nice job, Claude Code!
// Every packet is framed by an id and the length of the payload that follows it.
// Without the length, a receiver that meets an id it does not know has no way to
// tell where that packet ends, so it cannot skip it and the stream is desynchronised
// from that point on. With it, an unknown packet costs nothing but a log line.
struct PacketPrefix {
    // 1 byte of id, then 2 bytes of little-endian payload length.
    static constexpr size_t size = 3;
    // The largest payload we send is a full board (a few hundred bytes), so 16 bits
    // of length is far more headroom than the protocol will ever need.
    static constexpr size_t maxPayloadLength = 0xFFFF;

    PacketId id;
    uint16_t length;
};

struct PacketReceiver {
private:
    NET_StreamSocket *socket;
    std::vector<uint8_t> buffer;
    std::optional<PacketPrefix> prefix;
    size_t pointer;
    // Set when a handler asks for more bytes than the packet said it carries, which
    // means the packet is malformed. It is dropped whole rather than left at the
    // head of the stream, where it would block every packet behind it forever.
    bool malformed;
    bool own;
public:
    PacketReceiver(NET_StreamSocket *socket, bool own) : socket(socket), buffer(), prefix(std::nullopt), pointer(0), malformed(false), own(own) {}
    ~PacketReceiver() {
        if (this->own) {
            NET_DestroyStreamSocket(this->socket);
        }
    }

    // The buffer always begins at a packet prefix, so incoming bytes are simply
    // appended; no part of the stream needs special treatment any more.
    void receive(const uint8_t *data, size_t length) {
        this->buffer.insert(this->buffer.end(), data, data + length);
    }
    // Drops the packet at the head of the stream in its entirety, whether or not
    // the handler read all of it. This is what makes an unhandled or partially
    // read packet harmless instead of stream-corrupting.
    void flush() {
        if (!this->prefix.has_value()) { return; }
        const size_t total = PacketPrefix::size + static_cast<size_t>(this->prefix->length);
        this->buffer.erase(this->buffer.begin(), this->buffer.begin() + total);
        this->prefix = std::nullopt;
        this->pointer = 0;
        this->malformed = false;
    }
    void rewind() {
        this->pointer = 0;
    }

    const std::optional<std::vector<uint8_t>::const_iterator> read(const size_t length) {
        if (!this->prefix.has_value()) { return std::nullopt; }
        // Reads are bounded by the declared payload, so a short or hostile packet
        // can never read its way into the bytes of the packet behind it.
        if (this->pointer + length > static_cast<size_t>(this->prefix->length)) {
            this->malformed = true;
            return std::nullopt;
        }
        const std::vector<uint8_t>::const_iterator it = this->buffer.begin() + PacketPrefix::size + this->pointer;
        this->pointer += length;
        return it;
    }

    // Reports the packet at the head of the stream only once every one of its bytes
    // has arrived, so a handler never has to cope with a half-delivered packet.
    std::optional<PacketId> getPacketId() {
        if (this->malformed) { this->flush(); }
        if (!this->prefix.has_value()) {
            if (this->buffer.size() < PacketPrefix::size) { return std::nullopt; }
            PacketPrefix decoded;
            decoded.id = static_cast<PacketId>(this->buffer[0]);
            decoded.length = static_cast<uint16_t>(
                static_cast<uint16_t>(this->buffer[1]) |
                (static_cast<uint16_t>(this->buffer[2]) << 8)
            );
            this->prefix = decoded;
        }
        if (this->buffer.size() < PacketPrefix::size + static_cast<size_t>(this->prefix->length)) { return std::nullopt; }
        return this->prefix->id;
    }
    uint16_t getPacketLength() const { return this->prefix.has_value() ? this->prefix->length : 0; }
    NET_StreamSocket *getSocket() const { return this->socket; }
};
struct PacketSender {
private:
    std::vector<uint8_t> buffer;
public:
    PacketSender(const PacketId packetId, const std::vector<uint8_t> &data) : buffer() {
        buffer.reserve(PacketPrefix::size + data.size());
        buffer.push_back(static_cast<uint8_t>(packetId));
        // Placeholder for the payload length. The caller appends the payload after
        // construction via getMutableBuffer(), so the real value is only known by
        // the time send() runs.
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.insert(buffer.end(), data.begin(), data.end());
    }
    virtual ~PacketSender() = default;

    void send(NET_StreamSocket *socket) {
        const size_t payloadLength = this->buffer.size() - PacketPrefix::size;
        if (payloadLength > PacketPrefix::maxPayloadLength) {
            fprintf(stderr, "Refusing to send a %zu byte payload for packet %d: the length prefix only holds %zu.\n",
                payloadLength, static_cast<int>(this->buffer[0]), PacketPrefix::maxPayloadLength);
            return;
        }
        this->buffer[1] = static_cast<uint8_t>(payloadLength & 0xFF);
        this->buffer[2] = static_cast<uint8_t>((payloadLength >> 8) & 0xFF);
        NET_WriteToStreamSocket(socket, this->buffer.data(), this->buffer.size());
    }
    std::vector<uint8_t> &getMutableBuffer() { return this->buffer; }
};

class AbstractPacket {
protected:
    static void pushUInt8(std::vector<uint8_t> &buffer, const uint8_t value) {
        buffer.push_back(value);
    }
    static void pushInt16(std::vector<uint8_t> &buffer, const int16_t value) {
        buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }
    static void pushUInt32(std::vector<uint8_t> &buffer, const uint32_t value) {
        buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
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

class HandshakePacket : public AbstractPacket {
public:
    uint32_t protocolVersion;
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
// Sent right after the handshake, before any role is assigned: it asks the
// relay to allocate a fresh room and make this connection its first member.
class CreateRoomPacket : public AbstractPacket {
public:
    CreateRoomPacket() : AbstractPacket() {}
    virtual ~CreateRoomPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {}
    virtual bool parse(PacketReceiver &client) override { return true; }
};
// Answers C_CreateRoom with the code the relay generated, so the creator has
// something to hand to whoever it wants to play with.
class RoomCreatedPacket : public AbstractPacket {
public:
    Network::RoomCode code;
    RoomCreatedPacket() : AbstractPacket(), code() {}
    virtual ~RoomCreatedPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        for (const char c : this->code) {
            AbstractPacket::pushUInt8(buffer, static_cast<uint8_t>(c));
        }
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(Network::ROOM_CODE_LENGTH);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator it = reserveTry.value();
        for (char &c : this->code) {
            c = static_cast<char>(AbstractPacket::parseUInt8(it));
        }
        return true;
    }
};
// Sent right after the handshake, in place of C_CreateRoom, when this
// connection wants to join a room somebody else already created.
class JoinRoomPacket : public AbstractPacket {
public:
    Network::RoomCode code;
    JoinRoomPacket() : AbstractPacket(), code() {}
    virtual ~JoinRoomPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        for (const char c : this->code) {
            AbstractPacket::pushUInt8(buffer, static_cast<uint8_t>(c));
        }
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(Network::ROOM_CODE_LENGTH);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator it = reserveTry.value();
        for (char &c : this->code) {
            c = static_cast<char>(AbstractPacket::parseUInt8(it));
        }
        return true;
    }
};
// Answers C_JoinRoom. A failure means the code did not match any room
// currently open on the relay (typo'd, or the room has already ended).
class RoomJoinResultPacket : public AbstractPacket {
public:
    bool success;
    RoomJoinResultPacket() : AbstractPacket(), success(false) {}
    virtual ~RoomJoinResultPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt8(buffer, this->success ? 1 : 0);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(1);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->success = AbstractPacket::parseUInt8(reserve) != 0;
        return true;
    }
};
enum class ClientType : uint8_t {
    Player = 0, Builder = 1, Spectator = 2, Invalid = 255
};
class ClientTypeSetPacket : public AbstractPacket {
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
// A prefab travelling under SC_PrefabPush in both directions: the builder offers
// one to the server, and the server relays it unchanged to the player's client,
// which owns the prefab stack and decides whether it fits.
class PrefabPushPacket : public AbstractPacket {
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
class PrefabPushSucceedPacket : public AbstractPacket {
public:
    PrefabPushSucceedPacket() : AbstractPacket() {}
    virtual ~PrefabPushSucceedPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {}
    virtual bool parse(PacketReceiver &client) override { return true; }
};
// A complete snapshot of the playfield, travelling under SC_MapState in both
// directions: the player's client sends it whenever its simulation changes, and
// the server relays it unchanged to the builder and any spectators. The receiver
// always knows which way it came from, so one id is enough.
class MapStatePacket : public AbstractPacket {
public:
    std::array<std::array<uint8_t, Map::width>, Map::height> grid;
    std::optional<Piece> piece;
    MapStatePacket() : AbstractPacket(), grid(), piece(std::nullopt) {}
    virtual ~MapStatePacket() = default;

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
// Hands the player's client the seed for the piece sequence, so it can spawn
// pieces itself instead of waiting a round trip for the server to name each one.
// The server keeps the seed too, which is what makes the sequence reproducible
// if it ever needs to check the player's work.
class GameStartPacket : public AbstractPacket {
public:
    uint32_t seed;
    GameStartPacket() : AbstractPacket(), seed(0) {}
    virtual ~GameStartPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt32(buffer, this->seed);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(4);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->seed = AbstractPacket::parseUInt32(reserve);
        return true;
    }
};
// The player's client owns the prefab stack, so only it can say whether a push
// fit. This carries that answer back to the server, which forwards it to the
// builder as S_PrefabPushSucceed.
class PrefabPushResultPacket : public AbstractPacket {
public:
    bool accepted;
    PrefabPushResultPacket() : AbstractPacket(), accepted(false) {}
    virtual ~PrefabPushResultPacket() = default;

    virtual void build(std::vector<uint8_t> &buffer) const override {
        AbstractPacket::pushUInt8(buffer, this->accepted ? 1 : 0);
    }
    virtual bool parse(PacketReceiver &client) override {
        const std::optional<std::vector<uint8_t>::const_iterator> reserveTry = client.read(1);
        if (!reserveTry.has_value()) { return false; }
        std::vector<uint8_t>::const_iterator reserve = reserveTry.value();
        this->accepted = AbstractPacket::parseUInt8(reserve) != 0;
        return true;
    }
};