#include "packetProcessor.hpp"
#include "packets/connect.hpp"
#include "packets/ack.hpp"
#include "packets/serverInitialResponse.hpp"
#include "packets/playerPosition.hpp"
#include "packets/beacon.hpp"
#include "StarPieceSync.hpp"
#include "stageStarBits.hpp"
#include "packets/gameProgress.hpp"
#include "progressSync.hpp"
#include "beacon.hpp"

namespace Packets {

// A position is out of date when the slot already holds a newer one from the
// same player. A big backwards jump means the clock restarted (server restart),
// not reordering, so it is let through.
static const s32 REORDER_WINDOW_MS = 2000;

static bool isOutdated(const PlayerPosition &held, const PlayerPosition &incoming) {
    if(Timestamps::isEmpty(held.timestamp) || Timestamps::isEmpty(incoming.timestamp)) return false;
    if(held.playerId.toGlobalId() != incoming.playerId.toGlobalId()) return false;
    if(held.timestamp < incoming.timestamp) return false;
    return held.timestamp.t.timeMs - incoming.timestamp.t.timeMs < REORDER_WINDOW_MS;
}

// THIS FUNCTION RUNS IN AN UNTHREADED (INTERRUPT) CONTEXT: it must never block.
NetReturn PacketProcessor::process(Tag tag, const u8 *buffer, u32 len) {
    players->serverRxCount++;

    switch(tag) {
        case CONNECT:
        {
            break;
        }
        case ACK:
        {
            Ack ack;
            NetReturn res = Ack::netReadFromBuffer(&ack, buffer, len);
            if(res.err != NetReturn::OK) return res;

            ProgressSync::onServerAck(ack.seqNum);
            break;
        }
        case SERVER_INITIAL_RESPONSE:
        {
            if(!*connected) {
                ServerInitialResponse sip;
                NetReturn res = ServerInitialResponse::netReadFromBuffer(&sip, buffer, len);
                if(res.err != NetReturn::OK) return res;
                if(sip.major != Multiplayer::MAJOR) return NetReturn::InvalidData();
                PlayerPosition::consoleId = Multiplayer::Id::selfId(sip.id);
                players->serverEpoch = sip.epoch;
                players->sessionCount++;
                *connected = true;
            }

            break;
        }
        case STAR_PIECE:
        {
            StarPiece packet;
            NetReturn res = StarPiece::netReadFromBuffer(&packet, buffer, len);
            if(res.err != NetReturn::OK) return res;

            if(packet.countGeneration & 0x80) {
                StageStarBits::onNetReport(packet.senderGlobalId, packet.countGeneration, packet.count);
            }
            else netStarPieceQueue.write(packet);
            break;
        }
        case GAME_PROGRESS:
        {
            GameProgress packet;
            NetReturn res = GameProgress::netReadFromBuffer(&packet, buffer, len);
            if(res.err != NetReturn::OK) return res;

            ProgressSync::onNetEvent(packet);
            break;
        }
        case PLAYER_POSITION:
        {
            if(!*connected) break;

            PlayerPosition pos;
            NetReturn res = PlayerPosition::netReadFromBuffer(&pos, buffer, len);
            if(res.err != NetReturn::OK) return res;

            const u8 globalId = pos.playerId.toGlobalId();
            if(globalId == PlayerPosition::consoleId.toGlobalId()) break;

            // Only players in the same galaxy + scenario are shown
            const u32 stageHash = players->stageHash;
            if(stageHash == 0 || pos.stageHash != stageHash || pos.scenario != players->scenario) break;

            // Find this player's slot, or claim a free one
            const u32 NO_SLOT = Multiplayer::MAX_REMOTE_PLAYERS;
            u32 slot = NO_SLOT, freeSlot = NO_SLOT;
            for(u32 i = 0; i < Multiplayer::MAX_REMOTE_PLAYERS; i++) {
                const u8 owner = players->slotOwner[i];
                if(owner == globalId) {
                    slot = i;
                    break;
                }
                if(owner == Multiplayer::NO_OWNER && freeSlot == NO_SLOT) freeSlot = i;
            }

            bool isNewOwner = false;
            if(slot == NO_SLOT) {
                if(freeSlot == NO_SLOT) break; // more players here than we can show
                slot = freeSlot;
                players->slotOwner[slot] = globalId;
                isNewOwner = true;
            }

            Multiplayer::PlayerDoubleBuffer &doubleBuff = players->players[slot];

            u32 buffIdx = Multiplayer::getMostRecentBuffer(slot, players->status);

            // The game thread only ever reads these buffers, so peeking at the
            // most recent one without its lock is fine from here.
            if(!isNewOwner && isOutdated(doubleBuff.pos[buffIdx], pos)) break;

            buffIdx = buffIdx == 1 ? 0 : 1;

            if(simplelock_tryLock(&doubleBuff.locks[buffIdx]) != TRY_LOCK_RESULT_OK) {
                buffIdx = buffIdx == 1 ? 0 : 1;
                if(simplelock_tryLock(&doubleBuff.locks[buffIdx]) != TRY_LOCK_RESULT_OK) {
                    return NetReturn::Busy(); // This really should not happen
                }
            }

            doubleBuff.pos[buffIdx] = pos;

            simplelock_release(&doubleBuff.locks[buffIdx]);

            players->status = Multiplayer::setMostRecentBuffer(slot, buffIdx, players->status);
            players->rxCount[slot]++;

            break;
        }
        case TIME_QUERY:
        {
            break;
        }
        case TIME_RESPONSE:
        {
            TimeResponse trp;
            NetReturn res = TimeResponse::netReadFromBuffer(&trp, buffer, len);
            if(res.err != NetReturn::OK) return res;

            Timestamps::beacon.process(trp);
            break;
        }
    }
    return NetReturn::Ok();
}

}
