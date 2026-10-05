#include "net.h"
#include "transmission.hpp"
#include "packetProcessor.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <kamek/hooks.h>
#include <Game/System/DrawSyncManager.hpp>
#include <Game/System/GameSystem.hpp>
#include <Game/SingletonHolder.hpp>
#include <Game/Player/MarioActor.hpp>
#include <Game/Player/MarioAnimator.hpp>
#include "packets/connect.hpp"
#include "packets/playerPosition.hpp"
#include "beacon.hpp"
#include "debug.hpp"
#include "accurateTime.hpp"
#include "alignment.hpp"
#include "netActor.hpp"
#include "progressSync.hpp"
#include "uiIpFsTool.hpp"
#include "playerColors.hpp"

extern kmSymbol init__10GameSystemFv;
extern kmSymbol control__10MarioActorFv;

namespace Timestamps {
    Beacon beacon;
};

namespace Multiplayer {

static struct {
    AlignmentState state;
    bool isInit;
} alignmentStates[MAX_REMOTE_PLAYERS];

// Game-thread view of who is currently shown in each slot
static struct {
    u32 lastRxCount;
    u32 generation;
    u16 staleFrames;
    bool active;
} remotePlayers[MAX_REMOTE_PLAYERS];

static u32 lastSessionCount;
static u32 lastServerRxCount;
static u16 serverSilentFrames;

static bool initialized;
bool connected;
u8 localColor = 0;

static const u32 queryCooldown = 60;
static u32 queryTimer = 0;

MultiplayerInfo info;
MultiplayerAccess access;

Transmission::Transmitter<Packets::PacketProcessor> transmitter;

const static char *SERVER_ADDR_FS = "/CustomCode/serverIP.txt";
const static char *DEBUG_ADDR_FS = "/CustomCode/debugIP.txt";
static sockaddr_in serverAddr = {8, 2, 5029, 0};
static sockaddr_in debugAddr = {8, 2, 5001, 0};

static void init() {
    if(!initialized) {
        s32 err = netinit();
        if(err < 0) return;
        s32 sd = netsocket(2, 2, 0); // AF_INET, SOCK_DGRAM
        if(sd < 0) return;

        u8 *buff = new (32) u8[Packets::RX_BUFFER_SIZE + Packets::TX_BUFFER_SIZE];
        if(buff == nullptr) return;

        // Do not rely on static constructors having run for `info`
        for(u32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
            info.slotOwner[i] = NO_OWNER;
            info.players[i].locks[0] = 0;
            info.players[i].locks[1] = 0;
            info.players[i].pos[0].timestamp = Timestamps::makeEmptyServerTimestamp();
            info.players[i].pos[1].timestamp = Timestamps::makeEmptyServerTimestamp();
        }

        readIpAddrFs(SERVER_ADDR_FS, &serverAddr, &localColor);
        if(localColor >= PlayerColors::NUM_COLORS) localColor = 0;
        readIpAddrFs(DEBUG_ADDR_FS, &debugAddr);

        Transmission::Reader reader(16, buff, Packets::RX_BUFFER_SIZE, sd);
        Transmission::Writer writer (buff + Packets::RX_BUFFER_SIZE, Packets::TX_BUFFER_SIZE, sd, &serverAddr);
        transmitter = Transmission::Transmitter<Packets::PacketProcessor>(reader, writer, Packets::PacketProcessor(&connected, &info));
        Timestamps::beacon.init1();
        NetActor::initStarPieceQueue();
        ProgressSync::init();
        transmitter.init();
        initialized = true;
        setupDebug(sd, &debugAddr);
    }
}


static void initWrapper(unsigned long a, long b) {
    DrawSyncManager::start(a, b);
    init();
}


kmCall(&init__10GameSystemFv + 0x94, initWrapper); // Replaces a call to `DrawSyncManager::start`

// Identifies the stage the local player is in: a hash of the stage name plus
// the scenario (star) number. 0 means "not in a stage".
static u32 getCurrentStage(u8 &scenario) {
    scenario = 0;
    GameSystem *system = SingletonHolder<GameSystem>::sInstance;
    if(!system || !system->mSceneController) return 0;

    // GameSystemSceneController starts with the current SceneControlInfo:
    // char mScene[0x20]; char mStage[0x20]; s32 mCurrentScenarioNo; ...
    const u8 *controller = (const u8 *)system->mSceneController;
    const char *stage = (const char *)(controller + 0x20);
    if(!stage[0]) return 0;
    scenario = (u8)*(const s32 *)(controller + 0x40);

    u32 hash = 0x811C9DC5; // FNV-1a
    for(u32 i = 0; i < 0x20 && stage[i]; i++) {
        hash ^= (u8)stage[i];
        hash *= 0x01000193;
    }
    return hash ? hash : 1;
}

static void resetRemotePlayers() {
    for(u32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        remotePlayers[i].active = false;
        remotePlayers[i].staleFrames = 0;
        alignmentStates[i].isInit = false;
        info.slotOwner[i] = NO_OWNER;
        // Whatever is still buffered is from before the reset; only a packet
        // that arrives from now on may bring the slot back.
        remotePlayers[i].lastRxCount = info.rxCount[i];
    }
}

void onStageInit() {
    PlayerColors::onStageInit();
    resetRemotePlayers();
    ProgressSync::requestFullReplay();
}

// Reconnect when the server goes quiet, and start clean on every new session
static void updateSession() {
    const u32 serverRxCount = info.serverRxCount;
    if(serverRxCount != lastServerRxCount) {
        lastServerRxCount = serverRxCount;
        serverSilentFrames = 0;
    }
    else if(connected && ++serverSilentFrames > SERVER_TIMEOUT_FRAMES) {
        // The server restarted or dropped us. Our id is no longer valid.
        serverSilentFrames = 0;
        connected = false;
        queryTimer = 0;
        Timestamps::beacon.reset();
        resetRemotePlayers();
    }

    const u32 sessionCount = info.sessionCount;
    if(sessionCount != lastSessionCount) {
        lastSessionCount = sessionCount;
        serverSilentFrames = 0;
        resetRemotePlayers();
        ProgressSync::onSessionStart(info.serverEpoch);
    }
}

static void updateStage() {
    u8 scenario;
    const u32 stageHash = getCurrentStage(scenario);
    if(stageHash != info.stageHash || scenario != info.scenario) {
        // Close the gate first so nothing from the old stage slips in
        info.stageHash = 0;
        info.scenario = scenario;
        resetRemotePlayers();
        info.stageHash = stageHash;
        if(stageHash) ProgressSync::onStageChanged(stageHash);
    }
}

// Decide who is shown this frame. Owning this on the game thread (instead of a
// flag the network callbacks set) keeps every user within a frame consistent.
static void updateActivity() {
    for(u32 i = 0; i < MAX_REMOTE_PLAYERS; i++) {
        const u32 rxCount = info.rxCount[i];
        if(rxCount != remotePlayers[i].lastRxCount) {
            remotePlayers[i].lastRxCount = rxCount;
            remotePlayers[i].staleFrames = 0;
            if(!remotePlayers[i].active) {
                remotePlayers[i].active = true;
                remotePlayers[i].generation++;
                alignmentStates[i].isInit = false;
            }
        }
        else if(info.slotOwner[i] != NO_OWNER && ++remotePlayers[i].staleFrames > PLAYER_TIMEOUT_FRAMES) {
            remotePlayers[i].active = false;
            remotePlayers[i].staleFrames = 0;
            alignmentStates[i].isInit = false;
            info.slotOwner[i] = NO_OWNER; // free the slot for someone else
        }
    }
}

static u8 packWeight(f32 weight) {
    if(weight <= 0.0f) return 0;
    if(weight >= 1.0f) return 255;
    return (u8)(s32)(weight * 255.0f + 0.5f);
}

static s32 getAnimationIdx(const XanimePlayer &xanime, const XanimeGroupInfo *anim) {
    s32 diff = anim - xanime.mResourceTable->_10;
    return diff < 0x134 && diff >= 0 ? diff : -1;
}

// Call this every frame
static void updatePackets(MarioActor *mario) {
    if(connected) Timestamps::beacon.update(transmitter); // needs some space from transmitter.update() to avoid lock contention
    mario->control2();
    if(initialized) {
        setDebugMsg(0, 0xFE);
        Timestamps::updateDolphinTime();

        updateSession();
        updateStage();
        updateActivity();
        ProgressSync::update();

        if(queryTimer > 0) queryTimer--;
        else {
            queryTimer = queryCooldown;
            sendDebugMsg();
            if(!connected) {
                Packets::Connect connect;
                connect.major = MAJOR;
                connect.minor = MINOR;
                setDebugMsg(2, transmitter.addPacket(connect).err);
            }
        }

        if(connected) {
            Packets::PlayerPosition pos;
            pos.playerId = Packets::PlayerPosition::consoleId;
            pos.color = localColor;
            pos.stageHash = info.stageHash;
            pos.scenario = info.scenario;
            pos.position = mario->mPosition;
            pos.velocity = mario->mVelocity * Timestamps::realtimeRate;
            //pos.direction = mario->mRotation;
            const Mtx &baseMtx = mario->getJ3DModel()->_24;
            TVec3f X(baseMtx[0][0], baseMtx[1][0], baseMtx[2][0]);
            f32 magx = PSVECMag(X.toCVec());

            f32 r = 1 / magx;
            pos.direction.x = X.x * r;
            pos.direction.y = X.y * r;
            if(X.z < 0.0f) pos.direction.x += 3.0f;
            TVec3f v(0.0f, 0.0f, 0.0f), u, y(baseMtx[0][1], baseMtx[1][1], baseMtx[2][1]);
            if(X.x < 0.99f && X.x > -0.99f) {
                v.z = X.y;
                v.y = -X.z;
                v.setLength(1);
                pos.direction.z = (v.z * baseMtx[2][1] + v.y * baseMtx[1][1]) * r;
            }
            else {
                v.x = X.z;
                v.z = -X.x;
                v.setLength(1);
                pos.direction.z = (v.x * baseMtx[0][1] + v.z * baseMtx[2][1]) * r;
            }
            PSVECCrossProduct(v.toCVec(), X.toCVec(), u.toVec());
            if(y.dot(u) < 0.0f) pos.direction.z += 3.0f;

            const MarioAnimator &animator = *mario->mMarioAnim;
            const XanimePlayer &xanime = *animator.mXanimePlayer;
            pos.currAnmIdx = getAnimationIdx(xanime, xanime.mCurrentAnimation);
            pos.defaultAnmIdx = getAnimationIdx(xanime, xanime.mDefaultAnimation);

            pos.anmSpeed = xanime._20->mSpeed;

            // MarioAnimator keeps the walk/swim blend weights at _18.._24
            pos.trackWeights[0] = packWeight(animator._18);
            pos.trackWeights[1] = packWeight(animator._1C);
            pos.trackWeights[2] = packWeight(animator._20);
            pos.trackWeights[3] = packWeight(animator._24);

            if(MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) {
                pos.stateFlags |= Packets::PlayerPosition::O_STATE_HIPDROP;
            }

            pos.timestamp = Timestamps::beacon.isInit() ?
                Timestamps::beacon.convertToServer(Timestamps::now())
                : Timestamps::makeEmptyServerTimestamp();


            setDebugMsg(2, transmitter.addPacket(pos).err);
        }

        transmitter.update();
    }
}


kmCall(&control__10MarioActorFv + 0x100, updatePackets);

bool MultiplayerAccess::isPlayerActive(u32 i) const {
    return remotePlayers[i].active;
}

u32 MultiplayerAccess::getPlayerGeneration(u32 i) const {
    return remotePlayers[i].generation;
}

const Packets::PlayerPosition& MultiplayerAccess::getPlayerPosRaw(u32 i) {
    u32 buffIdx = getMostRecentBuffer(i, info.status);
    PlayerDoubleBuffer &doubleBuffer = info.players[i];

    if(simplelock_tryLockLoop(&doubleBuffer.locks[buffIdx]) != TRY_LOCK_RESULT_OK) {
        buffIdx = buffIdx == 1 ? 0 : 1;
        if(simplelock_tryLockLoop(&doubleBuffer.locks[buffIdx]) != TRY_LOCK_RESULT_OK) {
            return pos[i];
        }
    }

    pos[i] = doubleBuffer.pos[buffIdx];

    simplelock_release(&doubleBuffer.locks[buffIdx]);

    return pos[i];
}

bool MultiplayerAccess::isPlayerPosEstimateSet(u32 i) const {
    return alignmentStates[i].isInit;
}

void MultiplayerAccess::setPlayerPosEstimate(u32 i) const {
    alignmentStates[i].isInit = true;
}

AlignmentState& MultiplayerAccess::getPlayerPosEstimate(u32 i) const {
    return alignmentStates[i].state;
}

}

