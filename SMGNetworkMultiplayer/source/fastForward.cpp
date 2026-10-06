#include "fastForward.hpp"

#include <revolution.h>
#include <kamek/hooks.h>
#include <Game/Scene/SceneObjHolder.hpp>

class GameSystemSceneController;
class MoviePlayingSequence;

namespace MR {
    bool isDemoActive();
    bool testCorePadButtonDown(long);
    bool testCorePadTriggerDown(long);
    bool isActiveMoviePlayer();
    void stopMoviePlayer();
    void forceCloseWipeFade();
}

extern kmSymbol updateSceneController__10GameSystemFv;
extern kmSymbol updateScene__25GameSystemSceneControllerFv;
extern kmSymbol isExistSceneObjHolder__25GameSystemSceneControllerCFv;
extern kmSymbol trySkip__20MoviePlayingSequenceFv;
extern kmSymbol setNerve__11LayoutActorCFPC5Nerve;
extern kmSymbol sInstance__Q223NrvMoviePlayingSequence15HostTypeEndWait;

namespace FastForward {

bool extraUpdate = false;

static const u32 SPEED = 4;

typedef void (*UpdateSceneFn)(GameSystemSceneController *);
typedef bool (*IsExistHolderFn)(const GameSystemSceneController *);

static bool isWanted(GameSystemSceneController *controller) {
    // No scene objects while a stage is loading
    if(!((IsExistHolderFn)&isExistSceneObjHolder__25GameSystemSceneControllerCFv)(controller)) return false;
    if(!MR::isExistSceneObj(SceneObj_DemoDirector)) return false;
    return MR::isDemoActive() && MR::testCorePadButtonDown(0);
}

// Replaces the one call to GameSystemSceneController::updateScene in
// GameSystem::updateSceneController.
static void updateScene(GameSystemSceneController *controller) {
    const UpdateSceneFn update = (UpdateSceneFn)&updateScene__25GameSystemSceneControllerFv;
    update(controller);

    // Checked again before every extra update: the cutscene may have ended
    for(u32 i = 1; i < SPEED && isWanted(controller); i++) {
        extraUpdate = true;
        update(controller);
        extraUpdate = false;
    }
}

// Replaces MoviePlayingSequence::trySkip, which the game calls every frame of
// a pre-rendered movie but only honours for the final battle movie. Pressing
// Down ends any movie the way the game's own skip does: stop the player,
// close the wipe and go to the sequence's normal ending step, which then
// carries on to whatever follows the movie.
static bool trySkipMovie(MoviePlayingSequence *sequence) {
    if(!MR::isActiveMoviePlayer()) return false;
    if(!MR::testCorePadTriggerDown(0)) return false;

    MR::stopMoviePlayer();
    MR::forceCloseWipeFade();
    typedef void (*SetNerveFn)(const MoviePlayingSequence *, const void *);
    ((SetNerveFn)&setNerve__11LayoutActorCFPC5Nerve)(
        sequence, &sInstance__Q223NrvMoviePlayingSequence15HostTypeEndWait);
    return true;
}

}

kmCall(&updateSceneController__10GameSystemFv + 0xE0, FastForward::updateScene);
kmBranch(&trySkip__20MoviePlayingSequenceFv, FastForward::trySkipMovie);
