#ifndef MULTIPLAYER_HPP
#define MULTIPLAYER_HPP

#include "atomic.h"
#include "packets/playerPosition.hpp"

class AlignmentState;

namespace Multiplayer {

// How many players can be SEEN at once in one stage, including this console.
// The server can host many more than this; players in other galaxies cost
// nothing here, and their save progress still syncs. Each remote slot costs a
// skeleton buffer + animation player from the scene heap, plus draw time.
const u32 MAX_PLAYER_COUNT = 16;
const u32 MAX_REMOTE_PLAYERS = MAX_PLAYER_COUNT - 1;

// Must match server/smg_server.py
const u32 MAJOR = 1;
const u32 MINOR = 0;

const u8 NO_OWNER = 0xFF;

// A remote player disappears after this many frames without an update
// (left the stage, disconnected, or is behind a loading screen).
const u16 PLAYER_TIMEOUT_FRAMES = 90;
// With no word from the server for this long, start the handshake again.
const u16 SERVER_TIMEOUT_FRAMES = 5 * 60;

typedef u32 PlayerBufferStatus;

inline u32 getMostRecentBuffer(u8 player, u32 bufferStatus) {
    return bufferStatus >> player & 1;
}

inline u32 setMostRecentBuffer(u8 player, u32 buffer, u32 bufferStatus) {
    return bufferStatus & ~(1 << player) | buffer << player;
}

struct PlayerDoubleBuffer {
    simplelock_t locks[2];
    Packets::PlayerPosition pos[2];
};

// Shared between the game thread and the network callbacks. The callbacks run
// in interrupt context: they can preempt the game thread but never the other
// way round, so single-word reads/writes are safe in both directions. Anything
// bigger goes through the double buffer.
struct MultiplayerInfo {
    PlayerBufferStatus status; // written by the callbacks only
    PlayerDoubleBuffer players[MAX_REMOTE_PLAYERS];

    // Which server-side player id feeds each local slot. Claimed by the
    // callbacks, released (NO_OWNER) by the game thread.
    volatile u8 slotOwner[MAX_REMOTE_PLAYERS];
    // Bumped by the callbacks for every position stored in a slot.
    volatile u32 rxCount[MAX_REMOTE_PLAYERS];

    // Bumped for every record received from the server (liveness).
    volatile u32 serverRxCount;
    // Bumped every time a handshake completes.
    volatile u32 sessionCount;
    volatile u32 serverEpoch;

    // Where the local player is; written by the game thread. Positions from
    // players elsewhere are dropped before they take a slot.
    volatile u32 stageHash;
    volatile u8 scenario;
};

extern MultiplayerInfo info;
extern bool connected;
// This player's colour, read from serverIP.txt ("address:port c=3")
extern u8 localColor;

class MultiplayerAccess {
    Packets::PlayerPosition pos[MAX_REMOTE_PLAYERS];
public:
    // Game thread only.
    bool isPlayerActive(u32 i) const;
    // Changes every time slot `i` starts showing a (possibly different) player,
    // so per-slot render state can be reset.
    u32 getPlayerGeneration(u32 i) const;
    const Packets::PlayerPosition& getPlayerPosRaw(u32);
    bool isPlayerPosEstimateSet(u32) const;
    void setPlayerPosEstimate(u32) const;
    AlignmentState& getPlayerPosEstimate(u32) const;
};

extern MultiplayerAccess access;

// Where remote player `i` is drawn this frame, or nullptr if it is not shown
// yet. Game thread only.
const TVec3f* getPlayerRenderPos(u32 i);

// Call when a stage has (re)loaded: forgets every remote player and asks the
// server for the whole progress log again.
void onStageInit();

}

#endif
