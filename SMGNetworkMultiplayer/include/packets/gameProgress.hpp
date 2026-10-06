#ifndef PACKETS_GAMEPROGRESS_HPP
#define PACKETS_GAMEPROGRESS_HPP

#include "packets.hpp"
#include "playerCommon.hpp"

namespace Packets {

// Generic save-progression event used to keep both players' game data in sync
// for a co-op 100% run (power stars, scenario visits, game-event flags, story
// events, Luma feeding). See progressSync.cpp for how each eventType maps to a
// GameDataHolder call.
enum ProgressEventType {
    PE_POWER_STAR = 0,      // name=galaxy, arg=scenario, value=isOwned
    PE_SCENARIO_VISITED,    // name=galaxy, arg=scenario
    PE_GAME_EVENT_BIT,      // name=flag,   arg=bitIndex, value=on
    PE_STORY_EVENT,         // name=event
    PE_TICO_SEED,           // arg=ticoId,  value=TOTAL star pieces given so far (Luma feeding)
    PE_GAME_EVENT,          // name=flag (e.g. "AppearXxxGalaxy"); galaxy/dome unlocks
    PE_GAME_EVENT_VALUE,    // name=value name, value=u16 (e.g. "new stars until X appears" countdowns, Luigi quest state)
    PE_STAR_BITS            // value=the shared star bit total (banked star bits; the newest value wins)
};

// Every event is idempotent (applying it twice is harmless), which is what lets
// the server replay its whole log to a client at any time.
class _GameProgress {
    const static u32 implementationSize;
public:
    Multiplayer::Id playerId;

    u8 eventType;
    // Client -> server: the sender's own event counter (echoed back in an Ack).
    // Server -> client: index of this event in the server's progress log.
    u32 seq;
    s32 arg;
    s32 value;
    char name[48];

    inline _GameProgress() : eventType(0), seq(0), arg(0), value(0) { name[0] = '\0'; }
    NetReturn netWriteToBuffer(void *buff, u32 len) const;
    static NetReturn netReadFromBuffer(Packet<_GameProgress> *out, const void *buff, u32 len);
    static inline Tag getTag() {return GAME_PROGRESS;}
    inline u32 getSize() const {return implementationSize;}
};

typedef Packet<_GameProgress> GameProgress;

}

#endif
