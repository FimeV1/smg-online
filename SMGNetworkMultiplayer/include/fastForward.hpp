#pragma once

// Cutscene fast-forward: while the player holds Down on the +Control Pad during a cutscene or
// a talk, the scene is updated several times per frame, so it plays at several
// times normal speed. Nothing is skipped, so everything the cutscene does
// (unlocks, save flags) still happens.
// Pre-rendered movies cannot be sped up; pressing Down skips them instead.
namespace FastForward {
    // True during the extra scene updates of a frame. Code that must run once
    // per real frame (the netcode and its timers) checks this.
    extern bool extraUpdate;
}
