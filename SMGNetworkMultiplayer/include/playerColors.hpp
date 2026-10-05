#ifndef PLAYERCOLORS_HPP
#define PLAYERCOLORS_HPP

#include <revolution/types.h>

class J3DModel;

namespace PlayerColors {

    // 0 = the character's normal clothes, 1..NUM_COLORS-1 = recolours
    const u32 NUM_COLORS = 8;

    // Make the clothes of the player model drawn next use colour `color`.
    // With keepGameOverride, a clothes change the game itself has active
    // (a power-up) is left alone.
    void begin(J3DModel *model, u8 color, bool keepGameOverride = false);
    // Back to the normal clothes. Safe to call when nothing was begun.
    void end();
    // Call while a stage is being set up (allocates from the stage heap).
    void onStageInit();

}

#endif
