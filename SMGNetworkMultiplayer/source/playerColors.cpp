#include "playerColors.hpp"

#include <JSystem/JGeometry/TVec.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <revolution/os/OSCache.h>
#include <cstring>
#include <JSystem/JKernel/JKRHeap.hpp>

// Player colours.
//
// Mario's shirt/overalls and cap each have their own material and texture,
// separate from skin, hair and gloves. For every colour we keep a recoloured
// copy of those two textures. Mario's model class (J3DModelX) has a per-material
// "extra display list" slot that the game itself uses to swap the clothes
// texture for power-ups; while a player with a colour is being drawn, that slot
// points at a tiny list selecting the recoloured texture. The game's own
// textures and lists are never modified.

namespace PlayerColors {

struct Rgb { u8 r, g, b; };

// What a colour does to the shirt + cap (primary) and the overalls (secondary).
// `keep` leaves that part as it is.
struct Scheme {
    Rgb primary;
    bool keepPrimary;
    Rgb secondary;
    bool keepSecondary;
};

// Index 0 (unchanged) is not stored.
static const Scheme g_schemes[NUM_COLORS - 1] = {
    {{ 40, 200,  70}, false, {  0,   0,   0}, true },   // 1 green shirt, blue overalls
    {{255, 215,   0}, false, {140,  60, 200}, false},   // 2 yellow shirt, purple overalls
    {{150,  70, 225}, false, { 45,  45,  60}, false},   // 3 purple shirt, black overalls
    {{ 60, 130, 255}, false, {215,  40,  40}, false},   // 4 blue shirt, red overalls
    {{240, 240, 240}, false, {215,  40,  40}, false},   // 5 white shirt, red overalls
    {{255, 135,  25}, false, {  0, 150, 150}, false},   // 6 orange shirt, teal overalls
    {{255, 115, 185}, false, {225, 225, 240}, false},   // 7 pink shirt, white overalls
};

static const u32 BODY_SIZE = 256 * 128 / 2; // CMPR is 4 bits per pixel
static const u32 CAP_SIZE = 128 * 64 / 2;

// Per colour: body texture, cap texture, body display list, cap display list.
// About 160 KB in total, taken from the stage's heap when the stage loads (it
// goes away with the stage). It must not live in the mod's own memory: that
// comes out of a small heap the game needs at boot, and the game crashed
// creating its threads when it was there.
static const u32 OVERRIDE_SIZE = 32;
static const u32 PER_COLOR = BODY_SIZE + CAP_SIZE + 2 * OVERRIDE_SIZE;
static u8 *g_buffer = nullptr;

static u8* bodyTex(u32 c) { return g_buffer + c * PER_COLOR; }
static u8* capTex(u32 c) { return bodyTex(c) + BODY_SIZE; }
static u8* bodyOverride(u32 c) { return capTex(c) + CAP_SIZE; }
static u8* capOverride(u32 c) { return bodyOverride(c) + OVERRIDE_SIZE; }

// J3DDisplayListObj: void* mpDisplayList[2]; u32 mSize; u32 mMaxSize;
struct DisplayListObj {
    u8 *list[2];
    u32 size;
    u32 maxSize;
};

// ---- recolouring ---------------------------------------------------------

static u8 mix(u8 low, u8 range, u8 target) {
    return (u8)(low + (u32)range * target / 255);
}

// Shirt, cap and overalls are strongly saturated; shoes, buttons, the cap
// emblem and shading are not, or have another hue, and are left alone.
static u16 recolor(u16 color, const Scheme &scheme) {
    u32 r = (color >> 11) * 255 / 31;
    u32 g = ((color >> 5) & 63) * 255 / 63;
    u32 b = (color & 31) * 255 / 31;

    u32 high = r > g ? r : g; if(b > high) high = b;
    u32 low = r < g ? r : g; if(b < low) low = b;
    if(high - low < 50) return color;

    const Rgb *target = nullptr;
    if(b == high && r * 10 < b * 6 && g * 10 < b * 7) {
        if(!scheme.keepSecondary) target = &scheme.secondary;       // blue: overalls
    }
    else if(r == high && g * 10 < r * 5 && b * 10 < r * 5) {
        if(!scheme.keepPrimary) target = &scheme.primary;           // red: Mario's shirt and cap
    }
    else if(g == high && r * 10 < g * 6 && b * 10 < g * 6) {
        if(!scheme.keepPrimary) target = &scheme.primary;           // green: Luigi's shirt and cap
    }
    if(!target) return color;

    // Keep the shading: `low` is how washed out the pixel is, `high - low`
    // how much colour it carries.
    u8 range = (u8)(high - low);
    u32 nr = mix((u8)low, range, target->r);
    u32 ng = mix((u8)low, range, target->g);
    u32 nb = mix((u8)low, range, target->b);
    return (u16)(((nr * 31 + 127) / 255) << 11 | ((ng * 63 + 127) / 255) << 5 | ((nb * 31 + 127) / 255));
}

// CMPR: 8-byte blocks of two RGB565 colours + sixteen 2-bit indices. Which of
// the two colours is larger selects the block mode, so that relation has to
// survive the recolouring.
static void recolorTexture(u8 *dst, const u8 *src, u32 size, const Scheme &scheme) {
    for(u32 i = 0; i + 8 <= size; i += 8) {
        u16 c0 = (u16)(src[i] << 8 | src[i + 1]);
        u16 c1 = (u16)(src[i + 2] << 8 | src[i + 3]);
        u32 bits = (u32)src[i + 4] << 24 | (u32)src[i + 5] << 16 | (u32)src[i + 6] << 8 | src[i + 7];
        u16 n0 = recolor(c0, scheme);
        u16 n1 = recolor(c1, scheme);

        if(c0 > c1) {
            // 4-colour mode: needs n0 > n1
            if(n0 == n1) {
                if(n0 < 0xFFFF) n0++;
                else n1--;
            }
            else if(n0 < n1) {
                u16 t = n0; n0 = n1; n1 = t;
                bits ^= 0x55555555; // 0<->1, 2<->3
            }
        }
        else if(n0 > n1) {
            // 3-colour + transparent mode: needs n0 <= n1
            u16 t = n0; n0 = n1; n1 = t;
            for(u32 k = 0; k < 32; k += 2) {
                if(!(bits >> k & 2)) bits ^= 1u << k; // 0<->1 only
            }
        }

        dst[i] = (u8)(n0 >> 8); dst[i + 1] = (u8)n0;
        dst[i + 2] = (u8)(n1 >> 8); dst[i + 3] = (u8)n1;
        dst[i + 4] = (u8)(bits >> 24); dst[i + 5] = (u8)(bits >> 16);
        dst[i + 6] = (u8)(bits >> 8); dst[i + 7] = (u8)bits;
    }
    DCFlushRange(dst, size);
}

// ---- finding the materials -------------------------------------------------

// A texture table entry (ResTIMG) as stored in the model file. Read by offset:
// the struct in the headers carries an extra runtime field, so indexing an
// array of it would walk off the real 0x20-byte entries.
static const u32 TIMG_SIZE = 0x20;

// First CMPR texture of the given size in the model's texture table
static const u8* findTexture(const u8 *table, u32 num, u16 width, u16 height) {
    for(u32 i = 0; i < num; i++) {
        const u8 *entry = table + i * TIMG_SIZE;
        u8 format = entry[0];
        u16 w = *(const u16 *)(entry + 2);
        u16 h = *(const u16 *)(entry + 4);
        if(format == 14 && w == width && h == height) {
            return entry + *(const u32 *)(entry + 0x1C); // image offset is relative to the entry
        }
    }
    return nullptr;
}

// The address as the "set texture image" command stores it
static u32 imageField(const u8 *image) {
    return ((u32)image & 0x3FFFFFFF) >> 5;
}

// Does this display list load `image` into texture slot 0?
// (BP command 0x61, register 0x94 = image address of slot 0)
static bool loadsImage(const u8 *list, u32 size, const u8 *image) {
    if(!list || size < 5 || size > 0x1000) return false;
    const u32 field = imageField(image);
    for(u32 i = 0; i + 5 <= size; i++) {
        if(list[i] == 0x61 && list[i + 1] == 0x94
            && ((u32)list[i + 2] << 16 | (u32)list[i + 3] << 8 | list[i + 4]) == field) return true;
    }
    return false;
}

// The same 32-byte list the game builds for its own texture swaps
// (MarioActor::createTextureDL): one "set image address" command, zero padded.
static void buildOverride(u8 *list, const u8 *image) {
    const u32 field = imageField(image);
    for(u32 i = 0; i < OVERRIDE_SIZE; i++) list[i] = 0;
    list[0] = 0x61;
    list[1] = 0x94;
    list[2] = (u8)(field >> 16);
    list[3] = (u8)(field >> 8);
    list[4] = (u8)field;
    DCFlushRange(list, OVERRIDE_SIZE);
}

// J3DModelX (Mario's model class) keeps, per material, an optional extra
// display list that is run after the material is set up. The game uses it to
// swap Mario's clothes texture for power-ups; we use it for player colours.
static const u32 MODELX_OVERRIDE_LISTS = 0x1C8; // u8** : one list per material
static const u32 MODELX_OVERRIDE_SIZES = 0x1CC; // u16* : their sizes
static const u32 MATERIAL_SHARED_LIST = 0x48;   // J3DMaterial::mSharedDLObj

static const u32 NO_MATERIAL = 0xFFFFFFFF;
static u32 g_bodyMaterial = NO_MATERIAL, g_capMaterial = NO_MATERIAL;
static const void *g_preparedFor = nullptr;

// What begin() replaced, for end() to put back
static J3DModel *g_swappedModel = nullptr;
static u8 *g_savedList[2];
static u16 g_savedSize[2];

// Readable from outside the game (tools/emutest) when something does not show
u32 debug[8];

static void prepare(J3DModel *model) {
    J3DModelData *data = model->mModelData;
    if(data == g_preparedFor) return;
    g_preparedFor = data;
    g_bodyMaterial = NO_MATERIAL;
    g_capMaterial = NO_MATERIAL;
    debug[1]++;
    if(!g_buffer) return;

    // J3DTexture: u16 mNum; u16 pad; ResTIMG* mpRes;
    const u8 *texture = (const u8 *)data->mMaterialTable.mTexture;
    if(!texture) return;
    u32 num = *(const u16 *)texture;
    const u8 *table = *(const u8 * const *)(texture + 4);
    if(!table) return;

    const u8 *bodyImage = findTexture(table, num, 256, 128);
    const u8 *capImage = findTexture(table, num, 128, 64);
    debug[2] = (u32)bodyImage;
    debug[3] = (u32)capImage;
    if(!bodyImage || !capImage) return;

    // Which materials wear those textures
    for(u32 i = 0; i < data->mMaterialTable.mMaterialCount; i++) {
        const u8 *material = (const u8 *)data->mMaterialTable.mMaterials[i];
        if(!material) continue;
        const DisplayListObj *obj = *(const DisplayListObj * const *)(material + MATERIAL_SHARED_LIST);
        if(!obj) continue;
        if(g_bodyMaterial == NO_MATERIAL && loadsImage(obj->list[0], obj->size, bodyImage)) g_bodyMaterial = i;
        else if(g_capMaterial == NO_MATERIAL && loadsImage(obj->list[0], obj->size, capImage)) g_capMaterial = i;
    }
    debug[4] = g_bodyMaterial;
    debug[5] = g_capMaterial;
    if(g_bodyMaterial == NO_MATERIAL || g_capMaterial == NO_MATERIAL) {
        g_bodyMaterial = NO_MATERIAL;
        return;
    }

    for(u32 c = 0; c < NUM_COLORS - 1; c++) {
        recolorTexture(bodyTex(c), bodyImage, BODY_SIZE, g_schemes[c]);
        recolorTexture(capTex(c), capImage, CAP_SIZE, g_schemes[c]);
        buildOverride(bodyOverride(c), bodyTex(c));
        buildOverride(capOverride(c), capTex(c));
    }
}

void end() {
    if(!g_swappedModel) return;
    u8 **lists = *(u8 ***)((u8 *)g_swappedModel + MODELX_OVERRIDE_LISTS);
    u16 *sizes = *(u16 **)((u8 *)g_swappedModel + MODELX_OVERRIDE_SIZES);
    lists[g_bodyMaterial] = g_savedList[0];
    sizes[g_bodyMaterial] = g_savedSize[0];
    lists[g_capMaterial] = g_savedList[1];
    sizes[g_capMaterial] = g_savedSize[1];
    g_swappedModel = nullptr;
}

void begin(J3DModel *model, u8 color, bool keepGameOverride) {
    end();
    if(color == 0 || color >= NUM_COLORS) return;
    debug[6]++;
    prepare(model);
    if(g_bodyMaterial == NO_MATERIAL) return;

    u8 **lists = *(u8 ***)((u8 *)model + MODELX_OVERRIDE_LISTS);
    u16 *sizes = *(u16 **)((u8 *)model + MODELX_OVERRIDE_SIZES);
    if(!lists || !sizes) return;
    // The game is already overriding the clothes (a power-up): for the local
    // player that wins, so the power-up still shows.
    if(keepGameOverride && (lists[g_bodyMaterial] || lists[g_capMaterial])) return;

    g_savedList[0] = lists[g_bodyMaterial];
    g_savedSize[0] = sizes[g_bodyMaterial];
    g_savedList[1] = lists[g_capMaterial];
    g_savedSize[1] = sizes[g_capMaterial];
    lists[g_bodyMaterial] = bodyOverride(color - 1);
    sizes[g_bodyMaterial] = OVERRIDE_SIZE;
    lists[g_capMaterial] = capOverride(color - 1);
    sizes[g_capMaterial] = OVERRIDE_SIZE;
    g_swappedModel = model;
    debug[7]++;
}

void onStageInit() {
    // A new stage: the previous model and our old buffer (both on the previous
    // stage heap) are gone.
    g_swappedModel = nullptr;
    g_preparedFor = nullptr;
    g_bodyMaterial = NO_MATERIAL;
    g_capMaterial = NO_MATERIAL;
    // Called while the stage is being built, so this lands on the stage heap.
    // If there is no room, players simply keep their normal clothes.
    g_buffer = new (32) u8[(NUM_COLORS - 1) * PER_COLOR];
    debug[0] = (u32)g_buffer;
}

}
