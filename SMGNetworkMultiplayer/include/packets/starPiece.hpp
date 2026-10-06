#ifndef PACKETS_STARPIECE_HPP
#define PACKETS_STARPIECE_HPP

#include "packets.hpp"
#include "timestamps.hpp"
#include "playerCommon.hpp"
#include <JSystem/JGeometry/TVec.hpp>

namespace Packets {

class _StarPiece {
    const static u32 implementationSize;
public:
    Multiplayer::Id playerId;
    
    Timestamps::ServerTimestamp timestamp;
    
    TVec3f initLineStart;
    TVec3f initLineEnd;
    
    Timestamps::LocalTimestamp arrivalTime;

    // The three bytes after the player id used to be padding. 0 in the first
    // one = a star bit somebody shot (everything above). With its top bit set
    // the packet is instead a report of the sender's star bit count for the
    // level (see stageStarBits.hpp) and carries no star bit.
    u8 countGeneration; // 0, or 0x80 | generation
    s16 count;
    u8 senderGlobalId;  // as received; reports are kept per sender

    inline _StarPiece() : timestamp(Timestamps::makeEmptyServerTimestamp()), countGeneration(0), count(0), senderGlobalId(0) {}
    NetReturn netWriteToBuffer(void *buff, u32 len) const;
    static NetReturn netReadFromBuffer(Packet<_StarPiece> *out, const void *buff, u32 len);
    static inline Tag getTag() {return STAR_PIECE;}
    inline u32 getSize() const {return implementationSize;}
};

typedef Packet<_StarPiece> StarPiece;

}

#endif
