#include <Game/Player/MarioActor.hpp>
#include <Game/Player/MarioAnimator.hpp>
#include <Game/Player/J3DModelX.hpp>
#include <Game/Util/CameraUtil.hpp>
#include <Game/LiveActor/ClippingJudge.hpp>
#include <JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp>
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <kamek/hooks.h>
#include <revolution/os.h> // OSReport (diagnostic)

#include "multiplayer.hpp"
#include "playerColors.hpp"
#include "beacon.hpp"
#include "debug.hpp"
#include "accurateTime.hpp"
#include "alignment.hpp"

//static const BASE_INTERPOLATE_EPSILON = 0.1f;

const static f32 CORRECTION_ACCEL = 10.0f;
const static f32 CORRECTION_MAX_V = 15.0f;
const static f32 CORRECTION_S_EPSILON = 20.0f;
const static f32 CORRECTION_TIMEOUT_FRAMES = 2.0f * 60.0f;

J3DMtxBuffer playerBuffs[Multiplayer::MAX_REMOTE_PLAYERS];
Mtx playerBaseMtx[Multiplayer::MAX_REMOTE_PLAYERS];

// Smoothed render position per remote player. Replaces the extrapolating homing
// predictor (which overshot/slingshotted): exponential smoothing toward the last
// received position, snapping on large jumps (teleports / galaxy changes).
static TVec3f g_smoothPos[Multiplayer::MAX_REMOTE_PLAYERS];
static bool g_smoothInit[Multiplayer::MAX_REMOTE_PLAYERS];
const static f32 SMOOTH_FACTOR = 0.35f; // fraction of the gap closed each frame
const static f32 SMOOTH_SNAP_DIST = 500.0f; // beyond this, snap instead of lerp

struct XanimeWrapper {u32 raw[sizeof(XanimePlayer) / sizeof(u32)];};
static XanimeWrapper xanimeWrapper[Multiplayer::MAX_REMOTE_PLAYERS];
static XanimeWrapper xanimeUpperWrapper[Multiplayer::MAX_REMOTE_PLAYERS];

static XanimePlayer *playerXanimes = (XanimePlayer *)xanimeWrapper;

// Per-slot render state that has to start over whenever the slot starts
// showing a different player (see MultiplayerAccess::getPlayerGeneration)
static u32 g_generation[Multiplayer::MAX_REMOTE_PLAYERS];
static const s32 NO_ANIMATION = -2; // -1 is what the wire uses for "unknown"
static s32 g_lastAnmIdx[Multiplayer::MAX_REMOTE_PLAYERS];
static u8 g_color[Multiplayer::MAX_REMOTE_PLAYERS];
static XanimePlayer *playerUpperXanimes = (XanimePlayer *)xanimeUpperWrapper;

const TVec3f* Multiplayer::getPlayerRenderPos(u32 i) {
    // A stale position from the slot's previous player does not count
    if(!g_smoothInit[i] || g_generation[i] != Multiplayer::access.getPlayerGeneration(i)) return nullptr;
    return &g_smoothPos[i];
}

void createMtxBuffers(J3DModelData *data, const J3DMtxBuffer *basis, J3DMtxBuffer *dstArray, u32 numBuffs) {
    for(u32 i = 0; i < numBuffs; i++) {
        memcpy(dstArray + i, basis, sizeof(*basis));
        dstArray[i].createDoubleDrawMtx(data, 1);
    }
}

J3DModelX* createMtxBuffers_ep(MarioActor *self) {
    J3DModelX *model = (J3DModelX*) MR::getJ3DModel(self);
    createMtxBuffers(model->mModelData, model->_84, playerBuffs, Multiplayer::MAX_REMOTE_PLAYERS); // inconsistent

    return model;
}

extern kmSymbol initDrawAndModel__10MarioActorFv;
// Replaces `LiveActor::getJ3DModel` immediately after `LiveActor::initModelManagerWithAnm`
kmCall(&initDrawAndModel__10MarioActorFv + 0x214, createMtxBuffers_ep);

