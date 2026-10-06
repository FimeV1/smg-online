#pragma once

#include <revolution.h>

// Shares the star bit counter of a level between the players who are in that
// level together. The game keeps this counter per level (it is added to the
// banked total when a star is collected), separate from the banked total that
// progressSync shares.
//
// Every player reports the net change they caused themselves since the level
// loaded (pickups minus star bits shot). Everyone else adds the difference to
// the last report from that player to their own counter. Reports are absolute,
// so a lost packet is repaired by the next one. They travel in the star bit
// packet (see packets/starPiece.hpp), which the server already relays to the
// players in the same level.
namespace StageStarBits {
    // Game thread
    void onStageInit();     // a level (re)loaded
    void onStageChanged();  // the player is somewhere else now
    void update();          // once per real frame

    // Network thread
    void onNetReport(u8 senderGlobalId, u8 generation, s16 count);
}
