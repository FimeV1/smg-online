#include "packets.hpp"

#include "packets/ack.hpp"
#include "packets/connect.hpp"
#include "packets/playerPosition.hpp"
#include "packets/serverInitialResponse.hpp"
#include "packets/beacon.hpp"
#include "packets/starPiece.hpp"
#include "packets/gameProgress.hpp"
#include "accurateTime.hpp"
#include <cstring>

namespace Packets {

namespace implementation {

    template<typename T>
    class PacketTimestamp {
        s32 tMs;
     public:
        PacketTimestamp(const Timestamps::ClockboundTimestamp<T> &t) : tMs(t.t.timeMs) {}
        Timestamps::ClockboundTimestamp<T> toHL() const {
            Timestamps::ClockboundTimestamp<T> ret = {tMs};
            return ret;
        }
    };

    typedef PacketTimestamp<Timestamps::ServerClockTag> ServerPacketTimestamp;

    class ReliablePacket {
    protected:
        u32 seqNum;
    public:
        ReliablePacket(const ReliablePacketCode &check) : seqNum(check.seqNum) {}
        ReliablePacketCode toCode() const {return ReliablePacketCode(seqNum);}
    };

    static const u64 CONNECT_MAGIC = 0x436F6E6E65637400; // "Connect"
    
    struct Connect {
        u8 magic[8];
        u32 major;
        u32 minor;
    };

    struct Ack {
        u32 seqNum;
    };

    struct PlayerPosition {
        u8 playerId;
        u8 stateFlags;
        u8 scenario;
        u8 color; // was padding: older clients send 0 = normal clothes

        ServerPacketTimestamp timestamp;
        
        f32 positionX;
        f32 positionY;
        f32 positionZ;

        f32 velocityX;
        f32 velocityY;
        f32 velocityZ;

        f32 directionXx; // Base Matrix, column 1 row 1 (+ 3.0f if row 3 is negative)
        f32 directionXy; // Base Matrix, column 1 row 2
        f32 directionYxz; // ^Y*(^i|^j)x^X (+ 3.0f if Y*rejection is negative)
        
        s16 currAnmIdx;
        s16 defaultAnmIdx;
        f32 anmSpeed;

        u8 trackWeights[4];
        u32 stageHash;
    };

    struct StarPiece {
        u8 playerId;
        u8 padding[3];

        ServerPacketTimestamp timestamp;

        TVec3f initLineStart;
        TVec3f initLineEnd;
    };

    struct GameProgress {
        u8 playerId;
        u8 eventType;
        u8 padding[2];
        u32 seq;   // Big endian on the wire (Wii native)
        s32 arg;   // Big endian on the wire
        s32 value; // Big endian on the wire
        char name[48];
    };

    struct ServerInitialResponse {
        u32 major;
        u32 minor;
        u32 epoch;
        u8 id;
        u8 maxPlayers;
        u8 padding[2];
    };
    
    struct TimeQuery {
        u32 timeMs;
        ReliablePacket check;
    };
    
    struct TimeResponse {
        u32 timeMs;
        ReliablePacket check;
    };