void createXanimes(MarioAnimator *anim) {
    anim->init();

    for(u32 i = 0; i < Multiplayer::MAX_REMOTE_PLAYERS; i++) {
        playerXanimes[i] = XanimePlayer(MR::getJ3DModel(anim->mActor), anim->mResourceTable);
        playerXanimes[i].changeAnimation("\x97\x8e\x89\xba"); 
        playerXanimes[i].setDefaultAnimation("\x97\x8e\x89\xba");
        playerXanimes[i].mCore->enableJointTransform(MR::getJ3DModel(anim->mActor)->mModelData);
        g_lastAnmIdx[i] = NO_ANIMATION; // the animation players are new: re-apply

//        playerUpperXanimes[i] = XanimePlayer(MR::getJ3DModel(anim->mActor), anim->mResourceTable);
//        playerUpperXanimes[i].mCore->enableJointTransform(MR::getJ3DModel(anim->mActor)->mModelData);
    }
}
extern kmSymbol __ct__13MarioAnimatorFP10MarioActor;
kmCall(&__ct__13MarioAnimatorFP10MarioActor + 0x24, createXanimes);

static bool isPlayerClipped[Multiplayer::MAX_REMOTE_PLAYERS];

void calcAnim(MarioAnimator *anim, J3DModel *model, const Mtx *base, J3DMtxBuffer *buffs, u32 numBuffs) {
    j3dSys.mCurrentModel = model;
    for(u32 i = 0; i < numBuffs; i++) {
        
        if(!Multiplayer::access.isPlayerActive(i)) continue;

        if(MR::getClippingJudge()->isJudgedToClipFrustum (
            g_smoothPos[i], 60.0f, 6
        ) ) { // See LiveActor/ClippingJudge.cpp for final argument
            isPlayerClipped[i] = true;
            continue;
        }
        isPlayerClipped[i] = false;

        PSMTXCopy(base[i], model->_24); // inefficient!
        model->_84 = buffs + i;
        
        XanimePlayer *currXanime = &playerXanimes[i];
        if(currXanime->mModel != model) currXanime->setModel(model);
        currXanime->updateBeforeMovement();
        currXanime->updateAfterMovement();
        u32 idx = MR::getJointIndex(anim->mActor, "Spine1");
        model->mModelData->mJointTree.mJointsByIdx[idx]->mMtxCalc = nullptr;
        currXanime->calcAnm(0);
        /*currXanime->mCore->_6 = 1;

        model->mModelData->mJointTree.calc(buffs + i, *model->_18.toCVec(), model->_24); // maybe can fix issue above?
        model->calcWeightEnvelopeMtx();
        buffs[i].calcNrmMtx();
        buffs[i].calcDrawMtx(model->_8 & 3, *model->_18.toCVec(), model->_24);*/
        currXanime->mCore->_6 = 3;
        model->mModelData->mJointTree.calc(buffs + i, *model->_18.toCVec(), model->_24); // maybe can fix issue above?
        model->calcWeightEnvelopeMtx();
        buffs[i].calcNrmMtx();
        buffs[i].calcDrawMtx(model->_8 & 3, *model->_18.toCVec(), model->_24);
        DCStoreRangeNoSync(buffs[i].mpDrawMtxArr[1][buffs[i].mCurrentViewNo], model->mModelData->mJointTree.mMatrixData.mDrawMatrixCount * sizeof(Mtx));
        DCStoreRange(buffs[i].mpNrmMtxArr[1][buffs[i].mCurrentViewNo], model->mModelData->mJointTree.mMatrixData.mDrawMatrixCount * sizeof(Mtx33));
        currXanime->clearAnm(0);
    }
}

XanimeGroupInfo* getGroupInfoFromIdx(const XanimePlayer &xanime, s32 idx) {
    return idx >= 0 ? xanime.mResourceTable->_10 + idx : nullptr;
}

