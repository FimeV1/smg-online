#include "progressSync.hpp"
#include "multiplayer.hpp"
#include "globalTransmitter.hpp"
#include "packets/playerPosition.hpp"
#include "packets/ack.hpp"
#include "debug.hpp"

#include <Game/System/GameDataFunction.hpp>
#include <Game/System/GameDataHolder.hpp>
#include <kamek/hooks.h>
#include <revolution/os.h> // OSReport (-> Dolphin OSREPORT log)

namespace ProgressSync {

// ---- Receiving the server's log -------------------------------------------

// The server sends at most 16 log entries per datagram; twice that leaves room
// for a datagram that lands before the game thread has drained the last one.
static ConcurrentQueue<Packets::GameProgress> netGameProgressQueue;
static ConcurrentQueue<Packets::GameProgress>::Block netGameProgressQueueBuffer[32];

// Index of the next log entry we will accept. Advanced by the network
// callbacks, rewound by the game thread; both are single-word writes.
static volatile u32 rxExpected = 0;
static volatile bool needAck = false;
static u16 ackTimer = 0;
static const u16 ACK_INTERVAL_FRAMES = 60;

static u32 knownEpoch = 0;
static bool hasEpoch = false;

// Progress only flows (both ways) once this player has reached the Comet
// Observatory with a save loaded. Before that the "current" save may not be
// the one they will play (title/file select), or they are in the intro, whose
// early story events must not rewind everyone else's story.
// Stage ids are FNV-1a hashes of the stage name (see multiplayer.cpp).
static const u32 STAGE_FILE_SELECT = 1298080883; // "FileSelect" (title + file select)
static const u32 STAGE_OBSERVATORY = 1224986660; // "AstroGalaxy"
static volatile bool g_gateOpen = false;

// True while we are applying a received event, so the setter hooks below do not
// report it back to the server. (Apply calls the GameDataHolder methods directly,
// which already bypass the hooks; this is an extra guard against indirect triggers.)
static bool g_applying = false;

// ---- Sending our own events (game thread only, except ackedSeq) -----------

static const u32 PENDING_CAPACITY = 32;
static Packets::GameProgress pending[PENDING_CAPACITY];
static u32 pendingHead = 0;
static u32 pendingCount = 0;
static u32 nextSeq = 1; // 0 is never used, so `ackedSeq == 0` means "nothing acked"
static volatile u32 ackedSeq = 0;
static u16 resendTimer = 0;
static const u16 RESEND_FRAMES = 20;

void init() {
    netGameProgressQueue.init(
        netGameProgressQueueBuffer,
        sizeof(netGameProgressQueueBuffer) / sizeof(*netGameProgressQueueBuffer)
    );
}

void requestFullReplay() {
    rxExpected = 0;
    needAck = true;
}

void onSessionStart(u32 epoch) {
    if(!hasEpoch || epoch != knownEpoch) {
        // A different log than the one our index refers to
        hasEpoch = true;
        knownEpoch = epoch;
        rxExpected = 0;
    }
    needAck = true;
    resendTimer = 0;
}

void onStageChanged(u32 stageHash) {
    if(stageHash == STAGE_FILE_SELECT) {
        g_gateOpen = false;
    }
    else if(stageHash == STAGE_OBSERVATORY && !g_gateOpen) {
        g_gateOpen = true;
        requestFullReplay();
    }
}

void onServerAck(u32 seq) {
    ackedSeq = seq;
}

void onNetEvent(const Packets::GameProgress &packet) {
    // Not acked either: the server keeps offering it until the gate opens
    if(!g_gateOpen) return;

    // Strictly in order; anything else is answered with the index we still
    // need, which makes the server resend from there.
    if(packet.seq == rxExpected && netGameProgressQueue.write(packet)) {
        rxExpected = packet.seq + 1;
    }
    needAck = true;
}

// The game calls some setters over and over with a value that is already set
// (e.g. "scenario visited" on every stage entry). Remember what was last
// reported per key so repeats do not crowd real changes out of the queue.
static const u32 RECENT_CAPACITY = 16;
static struct RecentEvent {
    u8 type;
    bool used;
    s32 arg;
    s32 value;
    char name[48];
} recent[RECENT_CAPACITY];
static u32 recentNext = 0;

static bool isSameKey(const RecentEvent &event, u8 type, const char *name, s32 arg) {
    if(!event.used || event.type != type || event.arg != arg) return false;
    u32 i = 0;
    if(name) for(; i < sizeof(event.name) - 1 && name[i]; i++) {
        if(event.name[i] != name[i]) return false;
    }
    return event.name[i] == '\0';
}

// Returns true if this exact change was the last one reported for its key
static bool isRepeat(u8 type, const char *name, s32 arg, s32 value) {
    // Newest first, so an on -> off -> on sequence is not mistaken for a repeat
    for(u32 n = 0; n < RECENT_CAPACITY; n++) {
        const RecentEvent &event = recent[(recentNext + RECENT_CAPACITY - 1 - n) % RECENT_CAPACITY];
        if(isSameKey(event, type, name, arg)) return event.value == value;
    }
    return false;
}

static void remember(u8 type, const char *name, s32 arg, s32 value) {
    RecentEvent &event = recent[recentNext];
    recentNext = (recentNext + 1) % RECENT_CAPACITY;
    event.used = true;
    event.type = type;
    event.arg = arg;
    event.value = value;
    u32 i = 0;
    if(name) for(; i < sizeof(event.name) - 1 && name[i]; i++) event.name[i] = name[i];
    event.name[i] = '\0';
}

static void queueEvent(u8 type, const char *name, s32 arg, s32 value) {
    if(g_applying || !g_gateOpen) return;
    if(isRepeat(type, name, arg, value)) return;

    if(pendingCount == PENDING_CAPACITY) {
        OSReport("[MP] progress queue full, dropped type=%d\n", (int)type);
        return;
    }

    u32 idx = pendingHead + pendingCount;
    if(idx >= PENDING_CAPACITY) idx -= PENDING_CAPACITY;
    Packets::GameProgress &packet = pending[idx];

    packet.eventType = type;
    packet.seq = nextSeq++;
    if(nextSeq == 0) nextSeq = 1;
    packet.arg = arg;
    packet.value = value;

    u32 i = 0;
    if(name) for(; i < sizeof(packet.name) - 1 && name[i]; i++) packet.name[i] = name[i];
    for(; i < sizeof(packet.name); i++) packet.name[i] = '\0';

    pendingCount++;
    remember(type, name, arg, value);
}

static void applyEvent(const Packets::GameProgress &p) {
    GameDataHolder *holder = GameDataFunction::getCurrentGameDataHolder();
    if(!holder) return;

    g_applying = true;
    switch(p.eventType) {
        case Packets::PE_POWER_STAR:
            holder->setPowerStar(p.name, p.arg, p.value != 0);
            break;
        case Packets::PE_SCENARIO_VISITED:
            holder->onGalaxyScenarioFlagAlreadyVisited(p.name, p.arg);
            break;
        case Packets::PE_GAME_EVENT_BIT:
            holder->setGameEventValueForBit(p.name, (int)p.arg, p.value != 0);
            break;
        case Packets::PE_STORY_EVENT:
            holder->followStoryEventByName(p.name);
            break;
        case Packets::PE_TICO_SEED:
        {
            // `value` is the total fed so far; top ours up to it
            int have = holder->getStarPieceNumGivingToTicoSeed((int)p.arg);
            if(p.value > have) holder->addStarPieceGivingToTicoSeed((int)p.arg, (int)p.value - have);
            break;
        }
        case Packets::PE_GAME_EVENT:
            // Goes through our tryOnGameEventFlag hook, but g_applying suppresses
            // the report.
            holder->tryOnGameEventFlag(p.name);
            break;
        case Packets::PE_GAME_EVENT_VALUE:
            holder->setGameEventValue(p.name, (u16)p.value);
            break;
    }
    g_applying = false;

    // This is now the last known value for the key; a local change away from
    // it and back must not look like a repeat.
    remember(p.eventType, p.name, p.arg, p.value);
}

static void applyPending() {
    const Packets::GameProgress *p;
    while((p = netGameProgressQueue.read())) {
        // Left over from before the gate closed: drop it, it is replayed later
        if(g_gateOpen) applyEvent(*p);
        netGameProgressQueue.advance();
    }
}

void update() {
    applyPending();

    if(pendingCount && ackedSeq == pending[pendingHead].seq) {
        pendingHead++;
        if(pendingHead == PENDING_CAPACITY) pendingHead = 0;
        pendingCount--;
        resendTimer = 0;
    }

    if(!Multiplayer::connected) return;

    if(pendingCount) {
        if(resendTimer > 0) resendTimer--;
        else {
            Packets::GameProgress &packet = pending[pendingHead];
            // Our id can change between queueing and sending (reconnects)
            packet.playerId = Packets::PlayerPosition::consoleId;
            if(Multiplayer::transmitter.addPacket(packet).err == NetReturn::OK) {
                resendTimer = RESEND_FRAMES;
            }
        }
    }

    if(needAck || ++ackTimer >= ACK_INTERVAL_FRAMES) {
        needAck = false;
        ackTimer = 0;
        Packets::Ack ack;
        ack.seqNum = rxExpected;
        if(Multiplayer::transmitter.addPacket(ack).err != NetReturn::OK) needAck = true;
    }
}

// ---- Detect hooks: replace each GameDataFunction:: setter entry. The replacement
// performs the original work (via the GameDataHolder method) then reports it. The
// kmBranch is a `b`, so lr is preserved and we return to the original caller. ----

static void hookSetPowerStar(const char *galaxy, s32 scenario, bool isOwned) {
    GameDataFunction::getCurrentGameDataHolder()->setPowerStar(galaxy, scenario, isOwned);
    queueEvent(Packets::PE_POWER_STAR, galaxy, scenario, isOwned);
}

static void hookScenarioVisited(const char *galaxy, s32 scenario) {
    GameDataFunction::getCurrentGameDataHolder()->onGalaxyScenarioFlagAlreadyVisited(galaxy, scenario);
    queueEvent(Packets::PE_SCENARIO_VISITED, galaxy, scenario, 0);
}

static void hookGameEventBit(const char *name, int index, bool on) {
    GameDataFunction::getCurrentGameDataHolder()->setGameEventValueForBit(name, index, on);
    queueEvent(Packets::PE_GAME_EVENT_BIT, name, index, on);
}

static bool isSameString(const char *a, const char *b) {
    while(*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

// Event values gate progression: e.g. some galaxies appear only after N more
// Power Stars, counted down here each time a star is collected.
static void hookGameEventValue(const char *name, u16 value) {
    GameDataFunction::getCurrentGameDataHolder()->setGameEventValue(name, value);
    // The mail LED blink is this console's business only
    if(!isSameString(name, "MsgLedPattern")) queueEvent(Packets::PE_GAME_EVENT_VALUE, name, 0, value);
}

static void hookStoryEvent(const char *name) {
    GameDataFunction::getCurrentGameDataHolder()->followStoryEventByName(name);
    queueEvent(Packets::PE_STORY_EVENT, name, 0, 0);
}

static void hookTicoSeed(int ticoId, int amount) {
    GameDataHolder *holder = GameDataFunction::getCurrentGameDataHolder();
    holder->addStarPieceGivingToTicoSeed(ticoId, amount);
    // Report the running total rather than the amount: totals can be replayed
    // and merged without double counting.
    queueEvent(Packets::PE_TICO_SEED, nullptr, ticoId, holder->getStarPieceNumGivingToTicoSeed(ticoId));
}

// Replaces GameDataHolder::tryOnGameEventFlag (the inlined onGameEventFlag wrapper
// funnels here). `self` arrives in r3, name in r4. We reproduce the original body
// (mEventFlagChecker->tryOn) directly so there is no recursion, and report only
// when a flag actually flips off->on -- this is what unlocks galaxies/domes.
static void hookTryOnGameEvent(GameDataHolder *self, const char *name) {
    GameEventFlagChecker *checker = self->mEventFlagChecker;
    bool wasOn = checker->isOn(name);
    checker->tryOn(name);
    if(!wasOn && checker->isOn(name))
        queueEvent(Packets::PE_GAME_EVENT, name, 0, 0);
}

}

extern kmSymbol setGameFlagPowerStarSuccess__16GameDataFunctionFPCclb;
extern kmSymbol onGalaxyScenarioFlagAlreadyVisited__16GameDataFunctionFPCcl;
extern kmSymbol setGameEventValueForBit__16GameDataFunctionFPCcib;
extern kmSymbol followStoryEventByName__16GameDataFunctionFPCc;
extern kmSymbol addStarPieceGivingToTicoSeed__16GameDataFunctionFii;
extern kmSymbol tryOnGameEventFlag__14GameDataHolderFPCc;
extern kmSymbol setGameEventValue__16GameDataFunctionFPCcUs;

kmBranch(&setGameFlagPowerStarSuccess__16GameDataFunctionFPCclb, ProgressSync::hookSetPowerStar);
kmBranch(&onGalaxyScenarioFlagAlreadyVisited__16GameDataFunctionFPCcl, ProgressSync::hookScenarioVisited);
kmBranch(&setGameEventValueForBit__16GameDataFunctionFPCcib, ProgressSync::hookGameEventBit);
kmBranch(&followStoryEventByName__16GameDataFunctionFPCc, ProgressSync::hookStoryEvent);
kmBranch(&addStarPieceGivingToTicoSeed__16GameDataFunctionFii, ProgressSync::hookTicoSeed);
kmBranch(&tryOnGameEventFlag__14GameDataHolderFPCc, ProgressSync::hookTryOnGameEvent);
kmBranch(&setGameEventValue__16GameDataFunctionFPCcUs, ProgressSync::hookGameEventValue);
