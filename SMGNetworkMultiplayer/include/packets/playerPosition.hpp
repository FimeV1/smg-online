#ifndef PACKETS_PLAYERPOSITION_HPP
#define PACKETS_PLAYERPOSITION_HPP

#include "packets.hpp"
#include "playerCommon.hpp"
#include "timestamps.hpp"
#include <JSystem/JGeometry/TVec.hpp>

namespace Packets {

class _PlayerPosition {
    const static u32 implementationSize;
public:

    static Multiplayer::Id consoleId;

    Multiplayer::Id playerId;

    Timestamps::ServerTimestamp timestamp;
    Timestamps::LocalTimestamp arrivalTime;

    TVec3f position;
    TVec3f velocity;
    TVec3f direction;
    s32 currAnmIdx;
    s32 defaultAnmIdx;
    f32 anmSpeed;
    
    enum {
        O_STATE_HIPDROP = 1
    }; // State flags

    u8 stateFlags;

    // Player colour (see playerColors.hpp); 0 = normal clothes
    u8 color;

    // Where the player is. Players only see each other when both match.
    u8 scenario;
    u32 stageHash; // 0 = unknown / not in a stage

    // Blend weights of the 4 animation tracks (0-255 = 0.0-1.0). Walking and
    // swimming are blends of several tracks, so without these a remote player
    // would stay in the first track's pose.
    u8 trackWeights[4];

    inline bool isHipDropStun() const {return stateFlags & O_STATE_HIPDROP;}

    inline _PlayerPosition()
        : timestamp(Timestamps::makeEmptyServerTimestamp()),
        stateFlags(0),
        color(0),
        scenario(0),
        stageHash(0)
         {}
    NetReturn netWriteToBuffer(void *buff, u32 len) const;
    static NetReturn netReadFromBuffer(Packet<_PlayerPosition> *out, const void *buff, u32 len);
    static inline Tag getTag() {return PLAYER_POSITION;}
    inline u32 getSize() const {return implementationSize;}
};

typedef Packet<_PlayerPosition> PlayerPosition;

}

#endif