/*
class VectorInterpolation {
    TVec3f curr, step;
public:
    VectorInterpolation(TVec3f curr, TVec3f end, u32 time) : curr(curr), step((end - curr) / time) {}
    void update(f32 dt) {
        curr += step * dt;
    }
    TVec3f get() const {return curr;}
};*/

/*class VectorInterpolation {
    f32 currMag;
    f32 endMag;
    f32 step;
    TVec3f currDir;
    TVec3f endDir;

    f32 dirStep;
    TVec3f endDirStepMag;

    f32 magEpsilon;
    f32 dirEpsilon;

public:
    // t is in iterations
    VectorInterpolation(const TVec3f &start, const TVec3f &end, f32 t) 
        : currMag(PSVecMag(start.toCVec())), endMag(PSVecMag(end.toCVec())), 
        step((endMag - currMag) / t), currDir(start * (1.0f / currMag))
    {
        endDir = end * (1.0f / endMag);
        dirStepMag = acos(endDir.dot(currDir)) * PI;
        endDirStepMag = dirStepMag;
        
        magEpsilon = step * BASE_INTERPOLATE_EPSILON;
        dirEpsilon = dirStepMag * BASE_INTERPOLATE_EPSILON;
    }
    void update() {
        if(MR::isNearZero(currMag - endMag, magEpsilon)) currMag += step;
        if(currDir.epsilonEquals(endDir, dirEpsilon)) {
            TVec3f stepDir;
            JMAVECScaleAdd(endDirStepMag.toCVec(), currDir.toCVec(), stepDir.toVec(), -currDir.dot(endDirStepMag));
            currDir += stepDir;
            currDir.setLength(1.0f);
        }

    }
    TVec3f get() const {
        return currDir * currMag;
    }
};*/