    // The server (server/smg_server.py) hardcodes these sizes. A mismatch here
    // must be a build error, not a silent desync.
    #define WIRE_SIZE_CHECK(type, size) typedef char type##_wire_size_check[sizeof(type) == (size) ? 1 : -1]
    WIRE_SIZE_CHECK(Connect, 16);
    WIRE_SIZE_CHECK(Ack, 4);
    WIRE_SIZE_CHECK(ServerInitialResponse, 16);
    WIRE_SIZE_CHECK(PlayerPosition, 60);
    WIRE_SIZE_CHECK(TimeQuery, 8);
    WIRE_SIZE_CHECK(TimeResponse, 8);
    WIRE_SIZE_CHECK(StarPiece, 32);
    WIRE_SIZE_CHECK(GameProgress, 64);
    #undef WIRE_SIZE_CHECK

};

u32 getPayloadSize(Tag tag) {
    switch(tag) {
        case CONNECT: return sizeof(implementation::Connect);
        case ACK: return sizeof(implementation::Ack);
        case SERVER_INITIAL_RESPONSE: return sizeof(implementation::ServerInitialResponse);
        case PLAYER_POSITION: return sizeof(implementation::PlayerPosition);
        case TIME_QUERY: return sizeof(implementation::TimeQuery);
        case TIME_RESPONSE: return sizeof(implementation::TimeResponse);
        case STAR_PIECE: return sizeof(implementation::StarPiece);
        case GAME_PROGRESS: return sizeof(implementation::GameProgress);
        default: return INVALID_PAYLOAD_SIZE;
    }
}

const u32 _Connect::implementationSize = sizeof(implementation::Connect);
const u32 _Ack::implementationSize = sizeof(implementation::Ack);
const u32 _PlayerPosition::implementationSize = sizeof(implementation::PlayerPosition);
Multiplayer::Id _PlayerPosition::consoleId;
const u32 _ServerInitialResponse::implementationSize = sizeof(implementation::ServerInitialResponse);
const u32 _TimeQuery::implementationSize = sizeof(implementation::TimeQuery);
u32 _TimeQuery::seqNum = 0;
const u32 _TimeResponse::implementationSize = sizeof(implementation::TimeResponse);
const u32 _StarPiece::implementationSize = sizeof(implementation::StarPiece);
const u32 _GameProgress::implementationSize = sizeof(implementation::GameProgress);

NetReturn _Connect::netWriteToBuffer(void *buff, u32 len) const {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    implementation::Connect *packet = (implementation::Connect *)buff;
    *(u64*)packet->magic = implementation::CONNECT_MAGIC;
    packet->major = major;
    packet->minor = minor;
    return NetReturn::Ok(implementationSize);
}

NetReturn _Connect::netReadFromBuffer(Connect *out, const void *buff, u32 len) {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    const implementation::Connect *packet = (const implementation::Connect *)buff;
    if(*(u64*)packet->magic != implementation::CONNECT_MAGIC) return NetReturn::InvalidData();
    out->major = packet->major;
    out->minor = packet->minor;
    return NetReturn::Ok(implementationSize);
}

NetReturn _Ack::netWriteToBuffer(void *buff, u32 len) const {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    implementation::Ack *packet = (implementation::Ack *)buff;
    packet->seqNum = seqNum;
    return NetReturn::Ok(implementationSize);
}

NetReturn _Ack::netReadFromBuffer(Ack *out, const void *buff, u32 len) {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    const implementation::Ack *packet = (const implementation::Ack *)buff;
    out->seqNum = packet->seqNum;
    return NetReturn::Ok(implementationSize);
}

NetReturn _ServerInitialResponse::netWriteToBuffer(void *buff, u32 len) const {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    implementation::ServerInitialResponse *packet = (implementation::ServerInitialResponse *)buff;
    packet->major = major;
    packet->minor = minor;
    packet->epoch = epoch;
    packet->id = id;
    packet->maxPlayers = maxPlayers;
    packet->padding[0] = 0;
    packet->padding[1] = 0;
    return NetReturn::Ok(implementationSize);
}

NetReturn _ServerInitialResponse::netReadFromBuffer(ServerInitialResponse *out, const void *buff, u32 len) {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    const implementation::ServerInitialResponse *packet = (const implementation::ServerInitialResponse *)buff;
    out->major = packet->major;
    out->minor = packet->minor;
    out->epoch = packet->epoch;
    out->id = packet->id;
    out->maxPlayers = packet->maxPlayers;
    return NetReturn::Ok(implementationSize);
}

NetReturn _PlayerPosition::netWriteToBuffer(void *buff, u32 len) const {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    implementation::PlayerPosition *packet = (implementation::PlayerPosition *)buff;
    
    packet->playerId = playerId.toGlobalId();
    
    packet->stateFlags = stateFlags;
    packet->scenario = scenario;
    packet->color = color;

    packet->timestamp = implementation::ServerPacketTimestamp(timestamp);

    packet->positionX = position.x;
    packet->positionY = position.y;
    packet->positionZ = position.z;

    packet->velocityX = velocity.x;
    packet->velocityY = velocity.y;
    packet->velocityZ = velocity.z;

    packet->directionXx = direction.x;
    packet->directionXy = direction.y;
    packet->directionYxz = direction.z;

    packet->currAnmIdx = currAnmIdx;
    packet->defaultAnmIdx = defaultAnmIdx;
    packet->anmSpeed = anmSpeed;

    for(u32 i = 0; i < 4; i++) packet->trackWeights[i] = trackWeights[i];
    packet->stageHash = stageHash;

    return NetReturn::Ok(implementationSize);
}

NetReturn _PlayerPosition::netReadFromBuffer(PlayerPosition *out, const void *buff, u32 len) {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    const implementation::PlayerPosition *packet = (const implementation::PlayerPosition *)buff;
    
    out->playerId = consoleId.fromGlobalId(packet->playerId);

    out->stateFlags = packet->stateFlags;
    out->scenario = packet->scenario;
    out->color = packet->color;
    out->stageHash = packet->stageHash;
    for(u32 i = 0; i < 4; i++) out->trackWeights[i] = packet->trackWeights[i];

    out->timestamp = packet->timestamp.toHL();

    out->position.set(packet->positionX, packet->positionY, packet->positionZ);
    out->velocity.set(packet->velocityX, packet->velocityY, packet->velocityZ);
    out->direction.set(packet->directionXx, packet->directionXy, packet->directionYxz);

    out->currAnmIdx = packet->currAnmIdx;
    out->defaultAnmIdx = packet->defaultAnmIdx;
    out->anmSpeed = packet->anmSpeed;

    out->arrivalTime = Timestamps::now();

    return NetReturn::Ok(implementationSize);
}

NetReturn _TimeQuery::netWriteToBuffer(void *buff, u32 len) const {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    implementation::TimeQuery *packet = (implementation::TimeQuery *)buff;
    packet->timeMs = timeMs;
    packet->check = implementation::ReliablePacket(check);
    return NetReturn::Ok(implementationSize);
}

NetReturn _TimeQuery::netReadFromBuffer(TimeQuery *, const void *, u32) {
    return NetReturn::InvalidData();
}

NetReturn _TimeResponse::netWriteToBuffer(void *, u32) const {
    return NetReturn::SystemError(0xDEAD); // unreachable
}

NetReturn _TimeResponse::netReadFromBuffer(TimeResponse *out, const void *buff, u32 len) {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    const implementation::TimeResponse *packet = (const implementation::TimeResponse *)buff;
    out->timeMs = packet->timeMs;
    out->check = packet->check.toCode();
    return NetReturn::Ok(implementationSize);
}

NetReturn _StarPiece::netWriteToBuffer(void *buff, u32 len) const {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    implementation::StarPiece *packet = (implementation::StarPiece *)buff;

    packet->playerId = playerId.toGlobalId();
   
    packet->padding[0] = countGeneration;
    packet->padding[1] = (u8)((u16)count >> 8);
    packet->padding[2] = (u8)count;

    packet->timestamp = implementation::ServerPacketTimestamp(timestamp);

    packet->initLineStart = initLineStart;
    packet->initLineEnd = initLineEnd;

    return NetReturn::Ok(implementationSize);
}

NetReturn _StarPiece::netReadFromBuffer(StarPiece *out, const void *buff, u32 len) {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    const implementation::StarPiece *packet = (const implementation::StarPiece *)buff;
    
    out->playerId = _PlayerPosition::consoleId.fromGlobalId(packet->playerId);

    out->timestamp = packet->timestamp.toHL();

    out->initLineStart = packet->initLineStart;
    out->initLineEnd = packet->initLineEnd;

    out->countGeneration = packet->padding[0];
    out->count = (s16)(((u16)packet->padding[1] << 8) | packet->padding[2]);
    out->senderGlobalId = packet->playerId;

    out->arrivalTime = Timestamps::now();

    return NetReturn::Ok(implementationSize);
}

NetReturn _GameProgress::netWriteToBuffer(void *buff, u32 len) const {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    implementation::GameProgress *packet = (implementation::GameProgress *)buff;

    packet->playerId = playerId.toGlobalId();
    packet->eventType = eventType;
    packet->padding[0] = 0;
    packet->padding[1] = 0;

    packet->seq = seq;     // Wii is big-endian -> already network order
    packet->arg = arg;
    packet->value = value;

    memcpy(packet->name, name, sizeof packet->name);
    packet->name[sizeof packet->name - 1] = '\0';

    return NetReturn::Ok(implementationSize);
}

NetReturn _GameProgress::netReadFromBuffer(GameProgress *out, const void *buff, u32 len) {
    if(len < implementationSize) return NetReturn::NotEnoughSpace(implementationSize);
    const implementation::GameProgress *packet = (const implementation::GameProgress *)buff;

    out->playerId = _PlayerPosition::consoleId.fromGlobalId(packet->playerId);
    out->eventType = packet->eventType;
    out->seq = packet->seq;
    out->arg = packet->arg;
    out->value = packet->value;

    memcpy(out->name, packet->name, sizeof out->name);
    out->name[sizeof out->name - 1] = '\0';

    return NetReturn::Ok(implementationSize);
}

}
