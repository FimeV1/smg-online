#ifndef PROGRESSSYNC_HPP
#define PROGRESSSYNC_HPP

#include "packets/gameProgress.hpp"
#include "concurrencyUtils.hpp"

// Shared save-progression sync.
//
// The GameDataFunction:: progress setters are thin wrappers over GameDataHolder::
// methods. We replace each wrapper's entry (kmBranch) with our own version that
// performs the original GameDataHolder call and then reports the change to the
// server. The server keeps every change in an ordered log and sends that log
// to every client; received events are applied on the game thread by calling
// the GameDataHolder method directly, which both updates the save and avoids
// re-triggering the hooks.
//
// Both directions are reliable over UDP:
//   * our events wait in a queue and are re-sent until the server acks them
//   * log entries are accepted strictly in order and we tell the server which
//     index we expect next, so it re-sends whatever went missing
namespace ProgressSync {

    // Set up the queues. Call once during multiplayer init.
    void init();

    // Apply received events, (re)send ours, and ack. Call every frame on the
    // game thread.
    void update();

    // Ask for the whole log again (a save file may have just been loaded).
    // Game thread only.
    void requestFullReplay();

    // The local player entered another stage (0 = none). Game thread only.
    void onStageChanged(u32 stageHash);

    // A handshake completed. `epoch` identifies the server's log. Game thread only.
    void onSessionStart(u32 epoch);

    // BOTH OF THESE ARE CALLED FROM AN UNTHREADED CONTEXT
    void onServerAck(u32 seq);
    void onNetEvent(const Packets::GameProgress &);

}

#endif