void calcAnim_ep(MarioAnimator *anim) {
    J3DModel *model = anim->mActor->getJ3DModel();

    for(u32 i = 0; i < Multiplayer::MAX_REMOTE_PLAYERS; i++) {
        if(!Multiplayer::access.isPlayerActive(i)) continue;

        const Packets::PlayerPosition &pos = Multiplayer::access.getPlayerPosRaw(i);

        AlignmentState &posEstimate = Multiplayer::access.getPlayerPosEstimate(i);

        if(!Multiplayer::access.isPlayerPosEstimateSet(i)) {
            
            posEstimate = AlignmentState (
                pos.position, 
                pos.velocity, 
                Timestamps::beacon.isInit() && !Timestamps::isEmpty(pos.timestamp) ? 
                    Timestamps::beacon.convertToLocal(pos.timestamp) 
                    : pos.arrivalTime
            );

            Multiplayer::access.setPlayerPosEstimate(i);
        }
        else {
            posEstimate.invalidateAlignmentPlan(AlignmentState::HomingAlignmentPlan(pos, CORRECTION_ACCEL, CORRECTION_MAX_V, CORRECTION_TIMEOUT_FRAMES, CORRECTION_S_EPSILON));
            posEstimate.update(Timestamps::now());
        }
        
        Mtx &baseMtx = playerBaseMtx[i];

        const u32 generation = Multiplayer::access.getPlayerGeneration(i);
        if(generation != g_generation[i]) {
            g_generation[i] = generation;
            g_smoothInit[i] = false;
            g_lastAnmIdx[i] = NO_ANIMATION;
        }

        // Exponential smoothing toward the received position (no extrapolation -> no slingshot).
        TVec3f &sp = g_smoothPos[i];
        if(!g_smoothInit[i]) {
            sp = pos.position;
            g_smoothInit[i] = true;
        }
        else {
            TVec3f delta = pos.position - sp;
            f32 d = PSVECMag(delta.toCVec());
            if(d > SMOOTH_SNAP_DIST) sp = pos.position;
            else sp += delta * SMOOTH_FACTOR;
        }
        const TVec3f &res = sp;

        baseMtx[0][3] = res.x;
        baseMtx[1][3] = res.y;
        baseMtx[2][3] = res.z;

        bool isXzNegative = pos.direction.x > 1.5f;
        bool isYOrthoNegative = pos.direction.z > 1.5f;
        TVec3f X(pos.direction.x - (isXzNegative ? 3.0f : 0.0f), pos.direction.y, 0.0f);
        
        f32 tmp = 1.0f - X.x * X.x - X.y * X.y;
        X.z = sqrt(tmp < 0.0f ? 0.0f : tmp);
        if(isXzNegative) X.z = -X.z;

        TVec3f Y;
        TVec3f v(0.0f, 0.0f, 0.0f); // (i|j) x X
        TVec3f u;

        if(X.x < 0.99f && X.x > -0.99f) {
            v.z = X.y;
            v.y = -X.z;
        }
        else {
            v.x = X.z;
            v.z = -X.x;
        }
        f32 z = pos.direction.z - (isYOrthoNegative ? 3.0f : 0.0f);
        PSVECCrossProduct(v.toCVec(), X.toCVec(), u.toVec());
        v.setLength(z);
        tmp = 1 - z*z;
        u.setLength(sqrt(tmp < 0.0f ? 0.0f : tmp));
        if(isYOrthoNegative) u = -u;
        Y = u + v;
        TVec3f Z;
        PSVECCrossProduct(X.toCVec(), Y.toCVec(), Z.toVec());
        
        baseMtx[0][0] = X.x;
        baseMtx[0][1] = Y.x;
        baseMtx[0][2] = Z.x;
        baseMtx[1][0] = X.y;
        baseMtx[1][1] = Y.y;
        baseMtx[1][2] = Z.y;
        baseMtx[2][0] = X.z;
        baseMtx[2][1] = Y.z;
        baseMtx[2][2] = Z.z;

        //setDebugMsgFloat(12, pos.position.y);
        //setDebugMsgFloat(16, pos.position.z);

        // Start an animation once, when the remote player starts it. Doing it
        // every frame would restart one-shot animations as soon as they end.
        XanimeGroupInfo *info = getGroupInfoFromIdx(playerXanimes[i], pos.currAnmIdx);
        setDebugMsg(8, 0);
        if(!info) setDebugMsg(8, 1);
        else if(pos.currAnmIdx != g_lastAnmIdx[i]) playerXanimes[i].changeAnimation(info);
        g_lastAnmIdx[i] = pos.currAnmIdx;

        info = getGroupInfoFromIdx(playerXanimes[i], pos.defaultAnmIdx);
        setDebugMsg(9, 0);
        if(info) playerXanimes[i].mDefaultAnimation = info;
        else setDebugMsg(9, 1);

        playerXanimes[i]._20->mSpeed = pos.anmSpeed;
        g_color[i] = pos.color;

        // Walking, running and swimming are blends of up to 4 tracks; without
        // the weights the puppet stays in the first track's pose.
        for(u32 track = 0; track < 4; track++) {
            playerXanimes[i].changeTrackWeight(track, pos.trackWeights[track] * (1.0f / 255.0f));
        }

    }

    Mtx tmpBaseMtx;
    PSMTXCopy(model->_24, tmpBaseMtx);
    J3DMtxBuffer *tmpMtxBuffer = model->_84;
    
    calcAnim(anim, model, playerBaseMtx, playerBuffs, Multiplayer::MAX_REMOTE_PLAYERS);
    
    PSMTXCopy(tmpBaseMtx, model->_24);
    model->_84 = tmpMtxBuffer;
    
    anim->calc();
}

extern kmSymbol calcAnim__10MarioActorFv;
// Replaces `MarioAnimator::calcAnim`
kmCall(&calcAnim__10MarioActorFv + 0x2A4, calcAnim_ep);

