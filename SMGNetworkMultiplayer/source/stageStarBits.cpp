#include "stageStarBits.hpp"
#include "multiplayer.hpp"
#include "globalTransmitter.hpp"
#include "packets/playerPosition.hpp"
#include "packets/starPiece.hpp"

#include <Game/System/GameDataFunction.hpp>

namespace StageStarBits {

static const u32 NUM_IDS = 256; // every possible global player id

// Newest report per sender. Written by the network thread, one word at a
// time: generation in the top half, the count in the bottom half.
static volatile u32 latest[NUM_IDS];
static volatile bool hasLatest[NUM_IDS];

// What has been added to our counter for each sender (game thread)
static u8 appliedGeneration[NUM_IDS];
static s16 appliedCount[NUM_IDS];

// Our own net change since the level loaded, and the counter as we left it
static s16 ownCount = 0;
static u8 ownGeneration = 0x80;
static s32 lastSeen = 0;
static bool needBaseline = true;

static s16 sentCount = 0;
static u8 sentGeneration = 0;
static u16 sendTimer = 0;
static const u16 SEND_MIN_FRAMES = 12;   // at most 5 reports a second
static const u16 SEND_REPEAT_FRAMES = 120; // repeat for late joiners and lost packets

void onNetReport(u8 senderGlobalId, u8 generation, s16 count) {
    latest[senderGlobalId] = ((u32)generation << 16) | (u16)count;
    hasLatest[senderGlobalId] = true;
}

static void forgetSenders() {
    for(u32 i = 0; i < NUM_IDS; i++) {
        hasLatest[i] = false;
        appliedGeneration[i] = 0;
        appliedCount[i] = 0;
    }
}

void onStageChanged() {
    forgetSenders();
}

void onStageInit() {
    // A new generation tells the others that our count starts over (new
    // level, retry, death): they keep what we gave them before.
    ownGeneration = 0x80 | ((ownGeneration + 1) & 0x7F);
    ownCount = 0;
    needBaseline = true;
}

void update() {
    if(!GameDataFunction::getCurrentGameDataHolder()) return;
    s32 have = GameDataFunction::getStarPieceNum();

    if(needBaseline) {
        needBaseline = false;
        // An empty counter means the game started this level from zero, so
        // the others' star bits have to be added again. A counter the game
        // kept already contains them.
        for(u32 i = 0; i < NUM_IDS; i++) {
            if(have == 0 || !hasLatest[i]) {
                appliedGeneration[i] = 0;
                appliedCount[i] = 0;
            }
        }
        lastSeen = have;
    }

    // Whatever changed since last frame is our own doing
    ownCount = (s16)(ownCount + (have - lastSeen));

    for(u32 i = 0; i < NUM_IDS; i++) {
        if(!hasLatest[i]) continue;
        const u32 word = latest[i];
        const u8 generation = (u8)(word >> 16);
        const s16 count = (s16)(u16)word;

        if(generation != appliedGeneration[i]) {
            appliedGeneration[i] = generation;
            appliedCount[i] = 0;
        }
        if(count != appliedCount[i]) {
            GameDataFunction::addStarPiece(count - appliedCount[i]);
            appliedCount[i] = count;
        }
    }
    lastSeen = GameDataFunction::getStarPieceNum();

    if(sendTimer > 0) sendTimer--;
    if(!Multiplayer::connected || !Multiplayer::info.stageHash) return;

    const bool changed = ownCount != sentCount || ownGeneration != sentGeneration;
    // Nothing to tell anyone until we have picked something up
    if(!changed && ownCount == 0) return;
    if(sendTimer > (changed ? SEND_REPEAT_FRAMES - SEND_MIN_FRAMES : 0)) return;

    Packets::StarPiece packet;
    packet.playerId = Packets::PlayerPosition::consoleId;
    packet.countGeneration = ownGeneration;
    packet.count = ownCount;
    packet.initLineStart.x = 0.0f; packet.initLineStart.y = 0.0f; packet.initLineStart.z = 0.0f;
    packet.initLineEnd.x = 0.0f; packet.initLineEnd.y = 0.0f; packet.initLineEnd.z = 0.0f;
    if(Multiplayer::transmitter.addPacket(packet).err == NetReturn::OK) {
        sentCount = ownCount;
        sentGeneration = ownGeneration;
        sendTimer = SEND_REPEAT_FRAMES;
    }
}

}
