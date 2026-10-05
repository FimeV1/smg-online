#ifndef PACKETS_HPP
#define PACKETS_HPP

#include <revolution/types.h>
#include "netCommon.hpp"

namespace Packets {

static const u32 MAX_PACKET_SIZE = 128;

// The server bundles several records (tag + payload) into one datagram, so the
// receive buffer is much larger than any single packet. Both sizes must stay
// multiples of 32 (IOS wants 32-byte aligned buffers).
static const u32 RX_BUFFER_SIZE = 1408;
static const u32 TX_BUFFER_SIZE = 8 * MAX_PACKET_SIZE;

enum Tag {
    CONNECT = 0,
    ACK,
    SERVER_INITIAL_RESPONSE,
    PLAYER_POSITION,
    TIME_QUERY,
    TIME_RESPONSE,
    STAR_PIECE,
    GAME_PROGRESS,
    MAX_TAG
};

static const u32 INVALID_PAYLOAD_SIZE = 0xFFFFFFFF;

// Size of the payload that follows `tag` on the wire, or INVALID_PAYLOAD_SIZE.
// THIS FUNCTION MUST NOT REQUIRE A THREADED CONTEXT
u32 getPayloadSize(Tag tag);

namespace implementation {
    class ReliablePacket;
};

class ReliablePacketCode {
protected:
    friend class implementation::ReliablePacket;
    u32 seqNum;
public:
    inline ReliablePacketCode() {}
    inline ReliablePacketCode(u32 seqNum) : seqNum(seqNum) {}

    // THIS FUNCTION MUST NOT REQUIRE A THREADED CONTEXT
    inline bool verify(const ReliablePacketCode &reliablePacketCode) const {
        return seqNum == reliablePacketCode.seqNum;
    }
};

template<typename T>
class Packet : public T {
public:


    // Write the contents of this packet into `buff` so we can send
    // it over the network (perform any necessary endian conversions too).
    // Return the number of bytes written and possibly an error
    inline NetReturn netWriteToBuffer(void *buff, u32 len) const {
        return T::netWriteToBuffer(buff, len);
    }

    // Read the contents of `buff` into `out`. Return the number of bytes read
    // and possibly an error
    inline static NetReturn netReadFromBuffer(Packet<T> *out, const void *buff, u32 len) {
        return T::netReadFromBuffer(out, buff, len);
    }

    inline static Tag getTag() {return T::getTag();}
    inline u32 getSize() const {return T::getSize();}
};

}

#endif