void drawAll(J3DModelX *model, J3DMtxBuffer *buffs, u32 numBuffs) {
    MR::showJoint(model, "Face0");
    MR::showJoint(model, "HandL0");
    MR::showJoint(model, "HandR0");
    for(u32 i = 0; i < numBuffs; i++) {
        if(!Multiplayer::access.isPlayerActive(i) || isPlayerClipped[i]) continue;
        model->_84 = buffs + i;
        PlayerColors::begin(model, g_color[i]);
        model->prepareShapePackets();
        model->directDraw(nullptr);
    }
    PlayerColors::end();
    MR::hideJoint(model, "Face0");
    MR::hideJoint(model, "HandL0");
    MR::hideJoint(model, "HandR0");
}

void drawAll_ep(J3DModelX *model, J3DModel *model2) {
    // The local player wears their own colour too
    PlayerColors::begin(model, Multiplayer::localColor, true);
    model->directDraw(model2);
    PlayerColors::end();
    J3DMtxBuffer *tmp = model->_84;
    //Mtx tmpMtx;
    //PSMTXCopy(model->_24, tmpMtx);
    //calcAnim(model, playerBaseMtx, playerBuffs, 1);

    //PSMTXCopy(tmpMtx, model->_24);
    drawAll(model, playerBuffs, Multiplayer::MAX_REMOTE_PLAYERS);
    model->_84 = tmp;
}


extern kmSymbol drawMarioModel__10MarioActorCFv;
// Replaces `J3DModelX::directDraw`
kmCall(&drawMarioModel__10MarioActorCFv + 0x1B8, drawAll_ep);

void drawModel2(MarioActor *actor) {
//    f32 tmpA, tmpB, tmpC;
//    J3DModelX *model = actor->mModels[actor->mCurrModel];
    J3DModelX *model = (J3DModelX *) actor->getJ3DModel();
    model->directDraw(nullptr);
//    GXDrawDone();

    model->_24[1][3] += 500.0f;
   // actor->mMarioAnim->calc();
   
//    model->calc();
    model->mModelData->mJointTree.calc(model->_84, *model->_18.toCVec(), model->_24);
    model->calcWeightEnvelopeMtx();
//    if(model->_10) model->_10(model, 0);
    
    //MR::updateModelDiffDL(actor);
//    model->viewCalc3(0, nullptr);
//    ((J3DMtxBuffer2 *)model->_84)->rotationMtx((MtxPtr)model->_E0[model->_DC]);
//    model->_84->calcDrawMtx(model->_8 & 3, *model->_18.toCVec(), model->_24);
//    model->directDraw(nullptr);
//    GXDrawDone();
/*    model->_84->swapNrmMtx();
    ((J3DMtxBuffer2*)model->_84)->calcNrmMtx2();
    model->calcBBoardMtx();
    model->calcBumpMtx();
    DCStoreRangeNoSync(model->getDrawMtxPtr(), model->mModelData->mJointTree.mMatrixData.mDrawMatrixCount * 0x30);
    model->prepareShapePackets();*/
//    tmpA = model->_24[0][3];
//    tmpB = model->_24[1][3];
//    tmpC = model->_24[2][3];
//    model->_24[0][3] = 4.0f;
//    model->_24[2][3] = 4.0f;
    //actor->mMarioAnim->calc();
    //MR::updateModelDiffDL(actor);
//    actor->mModelManager->calcAnim();
//    model->update();
//    MR::updateModelDiffDL(actor);
    //model->viewCalc3(0, nullptr);
    //model->directDraw(nullptr);

    model->_24[1][3] -= 500.0f;
//    MR::loadViewMtx();
//    model->setDrawView(0);
//    model->viewCalc();
   // actor->updateModelMtx(actor, nullptr, nullptr);
//    model->directDraw(nullptr);

    
//    model->_24[0][3] = tmpA;
//    model->_24[1][3] = tmpB;
//    model->_24[2][3] = tmpC;

    model->mFlags = 0;
 //   model->_1D0++;

//    model->setDrawView(0);
//    model->directDraw(nullptr);


}

void copyFake(u32, MtxPtr mtx) {
    mtx[0][3] = 0.0f;
    mtx[1][3] = -500.0f;
    mtx[2][3] = 0.0f;
}

