#include "game_hooks.h"

#include <mod/amlmod.h>
#include <mod/logger.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gtasa_v200_armv7.h"
#include "../diagnostics/weapon_log.h"
#include "../ui/ui_common.h"
#include "special_models.h"
#include "../platform/psdk_extern.h"
#include <aml-psdk/game_sa/engine/Streaming.h>
#include <aml-psdk/game_sa/engine/ModelInfo.h>
#include <aml-psdk/game_sa/entity/Camera.h>
#include <aml-psdk/game_sa/entity/Vehicle.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/other/WeaponInfo.h>
#include <aml-psdk/game_sa/other/Pools.h>
#include <aml-psdk/game_sa/other/Clock.h>
#include "AnimBlendFrameData.h"
#include "sync_types.h"
#include <aml-psdk/game_sa/engine/RsGlobal.h>
#include <aml-psdk/game_sa/engine/Sprite.h>
#include <aml-psdk/game_sa/engine/Sprite2d.h>
#include <aml-psdk/game_sa/engine/Font.h>
#include <aml-psdk/game_sa/engine/Radar.h>

namespace
{


constexpr bool kBonePoseSyncEnabled = false;

uintptr_t g_game = 0;
CoopClient* g_client = nullptr;
ISAUtils* g_saUtils = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalPedProcess = nullptr;
gtasa_v200_armv7::ProcessControlFn g_originalVehicleProcess = nullptr;
gtasa_v200_armv7::FireInstantHitFn g_originalFireInstantHit = nullptr;
gtasa_v200_armv7::EntityGetBoundCentreFn g_getBoundCentre = nullptr;
gtasa_v200_armv7::PedSetMoveStateFn g_setMoveState = nullptr;
gtasa_v200_armv7::EntityTeleportFn g_entityTeleport = nullptr;
gtasa_v200_armv7::PedSetMoveAnimFn g_setMoveAnim = nullptr;
using GiveWeaponFn = void (*)(void*, eWeaponType, unsigned int, bool);
using GetFirstAssociationFn = void* (*)(RpClump*);
using GetNextAssociationFn = void* (*)(void*);
using RemoveAllAssociationsFn = void (*)(RpClump*);
using BlendAnimationFn = void* (*)(RpClump*, int, int, float);
using DoGunFlashFn = bool (*)(void*, int, bool);
GetFirstAssociationFn g_getFirstAssociation = nullptr;
GetNextAssociationFn g_getNextAssociation = nullptr;
RemoveAllAssociationsFn g_removeAllAssociations = nullptr;
BlendAnimationFn g_blendAnimation = nullptr;
DoGunFlashFn g_doGunFlash = nullptr;
using SetCurrentWeaponFn = void (*)(void*, eWeaponType);
using RequestSpecialCharFn = void (*)(int, const char*, int);
GiveWeaponFn g_giveWeapon = nullptr;
SetCurrentWeaponFn g_setCurrentWeapon = nullptr;
RequestSpecialCharFn g_originalRequestSpecialChar = nullptr;




static const SCRIPT_COMMAND kScmGetWeaponTypeModel = { 0x0781, "iv" };
static const SCRIPT_COMMAND kScmGetCurrentCharWeapon = { 0x0470, "iv" };
static const SCRIPT_COMMAND kScmRequestModel = { 0x0247, "i" };
static const SCRIPT_COMMAND kScmHasModelLoaded = { 0x0248, "i" };
static const SCRIPT_COMMAND kScmLoadAllRequestedModels = { 0x038B, "" };
static const SCRIPT_COMMAND kScmGiveWeaponToChar = { 0x01B2, "iii" };
static const SCRIPT_COMMAND kScmSetCurrentCharWeapon = { 0x01B9, "ii" };


static const SCRIPT_COMMAND kScmWarpCharIntoCar = { 0x036A, "ii" };
static const SCRIPT_COMMAND kScmWarpCharIntoCarAsPassenger = { 0x0430, "iii" };



static const SCRIPT_COMMAND kScmTaskAimGunAtCoord = { 0x0667, "ifffi" };
static const SCRIPT_COMMAND kScmTaskShootAtCoord = { 0x0668, "ifffi" };


uint32_t g_scmWeaponModelOutput = 0;
uint32_t g_scmCurrentWeaponOutput = 0xFFFFFFFFu;

bool IsSelectableWeaponType(uint32_t weaponType)
{


    return weaponType <= 0x2Eu || weaponType == 0x3Au;
}

uint8_t CurrentWeaponTypeForPed(CPed* ped)
{
    if (!ped) return 0;
    const unsigned slot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    uint8_t nativeWeapon = slot < 13
        ? static_cast<uint8_t>(ped->m_aWeapons[slot].m_eWeaponType) : 0;




    if (g_saUtils) {
        const int pedRef = CPools::GetPedRef(ped);
        if (pedRef > 0 && CPools::GetPed(pedRef) == ped) {
            g_scmCurrentWeaponOutput = 0xFFFFFFFFu;
            g_saUtils->ScriptCommand(&kScmGetCurrentCharWeapon, pedRef,
                                     &g_scmCurrentWeaponOutput);
            if (g_scmCurrentWeaponOutput != 0xFFFFFFFFu &&
                IsSelectableWeaponType(g_scmCurrentWeaponOutput))
                return static_cast<uint8_t>(g_scmCurrentWeaponOutput);
        }
    }
    return IsSelectableWeaponType(nativeWeapon) ? nativeWeapon : 0;
}
std::array<std::array<char, coop_special::kNameCapacity>, 11> g_specialCharNames{};
std::mutex g_specialCharNamesMutex;




struct StableSpecialIdentity {
    uint16_t modelIndex = 0;
    std::string name;
};
std::unordered_map<uint32_t, StableSpecialIdentity> g_localSpecialIdentities;
std::vector<CPed*> g_spawnedNpcs;
std::unordered_map<uint16_t, CPed*> g_remotePeds;


std::unordered_map<uint16_t, int> g_remotePedRefs;


constexpr float kRemoteTeleportDistance = 25.0f;
std::unordered_map<uint16_t, CVehicle*> g_remoteVehicles;



struct VehicleEntityBinding {
    CVehicle* vehicle = nullptr;
    int poolRef = 0;
    bool createdByFusion = false;
};
std::unordered_map<uint32_t, VehicleEntityBinding> g_localVehicleEntities;
std::unordered_map<uint16_t, VehicleEntityBinding> g_vehicleEntityByNetworkId;
std::unordered_map<uint32_t, int> g_remoteMissionMarkerBlips;
struct RemoteVehicleVisualState {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    float rightX = 1.0f, rightY = 0.0f, rightZ = 0.0f;
    float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
    float atX = 0.0f, atY = 0.0f, atZ = 1.0f;
    bool initialized = false;
};
std::unordered_map<uint16_t, RemoteVehicleVisualState> g_remoteVehicleVisuals;
std::unordered_map<uint32_t, CPed*> g_remoteNpcs;
struct RemoteNpcVisualState {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool initialized = false;


    uint8_t weapon = 0xFF;
    uint64_t nextWeaponTryMs = 0;
};
std::unordered_map<uint32_t, RemoteNpcVisualState> g_remoteNpcVisuals;
std::unordered_map<uint32_t, uint32_t> g_localNpcRefs;
uint64_t g_lastNpcSyncMs = 0;
bool g_remoteNameTagsVisible = true;
bool g_remoteHealthBarsVisible = true;
std::unordered_map<uint16_t, uint64_t> g_lastVehicleClaimMs;


float g_remoteNameTagScale = 0.5f;



struct WeaponIconRect { float left, bottom, right, top; };
using GetWeaponInfoForIconFn = void* (*)(int weaponType, int skill);
using RequestWeaponIconModelFn = void (*)(int model, int flags);
using LoadWeaponIconModelsFn = void (*)(bool priority);
using DrawWeaponIconFn = void (*)(void* self, void* ped, WeaponIconRect rect, float alpha);
GetWeaponInfoForIconFn g_getWeaponInfoForIcon = nullptr;
RequestWeaponIconModelFn g_requestWeaponIconModel = nullptr;
LoadWeaponIconModelsFn g_loadWeaponIconModels = nullptr;
DrawWeaponIconFn g_drawWeaponIcon = nullptr;
std::unordered_set<int> g_streamedWeaponIconModels;

struct RemoteVisualState {


    RemoteVisualState() {}
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    float lastTargetX = 0.0f, lastTargetY = 0.0f;
    uint8_t weapon = 0;
    uint8_t shootingFrames = 0;
    uint16_t fireAnimGroup = 0;
    int16_t fireAnimId = 0;
    uint8_t fireAnimFrames = 0;
    bool animationInitialized = false;
    bool initialized = false;
    bool wasInVehicle = false;
    uint8_t lastVehicleSeat = 0;
    uint64_t lastAimTaskMs = 0;
    uint64_t lastShootTaskMs = 0;
    uint32_t animationSequence = 0;
    uint8_t animationLocomotion = 0, animationAction = 0;
    uint16_t animationFlags = 0;
    uint8_t animationAssociationCount = 0;
    std::array<AnimationAssociationState, 64> animationAssociations{};
};
RemoteVisualState MakeInitialRemoteVisualState(const RemotePlayer& player)
{
    RemoteVisualState visual{};
    visual.x = player.x;
    visual.y = player.y;
    visual.z = player.z;
    visual.heading = player.heading;
    visual.lastTargetX = player.x;
    visual.lastTargetY = player.y;
    visual.weapon = 0xFF;
    visual.wasInVehicle = player.vehicleId != 0;
    visual.lastVehicleSeat = player.vehicleSeat;
    visual.initialized = true;
    return visual;
}
std::unordered_map<uint16_t, RemoteVisualState> g_remoteVisuals;
std::unordered_map<const CPed*, uint64_t> g_remoteTaskPermitUntilMs;
std::unordered_map<uint16_t, uint16_t> g_lastRemotePlayerHealth;
std::unordered_map<uint16_t, uint16_t> g_lastRejectedRemoteModels;
struct RemoteSpecialLease {
    int activeSlot = 0;
    std::string activeName;
    int pendingSlot = 0;
    std::string pendingName;
};
std::unordered_map<uint64_t, RemoteSpecialLease> g_remoteSpecialLeases;


std::array<int, 11> g_remoteSlotRefs{};
std::array<std::string, 11> g_remoteSlotHeldName{};

bool HasPedModelInfo(uint16_t modelId)
{
    if (modelId > 311) return false;
    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    if (!modelInfos || !modelInfos[modelId]) return false;
    return modelInfos[modelId]->GetModelType() == MODEL_INFO_PED;
}

bool HasReadyPedModel(uint16_t modelId)
{
    if (!HasPedModelInfo(modelId)) return false;
    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    return modelInfos && modelInfos[modelId] && modelInfos[modelId]->m_pRwClump != nullptr;
}

bool IsSafeNetworkVehicleModel(uint16_t modelId)
{
    if (modelId < 400 || modelId > 611 || !CStreaming::IsInitialised() ||
        !CStreaming::IsObjectInCdImage(static_cast<int>(modelId))) return false;
    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    return modelInfos && modelInfos[modelId] && modelInfos[modelId]->GetModelType() == MODEL_INFO_VEHICLE;
}

bool IsSafeNetworkPedModel(uint16_t modelId)
{



    if (modelId == 0) return true;
    return modelId <= 311 && CStreaming::IsInitialised() && HasPedModelInfo(modelId) &&
           CStreaming::IsObjectInCdImage(static_cast<int>(modelId));
}

void LoadNetworkPedModel(uint16_t modelId)
{



    g_saUtils->LoadModelId(static_cast<int>(modelId));
    CStreaming::LoadAllRequestedModels(false);
}

void HookRequestSpecialChar(int slot, const char* modelName, int flags)
{
    char normalized[coop_special::kNameCapacity]{};
    if (slot >= 1 && slot <= 10) {




        const bool known = coop_special::NormalizeName(modelName, normalized, sizeof(normalized));
        bool changed = false;
        {
            std::scoped_lock lock(g_specialCharNamesMutex);
            changed = std::strcmp(g_specialCharNames[slot].data(), normalized) != 0;
            std::snprintf(g_specialCharNames[slot].data(), g_specialCharNames[slot].size(), "%s", normalized);
        }
        if (changed) WeaponFileLog("SPECIAL_CHAR_REQUEST slot=%d name=%.15s known=%d", slot,
                                   known ? normalized : (modelName ? modelName : ""), known ? 1 : 0);
    }
    if (g_originalRequestSpecialChar) g_originalRequestSpecialChar(slot, modelName, flags);
}

std::string CurrentSpecialNameForModelIndex(uint16_t modelIndex)
{
    const int slot = coop_special::SlotFromModelIndex(modelIndex);
    if (!slot || !CStreaming::IsInitialised() || !CStreaming::HasSpecialCharLoaded(slot)) return {};
    {
        std::scoped_lock lock(g_specialCharNamesMutex);
        if (g_specialCharNames[slot][0] != '\0') return g_specialCharNames[slot].data();
    }




    CBaseModelInfo** modelInfos = CModelInfo::ms_modelInfoPtrs;
    if (!modelInfos || !modelInfos[modelIndex]) return {};
    char normalized[coop_special::kNameCapacity]{};
    if (coop_special::NormalizeName(modelInfos[modelIndex]->m_szModelName,
                                    normalized, sizeof(normalized)))
        return normalized;
    return {};
}

std::string StableSpecialNameForPed(CPed* ped, uint16_t modelIndex)
{
    if (!ped || modelIndex < coop_special::kFirstModelIndex ||
        modelIndex > coop_special::kLastModelIndex) return {};

    const uint32_t ref = static_cast<uint32_t>(CPools::GetPedRef(ped));
    if (!ref) return {};

    auto it = g_localSpecialIdentities.find(ref);
    if (it != g_localSpecialIdentities.end() && it->second.modelIndex == modelIndex &&
        !it->second.name.empty()) {
        return it->second.name;
    }




    const std::string current = CurrentSpecialNameForModelIndex(modelIndex);
    if (!current.empty()) {
        g_localSpecialIdentities[ref] = {modelIndex, current};
        WeaponFileLog("SPECIAL_PED_CAPTURE ref=%u model=%hu name=%s", ref, modelIndex, current.c_str());
        return current;
    }



    if (it != g_localSpecialIdentities.end() && it->second.modelIndex == modelIndex)
        return it->second.name;

    return {};
}

void ForgetLocalSpecialIdentity(uint32_t ref)
{
    if (ref) g_localSpecialIdentities.erase(ref);
}

uint64_t RemoteNpcSpecialLeaseKey(uint32_t npcId)
{
    return (uint64_t{1} << 63) | npcId;
}

void ReleaseRemoteSpecialSlot(uint64_t , int slot, const std::string& )
{

    if (slot < coop_special::kFirstRemoteSlot || slot > coop_special::kLastRemoteSlot ||
        g_remoteSlotRefs[slot] <= 0)
        return;
    if (--g_remoteSlotRefs[slot] > 0) return;
    if (CStreaming::IsInitialised()) {
        CStreaming::SetSpecialCharIsDeletable(slot);
        CStreaming::SetMissionDoesntRequireSpecialChar(slot);
    }
    g_remoteSlotHeldName[slot].clear();
    std::scoped_lock lock(g_specialCharNamesMutex);
    g_specialCharNames[slot][0] = '\0';
}

void ReleaseRemoteSpecialLease(uint64_t ownerKey)
{
    auto it = g_remoteSpecialLeases.find(ownerKey);
    if (it == g_remoteSpecialLeases.end()) return;
    ReleaseRemoteSpecialSlot(ownerKey, it->second.pendingSlot, it->second.pendingName);
    ReleaseRemoteSpecialSlot(ownerKey, it->second.activeSlot, it->second.activeName);
    g_remoteSpecialLeases.erase(it);
}

void ReleasePendingRemoteSpecialSlot(uint64_t ownerKey)
{
    auto it = g_remoteSpecialLeases.find(ownerKey);
    if (it == g_remoteSpecialLeases.end() || !it->second.pendingSlot) return;
    ReleaseRemoteSpecialSlot(ownerKey, it->second.pendingSlot, it->second.pendingName);
    it->second.pendingSlot = 0;
    it->second.pendingName.clear();
    if (!it->second.activeSlot) g_remoteSpecialLeases.erase(it);
}

void CommitRemoteSpecialSlot(uint64_t ownerKey, int slot, const std::string& modelName)
{
    auto it = g_remoteSpecialLeases.find(ownerKey);
    if (it == g_remoteSpecialLeases.end()) return;
    auto& lease = it->second;
    if (slot == lease.pendingSlot && lease.pendingName == modelName) {
        const int oldSlot = lease.activeSlot;
        const std::string oldName = lease.activeName;
        lease.activeSlot = slot;
        lease.activeName = modelName;
        lease.pendingSlot = 0;
        lease.pendingName.clear();
        if (oldSlot && oldSlot != slot) ReleaseRemoteSpecialSlot(ownerKey, oldSlot, oldName);
    }
}

int EnsureRemoteSpecialSlot(uint64_t ownerKey, const std::string& modelName)
{
    auto& lease = g_remoteSpecialLeases[ownerKey];
    if (lease.activeSlot && lease.activeName == modelName) return lease.activeSlot;
    if (lease.pendingSlot && lease.pendingName == modelName) return lease.pendingSlot;
    if (lease.pendingSlot) {
        ReleaseRemoteSpecialSlot(ownerKey, lease.pendingSlot, lease.pendingName);
        lease.pendingSlot = 0;
        lease.pendingName.clear();
    }
    if (!CStreaming::IsInitialised()) return 0;


    for (int slot = coop_special::kFirstRemoteSlot; slot <= coop_special::kLastRemoteSlot; ++slot) {
        if (g_remoteSlotRefs[slot] > 0 && g_remoteSlotHeldName[slot] == modelName) {
            ++g_remoteSlotRefs[slot];
            lease.pendingSlot = slot;
            lease.pendingName = modelName;
            return slot;
        }
    }


    auto findFree = [&]() {
        for (int slot = coop_special::kFirstRemoteSlot; slot <= coop_special::kLastRemoteSlot; ++slot) {
            if (g_remoteSlotRefs[slot] == 0 && !CStreaming::HasSpecialCharLoaded(slot)) return slot;
        }
        return 0;
    };
    int selected = findFree();
    if (!selected) {




        bool releasedButLoaded = false;
        for (int slot = coop_special::kFirstRemoteSlot; slot <= coop_special::kLastRemoteSlot; ++slot)
            if (g_remoteSlotRefs[slot] == 0 && CStreaming::HasSpecialCharLoaded(slot)) releasedButLoaded = true;
        static uint64_t lastSweepMs = 0;
        const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (releasedButLoaded && nowMs - lastSweepMs >= 1000) {
            lastSweepMs = nowMs;
            CStreaming::RemoveAllUnusedModels();
            selected = findFree();
        }
    }
    if (!selected) return 0;

    g_remoteSlotRefs[selected] = 1;
    g_remoteSlotHeldName[selected] = modelName;
    lease.pendingSlot = selected;
    lease.pendingName = modelName;
    CStreaming::RequestSpecialChar(selected, modelName.c_str(), 2);
    CStreaming::LoadAllRequestedModels(false);
    return selected;
}












constexpr uint64_t kRemoteNpcModelRetryMs = 250;
constexpr uint64_t kRemoteNpcModelGiveUpMs = 4000;
constexpr uint64_t kRemoteNpcModelRecheckMs = 5000;

constexpr uint16_t kSpecialLoadWaitKey = 0xFFFF;
constexpr uint16_t kUnnamedSpecialWaitKey = 0xFFFE;

struct RemoteNpcModelWait {
    uint16_t modelId = 0;
    uint64_t firstFailMs = 0;
    uint64_t nextTryMs = 0;
};
std::unordered_map<uint32_t, RemoteNpcModelWait> g_remoteNpcModelWait;
std::unordered_set<uint64_t> g_remoteNpcLogged;

uint64_t SteadyNowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}


bool RemoteNpcMayTryModel(uint32_t npcId, uint16_t modelId)
{
    if (modelId == 0) return true;
    auto it = g_remoteNpcModelWait.find(npcId);
    if (it == g_remoteNpcModelWait.end() || it->second.modelId != modelId) return true;
    return SteadyNowMs() >= it->second.nextTryMs;
}



bool RemoteNpcNoteModelFailure(uint32_t npcId, uint16_t modelId)
{
    const uint64_t now = SteadyNowMs();
    auto& w = g_remoteNpcModelWait[npcId];
    if (w.modelId != modelId) {
        w = RemoteNpcModelWait{};
        w.modelId = modelId;
        w.firstFailMs = now;
    }
    const bool giveUp = now - w.firstFailMs >= kRemoteNpcModelGiveUpMs;
    w.nextTryMs = now + (giveUp ? kRemoteNpcModelRecheckMs : kRemoteNpcModelRetryMs);
    return !giveUp;
}



bool RemoteNpcLogOnce(uint32_t npcId, uint8_t reason, uint16_t detail)
{
    if (g_remoteNpcLogged.size() > 4096) g_remoteNpcLogged.clear();
    const uint64_t key = (static_cast<uint64_t>(npcId) << 24) |
                         (static_cast<uint64_t>(reason) << 16) | detail;
    return g_remoteNpcLogged.insert(key).second;
}

ImU32 NameTagColor(uint8_t colorIndex)
{
    static const ImU32 palette[] = {
        IM_COL32(255, 255, 255, 255),
        IM_COL32(255, 235, 40, 255),
        IM_COL32(70, 235, 90, 255),
        IM_COL32(60, 220, 235, 255),
        IM_COL32(80, 145, 255, 255),
        IM_COL32(200, 100, 255, 255),
        IM_COL32(255, 165, 55, 255),
        IM_COL32(255, 75, 75, 255),
    };
    return palette[colorIndex <= 7 ? colorIndex : 0];
}

void DrawRemoteWeaponIcon(CPed* ped, uint8_t weaponType, float cx, float cy, float iconSize) {
    if (!g_drawWeaponIcon || !ped || weaponType > 0x3A) return;
    if (weaponType != 0 && g_getWeaponInfoForIcon && g_requestWeaponIconModel && g_loadWeaponIconModels) {
        void* info = g_getWeaponInfoForIcon(static_cast<int>(weaponType), 1);
        if (info) {

            const int model = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(info) + 0x0C);
            if (model > 0 && model < 30000 && g_streamedWeaponIconModels.find(model) == g_streamedWeaponIconModels.end()) {
                g_requestWeaponIconModel(model, 0x1A);
                g_loadWeaponIconModels(true);
                g_streamedWeaponIconModels.insert(model);
            }
        }
    }
    auto* bytes = reinterpret_cast<uint8_t*>(ped);

    int8_t* activeSlot = reinterpret_cast<int8_t*>(bytes + 0x71C);
    uint32_t* slotZeroType = reinterpret_cast<uint32_t*>(bytes + 0x5A4);
    const int8_t oldSlot = *activeSlot;
    const uint32_t oldWeapon = *slotZeroType;
    *activeSlot = 0;
    *slotZeroType = static_cast<uint32_t>(weaponType);
    const float half = iconSize * 0.5f;
    WeaponIconRect rect{cx - half, cy + half, cx + half, cy - half};

    g_drawWeaponIcon(nullptr, reinterpret_cast<void*>(ped), rect, 255.0f);
    *activeSlot = oldSlot;
    *slotZeroType = oldWeapon;
}



bool CaptureFireAssociation(CPed* ped, uint16_t& group, uint16_t& animId);

void HookFireInstantHit(void* weapon, void* entity, void* origin, void* target,
                        void* damagedEntity, void* source, void* direction,
                        bool unknown1, bool unknown2)
{
    if (g_originalFireInstantHit)
        g_originalFireInstantHit(weapon, entity, origin, target, damagedEntity, source, direction, unknown1, unknown2);
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (!g_client || !localPlayer || !g_getBoundCentre) return;
    gtasa_v200_armv7::CVectorAbi centre{};
    g_getBoundCentre(reinterpret_cast<void*>(localPlayer), centre);
    const uint8_t weaponType = CurrentWeaponTypeForPed(localPlayer);
    if (weaponType == 0 || weaponType > 0x3A) return;


    float dx = 0.0f, dy = 1.0f, dz = 0.0f;
    if (direction) {
        const auto* vector = reinterpret_cast<const gtasa_v200_armv7::CVectorAbi*>(direction);
        if (std::isfinite(vector->x) && std::isfinite(vector->y) && std::isfinite(vector->z))
            dx = vector->x, dy = vector->y, dz = vector->z;
    } else if (target && origin) {
        const auto* start = reinterpret_cast<const gtasa_v200_armv7::CVectorAbi*>(origin);
        const auto* end = reinterpret_cast<const gtasa_v200_armv7::CVectorAbi*>(target);
        dx = end->x - start->x; dy = end->y - start->y; dz = end->z - start->z;
    } else if (const auto* matrix = localPlayer->GetMatrix()) {
        const auto& forward = matrix->GetForward();
        dx = forward.x; dy = forward.y; dz = forward.z;
    }
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(length) || length < 0.001f) return;
    dx /= length; dy /= length; dz /= length;


    uint16_t fireGroup = 0, fireAnimId = 0;
    CaptureFireAssociation(localPlayer, fireGroup, fireAnimId);
    g_client->SendShot(weaponType, centre.x, centre.y, centre.z + 0.7f, dx, dy, dz, fireGroup, fireAnimId);
    WeaponFileLog("SHOT_SEND_FIRE_HOOK weapon=%u origin=(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f)", weaponType, centre.x, centre.y, centre.z, dx, dy, dz);
}

float AngleDelta(float target, float current)
{
    while (target - current > 180.0f) target -= 360.0f;
    while (target - current < -180.0f) target += 360.0f;
    return target - current;
}

float SmoothValue(float current, float target, float factor)
{
    return current + (target - current) * factor;
}

float HeadingDegrees(CPlaceable* placeable)
{
    if (!placeable) return 0.0f;
    float heading = placeable->m_placement.m_fHeading * 57.2957795f;
    if (auto* matrix = placeable->GetMatrix()) {
        const auto& forward = matrix->GetForward();
        if (std::isfinite(forward.x) && std::isfinite(forward.y) &&
            (std::fabs(forward.x) > 0.0001f || std::fabs(forward.y) > 0.0001f))
            heading = std::atan2(-forward.x, forward.y) * 57.2957795f;
    }
    if (!std::isfinite(heading)) return 0.0f;
    heading = std::fmod(heading, 360.0f);
    return heading < 0.0f ? heading + 360.0f : heading;
}













CVehicle* GetSeatedVehicle(CPed* ped, uint8_t* outSeat = nullptr)
{
    if (outSeat) *outSeat = 0;
    if (!ped) return nullptr;
    CVehicle* vehicle = ped->m_pVehicle;
    if (!vehicle) return nullptr;
    if (vehicle->m_pDriver == ped) return vehicle;
    for (uint8_t si = 0; si < 8; ++si) {
        if (vehicle->m_apPassengers[si] == ped) {
            if (outSeat) *outSeat = static_cast<uint8_t>(si + 1);
            return vehicle;
        }
    }
    if (ped->bInVehicle) return vehicle;
    return nullptr;
}

















struct LocalAssociationGate {
    static constexpr uint64_t kExitSettleMs = 1500;
    CVehicle* lastSeated = nullptr;
    uint64_t unseatedSinceMs = 0;



    bool ShouldSuppress(CVehicle* seated, CVehicle* rawPtr, uint64_t nowMs)
    {
        if (seated) { lastSeated = seated; unseatedSinceMs = 0; return true; }
        if (!rawPtr) { lastSeated = nullptr; unseatedSinceMs = 0; return false; }
        if (rawPtr != lastSeated) { unseatedSinceMs = 0; return true; }
        if (!unseatedSinceMs) unseatedSinceMs = nowMs;
        return nowMs - unseatedSinceMs < kExitSettleMs;
    }
};

AnimationProfile CaptureAnimationProfile(CPed* ped, uint32_t sequence)
{
    AnimationProfile profile{};
    if (!ped) return profile;
    profile.sequence = sequence;
    profile.locomotion = static_cast<uint8_t>(std::max(0, std::min(7, static_cast<int>(ped->m_eMoveStateAnim))));
    profile.animGroup = static_cast<uint16_t>(std::min(0xFFFFu, ped->m_nAnimGroup));
    profile.weaponState = 0;
    const unsigned slot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    if (slot < 13) profile.weaponState = static_cast<uint8_t>(ped->m_aWeapons[slot].m_nState);
    profile.aimYaw = std::isfinite(ped->m_fAimingRotation) ? ped->m_fAimingRotation : 0.0f;
    profile.aimPitch = std::isfinite(ped->m_pedIK.m_TorsoOrien.m_fPitch) ? ped->m_pedIK.m_TorsoOrien.m_fPitch : 0.0f;
    profile.blend = 1.0f;
    profile.phase = 0.0f;
    profile.flags = ANIM_ON_GROUND | ANIM_LOOPING;


    uint8_t profileSeat = 0;
    CVehicle* profileVehicle = GetSeatedVehicle(ped, &profileSeat);
    if (profileVehicle) profile.flags |= ANIM_IN_VEHICLE;
    if (ped->bIsDucking) profile.flags |= ANIM_DUCKING;
    if (ped->GetHealth() <= 0.0f || ped->m_ePedState == PEDSTATE_DIE || ped->m_ePedState == PEDSTATE_DEAD) {
        profile.action = AnimationAction::Death; profile.flags |= ANIM_DEAD;
    } else if (profileVehicle) {
        profile.action = profileSeat == 0 ? AnimationAction::Driving : AnimationAction::Passenger;
    } else {
        switch (ped->m_ePedState) {
            case PEDSTATE_JUMP: profile.action = AnimationAction::Jump; break;
            case PEDSTATE_FALL: profile.action = AnimationAction::Fall; break;
            case PEDSTATE_GETUP: profile.action = AnimationAction::GetUp; break;
            case PEDSTATE_ATTACK: profile.action = AnimationAction::Attack; profile.flags |= ANIM_UPPER_BODY | ANIM_FIRING; break;
            case PEDSTATE_AIMGUN: profile.action = AnimationAction::Aim; profile.flags |= ANIM_UPPER_BODY | ANIM_AIMING; break;
            case PEDSTATE_HANDS_UP: profile.action = AnimationAction::HandsUp; break;
            case PEDSTATE_FACE_PHONE:
            case PEDSTATE_MAKE_PHONECALL:
            case PEDSTATE_ANSWER_MOBILE: profile.action = AnimationAction::Phone; break;
            case PEDSTATE_CHAT: profile.action = AnimationAction::Chat; break;
            default: profile.action = profile.locomotion == kMoveStill ? AnimationAction::Idle : AnimationAction::Locomotion; break;
        }
        if (ped->bIsDucking && profile.action == AnimationAction::Idle) profile.action = AnimationAction::Crouch;
    }



    if (!profileVehicle && profile.weaponState == static_cast<uint8_t>(WEAPONSTATE_FIRING)) {
        profile.flags |= ANIM_FIRING | ANIM_UPPER_BODY;
        if (profile.action == AnimationAction::Idle || profile.action == AnimationAction::Locomotion)
            profile.action = AnimationAction::Attack;
    }
    return profile;
}

void ApplyRemoteAnimationProfile(CPed* ped, const AnimationProfile& profile, bool inVehicle, bool shooting)
{
    if (!ped) return;



    if (!inVehicle && !(profile.flags & ANIM_IN_VEHICLE) && g_setMoveState && g_setMoveAnim) {
        const int move = shooting ? kMoveStill : static_cast<int>(profile.locomotion <= kMoveSprint ? profile.locomotion : kMoveStill);
        g_setMoveState(ped, move);
        g_setMoveAnim(ped);
    }


    if (std::isfinite(profile.aimYaw)) ped->m_fAimingRotation = profile.aimYaw;
    if (std::isfinite(profile.aimPitch)) ped->m_pedIK.m_TorsoOrien.m_fPitch = profile.aimPitch;
}

constexpr uintptr_t kAssocGroup = 0x0E, kAssocBlend = 0x18, kAssocBlendDelta = 0x1C, kAssocCurrentTime = 0x20, kAssocSpeed = 0x24, kAssocTimeStep = 0x28, kAssocAnimId = 0x2C, kAssocFlags = 0x2E;
void ReadAssociation(void* p, AnimationAssociationState& s){const uintptr_t a=reinterpret_cast<uintptr_t>(p);s.group=*reinterpret_cast<uint16_t*>(a+kAssocGroup);s.animationId=*reinterpret_cast<int16_t*>(a+kAssocAnimId);s.flags=*reinterpret_cast<uint16_t*>(a+kAssocFlags);s.blend=*reinterpret_cast<float*>(a+kAssocBlend);s.blendDelta=*reinterpret_cast<float*>(a+kAssocBlendDelta);s.currentTime=*reinterpret_cast<float*>(a+kAssocCurrentTime);s.speed=*reinterpret_cast<float*>(a+kAssocSpeed);s.timeStep=*reinterpret_cast<float*>(a+kAssocTimeStep);}
[[maybe_unused]] void WriteAssociation(void* p,const AnimationAssociationState& s){const uintptr_t a=reinterpret_cast<uintptr_t>(p);*reinterpret_cast<float*>(a+kAssocBlend)=s.blend;*reinterpret_cast<float*>(a+kAssocBlendDelta)=s.blendDelta;*reinterpret_cast<float*>(a+kAssocCurrentTime)=s.currentTime;*reinterpret_cast<float*>(a+kAssocSpeed)=s.speed;*reinterpret_cast<float*>(a+kAssocTimeStep)=s.timeStep;*reinterpret_cast<uint16_t*>(a+kAssocFlags)=s.flags;}













using BlendAnimationHierarchyFn = void* (*)(RpClump*, void*, int, float);
using SetAssocBlendFn = void (*)(void*, float, float);
using SetAssocTimeFn = void (*)(void*, float);
BlendAnimationHierarchyFn g_blendAnimationHierarchy = nullptr;
SetAssocBlendFn g_setAssocBlend = nullptr;
SetAssocTimeFn g_setAssocTime = nullptr;

constexpr float kHashMarker = 99.0f;
constexpr unsigned kAssocWalkLimit = 64;
constexpr uint16_t kAssocMovementFlag = 0x0020;
constexpr float kAssocFadeRate = 8.0f;
constexpr unsigned kMaxAnimStartsPerFrame = 2;
constexpr uintptr_t kAssocHierarchy = 0x14;


struct SyncAnimGroupView {
    void* animBlock;
    void* associations;
    uint32_t numAnimations;
    int32_t idOffset;
    int32_t groupId;
};

constexpr uintptr_t kAnimAssocGroupsPtrOffset = 0x942184;
constexpr uintptr_t kNumAnimAssocDefinitionsOffset = 0x9421BC;
constexpr uintptr_t kAnimHierarchyArrayOffset = 0x93209C;
constexpr uintptr_t kNumAnimationsOffset = 0x94217C;
constexpr uintptr_t kAnimHierarchyStride = 0x18;
constexpr uintptr_t kAnimHierarchyCount = 2500;

enum class AnimStartCheck { Ok, BadId, BadGroup, GroupNotLoaded, IdOutOfRange };




using GetAnimBlockNameFn = const char* (*)(int);
using GetAnimationBlockIndexFn = int (*)(const char*);
using RequestModelFn = void (*)(int, int);
using ClumpIsInitializedFn = bool (*)(RpClump*);
GetAnimBlockNameFn g_getAnimBlockName = nullptr;
GetAnimationBlockIndexFn g_getAnimationBlockIndex = nullptr;
RequestModelFn g_requestModel = nullptr;
ClumpIsInitializedFn g_clumpIsInitialized = nullptr;
void RequestAnimBlockStreaming(int blockIndex, const char* what);

void RequestGroupAnimBlock(uint16_t group)
{
    if (!g_getAnimBlockName || !g_getAnimationBlockIndex) return;
    const char* name = g_getAnimBlockName(static_cast<int>(group));
    if (!name || !*name) return;
    const int index = g_getAnimationBlockIndex(name);
    if (index >= 0) RequestAnimBlockStreaming(index, name);
}




AnimStartCheck CheckAnimCanStart(uint16_t group, int16_t id)
{
    if (id < 0) return AnimStartCheck::BadId;
    if (!g_game) return AnimStartCheck::BadGroup;
    const int32_t groupCount = *reinterpret_cast<const int32_t*>(g_game + kNumAnimAssocDefinitionsOffset);
    if (groupCount <= 0 || groupCount > 512 || static_cast<int32_t>(group) >= groupCount) return AnimStartCheck::BadGroup;
    const auto* groups = *reinterpret_cast<SyncAnimGroupView* const*>(g_game + kAnimAssocGroupsPtrOffset);
    if (!groups) return AnimStartCheck::BadGroup;
    const SyncAnimGroupView& g = groups[group];
    if (!g.associations || !g.animBlock) return AnimStartCheck::GroupNotLoaded;
    const int32_t index = static_cast<int32_t>(id) - g.idOffset;
    if (index < 0 || static_cast<uint32_t>(index) >= g.numAnimations) return AnimStartCheck::IdOutOfRange;
    return AnimStartCheck::Ok;
}

const char* AnimStartCheckName(AnimStartCheck c)
{
    switch (c) {
        case AnimStartCheck::BadId: return "bad animation id";
        case AnimStartCheck::BadGroup: return "bad group";
        case AnimStartCheck::GroupNotLoaded: return "anim block not loaded on this phone";
        case AnimStartCheck::IdOutOfRange: return "id outside group";
        default: return "ok";
    }
}

constexpr uintptr_t kAnimBlocksArrayOffset = 0x940AFC;
constexpr uintptr_t kNumAnimBlocksOffset = 0x942180;


constexpr uintptr_t kAnimBlockStride = 0x20;
constexpr uintptr_t kAnimBlockFirstOffset = 0x14;
constexpr uintptr_t kAnimBlockCountOffset = 0x18;

int NumLoadedBlockSlots()
{
    if (!g_game) return 0;
    const int32_t n = *reinterpret_cast<const int32_t*>(g_game + kNumAnimBlocksOffset);
    return (n > 0 && n <= 512) ? n : 0;
}


int BlockOfAnimationIndex(uint32_t index)
{
    if (!g_game) return -1;
    const int blocks = NumLoadedBlockSlots();
    for (int i = 0; i < blocks; ++i) {
        const uintptr_t e = g_game + kAnimBlocksArrayOffset + static_cast<uintptr_t>(i) * kAnimBlockStride;
        const uint32_t first = *reinterpret_cast<const uint32_t*>(e + kAnimBlockFirstOffset);
        const uint32_t count = *reinterpret_cast<const uint32_t*>(e + kAnimBlockCountOffset);
        if (count && index >= first && index - first < count) return i;
    }
    return -1;
}


constexpr float kHashMarkerBase = 99.0f;
bool IsHashedState(const AnimationAssociationState& s) { return s.blendDelta >= kHashMarkerBase && s.blendDelta < kHashMarkerBase + 0.512f; }
int HashedStateBlock(const AnimationAssociationState& s) { return static_cast<int>(std::lround((s.blendDelta - kHashMarkerBase) * 1000.0f)); }







constexpr uintptr_t kStreamingInfoArrayOffset = 0x712330;
constexpr uintptr_t kStreamingInfoStride = 0x14;
constexpr uintptr_t kStreamingInfoLoadState = 0x10;
constexpr int kStreamingModelCount = 26320;
constexpr int kAnimBlockSlots = 180;
constexpr int kStreamingGameRequired = 2;
int g_ifpBase = -1;
int g_ifpSearchAttempts = 0;
uint64_t g_ifpLastSearchMs = 0;
std::unordered_map<int, uint64_t> g_animBlockRequestedAt;

uint64_t StreamClockMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

int FindIfpStreamingBase()
{
    if (!g_game) return -1;
    bool loaded[kAnimBlockSlots];
    int loadedCount = 0;
    for (int i = 0; i < kAnimBlockSlots; ++i) {
        const uintptr_t e = g_game + kAnimBlocksArrayOffset + static_cast<uintptr_t>(i) * kAnimBlockStride;
        loaded[i] = *reinterpret_cast<const uint32_t*>(e + kAnimBlockCountOffset) != 0;
        loadedCount += loaded[i] ? 1 : 0;
    }
    if (loadedCount == 0) return -1;
    int found = -1, matches = 0;
    for (int base = 0; base + kAnimBlockSlots <= kStreamingModelCount; ++base) {
        bool ok = true;
        for (int i = 0; i < kAnimBlockSlots && ok; ++i) {
            const uintptr_t info = g_game + kStreamingInfoArrayOffset + static_cast<uintptr_t>(base + i) * kStreamingInfoStride;
            ok = ((*reinterpret_cast<const uint8_t*>(info + kStreamingInfoLoadState) == 1) == loaded[i]);
        }
        if (ok) { found = base; ++matches; }
    }
    return matches == 1 ? found : -1;
}

void RequestAnimBlockStreaming(int blockIndex, const char* what)
{
    if (!g_requestModel || blockIndex < 0 || blockIndex >= kAnimBlockSlots) return;
    const uint64_t now = StreamClockMs();
    if (g_ifpBase < 0) {
        if (g_ifpSearchAttempts >= 10 || now - g_ifpLastSearchMs < 5000) return;
        g_ifpLastSearchMs = now;
        ++g_ifpSearchAttempts;
        g_ifpBase = FindIfpStreamingBase();
        if (g_ifpBase >= 0) WeaponFileLog("ANIM_STREAM ifp base=%d verified against the block table (180/180), requests enabled", g_ifpBase);
        else WeaponFileLog("ANIM_STREAM ifp base not verified (attempt %d/10), no block is requested", g_ifpSearchAttempts);
        if (g_ifpBase < 0) return;
    }
    auto it = g_animBlockRequestedAt.find(blockIndex);
    if (it != g_animBlockRequestedAt.end() && now - it->second < 3000) return;
    const bool first = (it == g_animBlockRequestedAt.end());
    g_animBlockRequestedAt[blockIndex] = now;
    g_requestModel(g_ifpBase + blockIndex, kStreamingGameRequired);
    if (first) WeaponFileLog("ANIM_STREAM requested block %d (model %d) for %s", blockIndex, g_ifpBase + blockIndex, what ? what : "?");
}

bool IsHierarchyEntry(uintptr_t h)
{
    if (!g_game) return false;
    const uintptr_t base = g_game + kAnimHierarchyArrayOffset;
    return h >= base && h + kAnimHierarchyStride <= base + kAnimHierarchyStride * kAnimHierarchyCount &&
           ((h - base) % kAnimHierarchyStride) == 0;
}


bool HierarchyLooksLoaded(uintptr_t h)
{
    if (!IsHierarchyEntry(h)) return false;
    const uint32_t hash = *reinterpret_cast<const uint32_t*>(h);
    const uint32_t sequences = *reinterpret_cast<const uint32_t*>(h + 4);
    const uint16_t sequenceCount = *reinterpret_cast<const uint16_t*>(h + 8);
    return hash != 0 && sequences != 0 && sequenceCount > 0 && sequenceCount <= 256;
}

uint32_t HierarchyHash(const void* hierarchy)
{
    const uintptr_t h = reinterpret_cast<uintptr_t>(hierarchy);
    return HierarchyLooksLoaded(h) ? *reinterpret_cast<const uint32_t*>(h) : 0;
}

void* FindHierarchyByHash(uint32_t hash)
{
    if (!g_game || hash == 0) return nullptr;
    const int32_t used = *reinterpret_cast<const int32_t*>(g_game + kNumAnimationsOffset);
    if (used <= 0 || static_cast<uintptr_t>(used) > kAnimHierarchyCount) return nullptr;
    const uintptr_t base = g_game + kAnimHierarchyArrayOffset;
    for (int32_t i = 0; i < used; ++i) {
        const uintptr_t h = base + static_cast<uintptr_t>(i) * kAnimHierarchyStride;
        if (*reinterpret_cast<const uint32_t*>(h) == hash && HierarchyLooksLoaded(h) && BlockOfAnimationIndex(static_cast<uint32_t>(i)) >= 0)
            return reinterpret_cast<void*>(h);
    }
    return nullptr;
}

void* AssocHierarchy(void* assoc)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(assoc) + kAssocHierarchy);
}

bool IsUngroupedAssociation(uint16_t group, int16_t id) { return group == 0xFFFF || id < 0; }
uint32_t StateHash(const AnimationAssociationState& s)
{
    return (static_cast<uint32_t>(s.group) << 16) | static_cast<uint16_t>(s.animationId);
}


bool IsSkippedRemoteState(const AnimationAssociationState& s)
{
    if (s.flags & kAssocMovementFlag) return true;
    if (!IsHashedState(s) && (s.animationId == kAnimationFireId || s.animationId == kAnimationCrouchFireId)) return true;
    return false;
}

bool SameSyncState(const AnimationAssociationState& a, const AnimationAssociationState& b)
{
    return a.group == b.group && a.animationId == b.animationId && IsHashedState(a) == IsHashedState(b);
}

std::unordered_set<uintptr_t> g_loggedUngroupedHierarchies;
std::unordered_set<uint64_t> g_loggedAnimEvents;

void LogUngroupedAssociation(void* assoc, uint32_t hash)
{
    if (!g_game || g_loggedUngroupedHierarchies.size() >= 48) return;
    const uintptr_t h = reinterpret_cast<uintptr_t>(AssocHierarchy(assoc));
    if (!g_loggedUngroupedHierarchies.insert(h).second) return;
    AnimationAssociationState s{};
    ReadAssociation(assoc, s);
    WeaponFileLog("ANIM_DIAG hier=0x%lx hash=%08x selftest=%d flags=0x%x blend=%.2f%s", static_cast<unsigned long>(h),
                  static_cast<unsigned>(hash), static_cast<int>(hash != 0), static_cast<unsigned>(s.flags), s.blend,
                  hash != 0 ? "" : " (cannot sync)");
}





constexpr uintptr_t kAnimBlocksWindowEnd = 0x942180;

void DumpAnimBlockTable()
{
    if (!g_game) return;
    const int32_t count = *reinterpret_cast<const int32_t*>(g_game + kNumAnimBlocksOffset);
    WeaponFileLog("ANIMBLOCK_DUMP ms_numAnimBlocks=%d window=0x%lx bytes=%lu", count,
                  static_cast<unsigned long>(kAnimBlocksArrayOffset), static_cast<unsigned long>(kAnimBlocksWindowEnd - kAnimBlocksArrayOffset));
    const uint8_t* base = reinterpret_cast<const uint8_t*>(g_game + kAnimBlocksArrayOffset);
    const size_t size = kAnimBlocksWindowEnd - kAnimBlocksArrayOffset;
    for (size_t off = 0; off < size; off += 16) {
        char hex[16 * 3 + 1] = {0};
        char ascii[17] = {0};
        for (size_t i = 0; i < 16; ++i) {
            const uint8_t b = base[off + i];
            std::snprintf(hex + i * 3, 4, "%02x ", b);
            ascii[i] = (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
        }
        WeaponFileLog("ANIMBLOCK_DUMP +%04lx %s|%s|", static_cast<unsigned long>(off), hex, ascii);
    }
    WeaponFileLog("ANIMBLOCK_DUMP end");
}


uint8_t CaptureActiveAssociations(CPed* ped, AnimationAssociationState* out)
{
    if (!ped || !ped->m_pRwClump || !g_getFirstAssociation || !g_getNextAssociation || !out) return 0;
    if (g_clumpIsInitialized && !g_clumpIsInitialized(ped->m_pRwClump)) return 0;
    static bool blockTableDumped = false;
    if (!blockTableDumped) {
        blockTableDumped = true;
        DumpAnimBlockTable();
    }
    uint8_t n = 0;
    unsigned walked = 0;
    for (void* a = g_getFirstAssociation(ped->m_pRwClump); a && n < 64 && walked < kAssocWalkLimit;
         a = g_getNextAssociation(a), ++walked) {
        AnimationAssociationState x{};
        ReadAssociation(a, x);
        if (x.blend <= 0.001f) continue;
        if (!(std::isfinite(x.blend) && std::isfinite(x.blendDelta) && std::isfinite(x.currentTime) &&
              std::isfinite(x.speed) && std::isfinite(x.timeStep))) continue;
        if (IsUngroupedAssociation(x.group, x.animationId)) {
            const uint32_t hash = HierarchyHash(AssocHierarchy(a));
            LogUngroupedAssociation(a, hash);
            if (!hash) continue;
            const uintptr_t h = reinterpret_cast<uintptr_t>(AssocHierarchy(a));
            const uintptr_t base = g_game + kAnimHierarchyArrayOffset;
            if (!IsHierarchyEntry(h)) continue;
            const int block = BlockOfAnimationIndex(static_cast<uint32_t>((h - base) / kAnimHierarchyStride));
            if (block < 0) continue;
            x.group = static_cast<uint16_t>(hash >> 16);
            x.animationId = static_cast<int16_t>(hash & 0xFFFF);
            x.blendDelta = kHashMarkerBase + static_cast<float>(block) * 0.001f;
        }
        out[n++] = x;
    }
    return n;
}


void* FindRemoteAssociation(RpClump* clump, const AnimationAssociationState& s)
{
    if (!clump || !g_getFirstAssociation || !g_getNextAssociation) return nullptr;
    void* hierarchy = nullptr;
    if (IsHashedState(s)) {
        hierarchy = FindHierarchyByHash(StateHash(s));
        if (!hierarchy) return nullptr;
    }
    unsigned walked = 0;
    for (void* a = g_getFirstAssociation(clump); a && walked < kAssocWalkLimit; a = g_getNextAssociation(a), ++walked) {
        if (hierarchy) {
            if (AssocHierarchy(a) == hierarchy) return a;
        } else {
            AnimationAssociationState cur{};
            ReadAssociation(a, cur);
            if (cur.group == s.group && cur.animationId == s.animationId) return a;
        }
    }
    return nullptr;
}

void ApplyAssociationStates(CPed* ped, const RemotePlayer& player, RemoteVisualState& visual)
{
    if (!ped || !ped->m_pRwClump || !g_getFirstAssociation || !g_getNextAssociation || !g_setAssocBlend || !g_setAssocTime) return;
    RpClump* clump = ped->m_pRwClump;
    if (g_clumpIsInitialized && !g_clumpIsInitialized(clump)) return;

    if (ped->m_fHealth <= 0.0f) return;
    unsigned startsThisFrame = 0;
    const unsigned n = std::min<unsigned>(player.animationAssociationCount, 64);
    const auto* states = player.animationAssociations.data();

    for (unsigned i = 0; i < n; ++i) {
        const AnimationAssociationState& s = states[i];
        if (IsSkippedRemoteState(s)) continue;
        void* assoc = FindRemoteAssociation(clump, s);
        if (!assoc) {
            if (IsHashedState(s)) {
                void* hierarchy = FindHierarchyByHash(StateHash(s));
                if (!hierarchy) {
                    const int block = HashedStateBlock(s);
                    if (block >= 0 && block < NumLoadedBlockSlots()) RequestAnimBlockStreaming(block, "hash-identified animation");
                    continue;
                }
                if (!g_blendAnimationHierarchy || startsThisFrame >= kMaxAnimStartsPerFrame) continue;
                ++startsThisFrame;

                if (g_loggedAnimEvents.insert((5ull << 32) | StateHash(s)).second)
                    WeaponFileLog("ANIM_SYNC starting hash=%08x flags=0x%x", static_cast<unsigned>(StateHash(s)), static_cast<unsigned>(s.flags));
                assoc = g_blendAnimationHierarchy(clump, hierarchy, static_cast<int>(s.flags), kAssocFadeRate);
                if (!assoc) continue;
                if (g_loggedAnimEvents.insert((2ull << 32) | StateHash(s)).second)
                    WeaponFileLog("ANIM_SYNC started hash=%08x on remote ped", static_cast<unsigned>(StateHash(s)));
            } else {
                if (!g_blendAnimation) continue;
                const AnimStartCheck check = CheckAnimCanStart(s.group, s.animationId);
                const uint64_t key = (static_cast<uint64_t>(s.group) << 16) | static_cast<uint16_t>(s.animationId);
                if (check == AnimStartCheck::GroupNotLoaded) {
                    RequestGroupAnimBlock(s.group);
                    continue;
                }
                if (check != AnimStartCheck::Ok) {
                    if (g_loggedAnimEvents.insert((3ull << 32) | key).second)
                        WeaponFileLog("ANIM_SYNC skipped group=%u id=%d (%s)", static_cast<unsigned>(s.group),
                                      static_cast<int>(s.animationId), AnimStartCheckName(check));
                    continue;
                }
                if (startsThisFrame >= kMaxAnimStartsPerFrame) continue;
                ++startsThisFrame;
                if (g_loggedAnimEvents.insert((6ull << 32) | key).second)
                    WeaponFileLog("ANIM_SYNC starting group=%u id=%d", static_cast<unsigned>(s.group), static_cast<int>(s.animationId));
                assoc = g_blendAnimation(clump, static_cast<int>(s.group), static_cast<int>(s.animationId), kAssocFadeRate);
                if (!assoc) continue;
                if (g_loggedAnimEvents.insert((4ull << 32) | key).second)
                    WeaponFileLog("ANIM_SYNC started group=%u id=%d on remote ped", static_cast<unsigned>(s.group),
                                  static_cast<int>(s.animationId));
            }
        }
        AnimationAssociationState cur{};
        ReadAssociation(assoc, cur);
        if (std::fabs(cur.currentTime - s.currentTime) > 0.15f) g_setAssocTime(assoc, s.currentTime);
        *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(assoc) + kAssocSpeed) = s.speed;
        g_setAssocBlend(assoc, s.blend, kAssocFadeRate);
    }


    const unsigned oldCount = std::min<unsigned>(visual.animationAssociationCount, 64);
    for (unsigned i = 0; i < oldCount; ++i) {
        const AnimationAssociationState& old = visual.animationAssociations[i];
        if (IsSkippedRemoteState(old)) continue;
        bool stillActive = false;
        for (unsigned k = 0; k < n && !stillActive; ++k) stillActive = SameSyncState(old, states[k]);
        if (stillActive) continue;
        if (void* stale = FindRemoteAssociation(clump, old)) g_setAssocBlend(stale, 0.0f, -kAssocFadeRate);
    }
    visual.animationAssociationCount = static_cast<uint8_t>(n);
    for (unsigned i = 0; i < n; ++i) visual.animationAssociations[i] = states[i];

    if (std::isfinite(player.animation.aimYaw)) ped->m_fAimingRotation = player.animation.aimYaw;
    if (std::isfinite(player.animation.aimPitch)) ped->m_pedIK.m_TorsoOrien.m_fPitch = player.animation.aimPitch;
}













constexpr bool kBlockRemotePedEvents = true;
constexpr bool kBlockRemotePedTasks = true;

using ScanForEventsFn = void (*)(void*, CPed*);
using IntelligenceFn = void (*)(void*);
ScanForEventsFn g_originalScanForEvents = nullptr;
IntelligenceFn g_originalProcessEventHandler = nullptr;
IntelligenceFn g_originalIntelligenceProcess = nullptr;

uint64_t NowSteadyMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool IsRemotePlayerPed(const CPed* ped)
{
    if (!ped) return false;
    for (const auto& entry : g_remotePeds)
        if (entry.second == ped) return true;
    return false;
}


CPed* OwnerOfIntelligence(const void* intelligence)
{
    return intelligence ? *reinterpret_cast<CPed* const*>(intelligence) : nullptr;
}

void HookScanForEvents(void* scanner, CPed* ped)
{
    if (kBlockRemotePedEvents && IsRemotePlayerPed(ped)) return;
    if (g_originalScanForEvents) g_originalScanForEvents(scanner, ped);
}

void HookProcessEventHandler(void* intelligence)
{
    CPed* owner = OwnerOfIntelligence(intelligence);
    if (kBlockRemotePedEvents && IsRemotePlayerPed(owner)) return;
    if (g_originalProcessEventHandler) g_originalProcessEventHandler(intelligence);
}

void HookIntelligenceProcess(void* intelligence)
{
    CPed* owner = OwnerOfIntelligence(intelligence);
    if (kBlockRemotePedTasks && IsRemotePlayerPed(owner) && owner && owner->m_fHealth > 0.0f) {
        const uint64_t now = NowSteadyMs();
        auto permit = g_remoteTaskPermitUntilMs.find(owner);
        if (permit != g_remoteTaskPermitUntilMs.end() && now < permit->second) {


            if (g_originalIntelligenceProcess) g_originalIntelligenceProcess(intelligence);
            return;
        }
        if (permit != g_remoteTaskPermitUntilMs.end()) g_remoteTaskPermitUntilMs.erase(permit);



        if (GetSeatedVehicle(owner)) {
            if (g_originalIntelligenceProcess) g_originalIntelligenceProcess(intelligence);
        }
        return;
    }
    if (g_originalIntelligenceProcess) g_originalIntelligenceProcess(intelligence);
}

void InstallRemotePedAiHooks()
{
    void* lib = aml->GetLibHandle("libGTASA.so");
    if (!lib) return;
    auto scan = aml->GetSym(lib, "_ZN13CEventScanner13ScanForEventsER4CPed");
    auto events = aml->GetSym(lib, "_ZN16CPedIntelligence19ProcessEventHandlerEv");
    auto process = aml->GetSym(lib, "_ZN16CPedIntelligence7ProcessEv");
    const bool scanHooked = scan && aml->Hook(reinterpret_cast<void*>(scan), reinterpret_cast<void*>(HookScanForEvents),
                                              reinterpret_cast<void**>(&g_originalScanForEvents));
    const bool eventsHooked = events && aml->Hook(reinterpret_cast<void*>(events), reinterpret_cast<void*>(HookProcessEventHandler),
                                                  reinterpret_cast<void**>(&g_originalProcessEventHandler));
    const bool processHooked = process && aml->Hook(reinterpret_cast<void*>(process), reinterpret_cast<void*>(HookIntelligenceProcess),
                                                    reinterpret_cast<void**>(&g_originalIntelligenceProcess));
    WeaponFileLog("REMOTE_AI hooks: scanForEvents=%d eventHandler=%d process=%d (blockEvents=%d blockAmbientTasks=%d directVehicleWarp=1)",
                  scanHooked, eventsHooked, processHooked, kBlockRemotePedEvents, kBlockRemotePedTasks);
}




CVector RemoteAimTarget(const RemotePlayer& player)
{
    float yaw = std::isfinite(player.animation.aimYaw) ? player.animation.aimYaw : player.heading * 0.01745329252f;
    float pitch = std::isfinite(player.animation.aimPitch) ? player.animation.aimPitch : 0.0f;


    if (std::fabs(yaw) > 6.2831853f && std::fabs(yaw) <= 360.0f) yaw *= 0.01745329252f;
    if (std::fabs(pitch) > 6.2831853f && std::fabs(pitch) <= 180.0f) pitch *= 0.01745329252f;
    yaw = std::max(-100.0f, std::min(100.0f, yaw));
    pitch = std::max(-1.45f, std::min(1.45f, pitch));
    constexpr float kAimDistance = 30.0f;
    const float cp = std::cos(pitch);
    return CVector(player.x - std::sin(yaw) * cp * kAimDistance,
                   player.y + std::cos(yaw) * cp * kAimDistance,
                   player.z + 0.75f - std::sin(pitch) * kAimDistance);
}

bool RunRemotePedTask(CPed* ped, const SCRIPT_COMMAND* command, const CVector& target, int durationMs)
{
    if (!ped || !g_saUtils || durationMs <= 0) return false;
    const int pedRef = CPools::GetPedRef(ped);
    if (pedRef <= 0 || CPools::GetPed(pedRef) != ped) return false;
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) return false;
    g_remoteTaskPermitUntilMs[ped] = NowSteadyMs() + static_cast<uint64_t>(durationMs + 220);
    return g_saUtils->ScriptCommand(command, pedRef, target.x, target.y, target.z, durationMs);
}

void UpdateRemoteAimAndShooting(CPed* ped, const RemotePlayer& player, RemoteVisualState& visual)
{
    if (!ped || ped->m_pVehicle || player.vehicleId || ped->m_fHealth <= 0.0f) return;
    const AnimationProfile& profile = player.animation;


    if (std::isfinite(profile.aimYaw)) ped->m_fAimingRotation = profile.aimYaw;
    if (std::isfinite(profile.aimPitch)) ped->m_pedIK.m_TorsoOrien.m_fPitch = profile.aimPitch;
    const uint64_t now = NowSteadyMs();
    const bool firing = (profile.flags & ANIM_FIRING) != 0 ||
                        profile.weaponState == static_cast<uint8_t>(WEAPONSTATE_FIRING);
    const bool aiming = (profile.flags & ANIM_AIMING) != 0 || firing;
    const CVector target = RemoteAimTarget(player);


    if (firing && now - visual.lastShootTaskMs >= 300) {
        if (RunRemotePedTask(ped, &kScmTaskShootAtCoord, target, 450)) {
            visual.lastShootTaskMs = now;
            visual.shootingFrames = std::max<uint8_t>(visual.shootingFrames, 5);
        }
    } else if (aiming && now - visual.lastAimTaskMs >= 250 && !firing) {
        if (RunRemotePedTask(ped, &kScmTaskAimGunAtCoord, target, 350))
            visual.lastAimTaskMs = now;
    }
}



constexpr int kRemoteShootingFrames = 5;
constexpr int kRemoteFireAnimFrames = 14;
constexpr int kRemoteGunFlashLifetime = 100;
constexpr float kRemoteFireBlendRate = 8.0f;

bool CaptureFireAssociation(CPed* ped, uint16_t& group, uint16_t& animId)
{
    group = 0;
    animId = 0;
    if (!ped || !ped->m_pRwClump || !g_getFirstAssociation || !g_getNextAssociation) return false;
    for (void* a = g_getFirstAssociation(ped->m_pRwClump); a; a = g_getNextAssociation(a)) {
        AnimationAssociationState state{};
        ReadAssociation(a, state);
        if (!(state.flags & 1) || state.blend <= 0.001f || state.group == 0) continue;
        if (state.animationId == kAnimationFireId || state.animationId == kAnimationCrouchFireId) {
            group = state.group;
            animId = static_cast<uint16_t>(state.animationId);
            return true;
        }
    }
    return false;
}

void FadeRemoteFireAnimation(RemoteVisualState& visual, CPed* ped)
{
    if (visual.fireAnimGroup != 0 && ped && ped->m_pRwClump && g_getFirstAssociation && g_getNextAssociation) {
        for (void* a = g_getFirstAssociation(ped->m_pRwClump); a; a = g_getNextAssociation(a)) {
            AnimationAssociationState state{};
            ReadAssociation(a, state);
            if (state.group == visual.fireAnimGroup && state.animationId == visual.fireAnimId)
                *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(a) + kAssocBlendDelta) = -kRemoteFireBlendRate;
        }
    }
    visual.fireAnimGroup = 0;
    visual.fireAnimId = 0;
    visual.fireAnimFrames = 0;
}

bool IsSupportedNpcModel(int modelId)
{

    return modelId == 7;
}


bool WeaponModelResident(int modelId)
{
    if (modelId <= 0) return true;
    if (modelId >= 30000) return false;
    CBaseModelInfo** infos = CModelInfo::ms_modelInfoPtrs;
    return infos && infos[modelId] && infos[modelId]->m_pRwObject != nullptr;
}


uint64_t g_weaponStreamRetryAtMs[256]{};
bool g_weaponNotReadyLogged[256]{};






bool WeaponModelsReadyForHand(const CWeaponInfo* info, uint8_t weaponType)
{
    const int ids[2] = {info->m_nModelId, info->m_nModelId2};
    if (WeaponModelResident(ids[0]) && WeaponModelResident(ids[1])) {
        g_weaponNotReadyLogged[weaponType] = false;
        return true;
    }
    const uint64_t now = SteadyNowMs();
    if (now < g_weaponStreamRetryAtMs[weaponType]) return false;
    for (int id : ids) if (id > 0 && id < 30000) CStreaming::RequestModel(id, 2);
    CStreaming::LoadAllRequestedModels(false);
    if (WeaponModelResident(ids[0]) && WeaponModelResident(ids[1])) {
        g_weaponStreamRetryAtMs[weaponType] = 0;
        g_weaponNotReadyLogged[weaponType] = false;
        return true;
    }
    g_weaponStreamRetryAtMs[weaponType] = now + 250;
    if (!g_weaponNotReadyLogged[weaponType]) {
        g_weaponNotReadyLogged[weaponType] = true;
        WeaponFileLog("WEAPON_MODEL_NOT_READY weapon=%u model=%d loaded=%u model2=%d loaded2=%u (retrying)",
                      static_cast<unsigned>(weaponType), ids[0], WeaponModelResident(ids[0]) ? 1u : 0u,
                      ids[1], WeaponModelResident(ids[1]) ? 1u : 0u);
    }
    return false;
}

bool ApplyRemoteWeaponSaMpStyle(CPed* ped, uint8_t weaponType)
{





    if (!ped || !g_saUtils || weaponType == 0 || weaponType >= 47) return false;
    const int pedRef = CPools::GetPedRef(ped);
    if (pedRef <= 0 || CPools::GetPed(pedRef) != ped) return false;

    g_scmWeaponModelOutput = 0xFFFFFFFFu;
    g_saUtils->ScriptCommand(&kScmGetWeaponTypeModel,
                             static_cast<int>(weaponType), &g_scmWeaponModelOutput);
    if (g_scmWeaponModelOutput == 0xFFFFFFFFu) {
        WeaponFileLog("WEAPON_SAMP_MODEL_QUERY_FAIL weapon=%u pedRef=%d",
                      static_cast<unsigned>(weaponType), pedRef);
        return false;
    }
    const int weaponModel = static_cast<int>(g_scmWeaponModelOutput);
    if (weaponModel > 0 && weaponModel < 30000 &&
        !g_saUtils->ScriptCommand(&kScmHasModelLoaded, weaponModel)) {
        g_saUtils->ScriptCommand(&kScmRequestModel, weaponModel);
        g_saUtils->ScriptCommand(&kScmLoadAllRequestedModels);
    }
    if (weaponModel > 0 && weaponModel < 30000 &&
        !g_saUtils->ScriptCommand(&kScmHasModelLoaded, weaponModel)) {
        WeaponFileLog("WEAPON_SAMP_MODEL_WAIT weapon=%u model=%d; will retry",
                      static_cast<unsigned>(weaponType), weaponModel);
        return false;
    }

    g_saUtils->ScriptCommand(&kScmGiveWeaponToChar, pedRef,
                             static_cast<int>(weaponType), 9999);
    g_saUtils->ScriptCommand(&kScmSetCurrentCharWeapon, pedRef,
                             static_cast<int>(weaponType));

    const unsigned selectedSlot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    const unsigned selectedWeapon = selectedSlot < 13
        ? static_cast<unsigned>(ped->m_aWeapons[selectedSlot].m_eWeaponType) : 0xFFu;
    if (selectedWeapon != weaponType) {
        WeaponFileLog("WEAPON_SAMP_VERIFY_FAIL requested=%u pedRef=%d slot=%u actual=%u",
                      static_cast<unsigned>(weaponType), pedRef, selectedSlot, selectedWeapon);
        return false;
    }
    WeaponFileLog("WEAPON_SAMP_APPLY weapon=%u model=%d pedRef=%d",
                  static_cast<unsigned>(weaponType), weaponModel, pedRef);
    return true;
}

bool ApplyRemoteWeapon(CPed* ped, uint8_t weaponType)
{
    if (!ped || (weaponType > 0x2E && weaponType != 0x3A)) return false;
    if (weaponType == 0) {



        ped->ClearWeapons();
        ped->m_nSelectedWepSlot = 0;
        if (g_setCurrentWeapon)
            g_setCurrentWeapon(reinterpret_cast<void*>(ped), WEAPONTYPE_UNARMED);
        if (g_saUtils) {
            const int pedRef = CPools::GetPedRef(ped);
            if (pedRef > 0) g_saUtils->ScriptCommand(&kScmSetCurrentCharWeapon, pedRef, 0);
        }
        WeaponFileLog("WEAPON_PED_APPLY id=unarmed");
        logger->Info("WEAPON_PED_APPLY id=unarmed");
        return true;
    }




    if (weaponType < 47 && ApplyRemoteWeaponSaMpStyle(ped, weaponType)) {
        logger->Info("Remote weapon %u applied through SA-MP-compatible SCM path",
                     static_cast<unsigned>(weaponType));
        return true;
    }



    auto* info = CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(weaponType), 0);
    if (!info || info->m_nSlot >= 13) return false;
    if (!g_giveWeapon || !g_setCurrentWeapon) return false;
    if (!WeaponModelsReadyForHand(info, weaponType)) return false;
    g_giveWeapon(reinterpret_cast<void*>(ped), static_cast<eWeaponType>(weaponType), 9999, true);
    g_setCurrentWeapon(reinterpret_cast<void*>(ped), static_cast<eWeaponType>(weaponType));
    const unsigned selectedSlot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    const unsigned selectedWeapon = selectedSlot < 13
        ? static_cast<unsigned>(ped->m_aWeapons[selectedSlot].m_eWeaponType) : 0xFFu;
    if (selectedWeapon != weaponType) {
        WeaponFileLog("WEAPON_NATIVE_VERIFY_FAIL requested=%u slot=%u actual=%u",
                      static_cast<unsigned>(weaponType), selectedSlot, selectedWeapon);
        logger->Error("Remote weapon %u did not become active (slot %u contains %u); will retry",
                      static_cast<unsigned>(weaponType), selectedSlot, selectedWeapon);
        return false;
    }
    WeaponFileLog("WEAPON_NATIVE_FALLBACK_APPLY weapon=%u", static_cast<unsigned>(weaponType));
    logger->Info("Remote weapon %u applied through native fallback", static_cast<unsigned>(weaponType));
    return true;
}

void ApplyRemoteVitals(CPed* ped, uint16_t health, uint16_t armour)
{
    if (!ped) return;
    ped->SetHealthSafe(static_cast<float>(health));
    ped->m_fArmour = static_cast<float>(armour);
}

void ExecuteRemoteShot(const RemoteShot& shot, CPed* ped)
{
    if (!ped) return;
    RemoteVisualState& visual = g_remoteVisuals[shot.id];

    if (g_doGunFlash) g_doGunFlash(reinterpret_cast<void*>(ped), kRemoteGunFlashLifetime, false);
    if (shot.animGroup != 0 && shot.animId != 0 && g_blendAnimation && ped->m_pRwClump) {
        g_blendAnimation(ped->m_pRwClump, shot.animGroup, shot.animId, kRemoteFireBlendRate);
        visual.fireAnimGroup = shot.animGroup;
        visual.fireAnimId = static_cast<int16_t>(shot.animId);
        visual.fireAnimFrames = kRemoteFireAnimFrames;
    } else if (g_setMoveState && g_setMoveAnim) {
        g_setMoveState(ped, static_cast<int>(kMoveStill));
        g_setMoveAnim(ped);
    }
    visual.shootingFrames = kRemoteShootingFrames;

    const unsigned slot = static_cast<unsigned>(ped->m_nSelectedWepSlot);
    if (slot >= 13 || static_cast<uint8_t>(ped->m_aWeapons[slot].m_eWeaponType) != shot.weapon) {
        WeaponFileLog("REMOTE_SHOT_VISUAL_ONLY id=%u weapon=%u reason=weapon-not-active", shot.id, shot.weapon);
        return;
    }





    const uint64_t now = NowSteadyMs();
    if (now - visual.lastShootTaskMs >= 300) {
        CVector target(shot.x + shot.dx * 60.0f,
                       shot.y + shot.dy * 60.0f,
                       shot.z + shot.dz * 60.0f);
        if (RunRemotePedTask(ped, &kScmTaskShootAtCoord, target, 450)) {
            visual.lastShootTaskMs = now;
            logger->Info("Remote player %u GTA shoot task scheduled weapon=%u", shot.id, shot.weapon);
        } else {
            logger->Info("Remote player %u shot visible but GTA shoot task deferred", shot.id);
        }
    }
    WeaponFileLog("REMOTE_SHOT_TASK id=%u weapon=%u fire=%u/%u", shot.id, shot.weapon, shot.animGroup, shot.animId);
}



void CaptureAndSendLocalBones(CPed* ped)
{
    if(!g_client || !ped) return;
    static NetworkBone bones[19];
    uint8_t count=0;
    for(uint8_t i=0;i<19;++i){
        auto* b=ped->m_apBones[i];
        if(!b || !b->pStdKeyFrame) continue;
        auto q=b->pStdKeyFrame->q;
        if(!std::isfinite(q.imag.x)||!std::isfinite(q.imag.y)||!std::isfinite(q.imag.z)||!std::isfinite(q.real)) continue;
        bones[count].index=i; bones[count].qx=q.imag.x; bones[count].qy=q.imag.y; bones[count].qz=q.imag.z; bones[count].qw=q.real; ++count;
    }
    g_client->SendBoneSnapshot(bones,count);
}

void ApplyRemoteBones(CPed* ped, const RemotePlayer& player)
{
    if(!ped || player.boneCount==0) return;
    for(uint8_t i=0;i<player.boneCount;++i){
        const auto& n=player.bones[i]; if(n.index>=19) continue;
        auto* b=ped->m_apBones[n.index]; if(!b || !b->pStdKeyFrame) continue;
        b->pStdKeyFrame->q.imag.x=n.qx; b->pStdKeyFrame->q.imag.y=n.qy; b->pStdKeyFrame->q.imag.z=n.qz; b->pStdKeyFrame->q.real=n.qw;
    }
    ped->UpdateRpHAnim();
}









struct RetiredPedRegistry {
    std::unordered_map<uint32_t, bool> refs;
    void Add(uint32_t ref) { if (ref) refs[ref] = false; }
    bool Empty() const { return refs.empty(); }


    bool Check(uint32_t ref, bool* firstSkip) {
        auto it = refs.find(ref);
        if (it == refs.end()) return false;
        *firstSkip = !it->second;
        it->second = true;
        return true;
    }

    void Prune(const std::unordered_map<uint32_t, bool>& present) {
        for (auto it = refs.begin(); it != refs.end();) {
            if (present.count(it->first)) ++it;
            else it = refs.erase(it);
        }
    }
};
RetiredPedRegistry g_retiredPeds;



void RetireRemotePed(CPed* ped)
{
    if (!ped) return;
    g_retiredPeds.Add(static_cast<uint32_t>(CPools::GetPedRef(ped)));
    g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(ped));
}



static const SCRIPT_COMMAND kScmDeleteChar = { 0x009B, "i" };
static const SCRIPT_COMMAND kScmRemoveCharFromCarMaintainPosition = { 0x09C9, "i" };










void DiscardRemotePed(CPed* ped, const char* why)
{
    if (!ped || !g_saUtils) return;
    const int ref = CPools::GetPedRef(ped);
    if (!ref) { RetireRemotePed(ped); return; }
    g_retiredPeds.Add(static_cast<uint32_t>(ref));
    g_saUtils->ScriptCommand(&kScmDeleteChar, ref);


    if (CPed* stillAlive = CPools::GetPed(ref)) {
        logger->Info("DiscardRemotePed(%s) ref=%d: DELETE_CHAR had no effect, falling back to MarkEntityAsNotNeeded", why, ref);
        g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(stillAlive));
    } else {
        logger->Info("DiscardRemotePed(%s) ref=%d: seated ghost ped deleted", why, ref);
    }
}


void SyncLocalMissionMarkers()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;




    static std::array<bool, 175> lastSeen{};
    std::array<bool, 175> seen{};

    for (uint16_t i = 0; i < 175; ++i) {
        const auto& trace = CRadar::ms_RadarTrace[i];
        if (!trace.m_bInUse ||
            (trace.m_nBlipDisplay != BLIP_DISPLAY_MARKER_ONLY &&
             trace.m_nBlipDisplay != BLIP_DISPLAY_BOTH))
            continue;

        switch (trace.m_nBlipType) {
            case BLIP_CHAR:
            case BLIP_OBJECT:
            case BLIP_COORD:
            case BLIP_CONTACTPOINT:
                break;
            default:
                continue;
        }

        if (!std::isfinite(trace.m_vecPos.x) ||
            !std::isfinite(trace.m_vecPos.y) ||
            !std::isfinite(trace.m_vecPos.z))
            continue;

        seen[i] = true;
        g_client->SendMissionMarker(
            i,
            static_cast<uint8_t>(trace.m_nBlipType),
            static_cast<uint8_t>(trace.m_nBlipDisplay),
            static_cast<uint8_t>(trace.m_nRadarSprite),
            trace.m_nBlipSize,
            trace.m_nColour,
            trace.m_vecPos.x, trace.m_vecPos.y, trace.m_vecPos.z,
            trace.m_fSphereRadius);
    }

    for (uint16_t i = 0; i < 175; ++i) {
        if (lastSeen[i] && !seen[i])
            g_client->SendMissionMarkerRemove(i);
    }
    lastSeen = seen;
}

void UpdateRemoteMissionMarkers()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;

    for (const auto id : g_client->DrainRemovedMissionMarkers()) {
        auto it = g_remoteMissionMarkerBlips.find(id);
        if (it != g_remoteMissionMarkerBlips.end()) {
            if (it->second >= 0)
                CRadar::ClearBlip(it->second);
            g_remoteMissionMarkerBlips.erase(it);
        }
    }

    for (const auto& marker : g_client->GetRemoteMissionMarkers()) {
        if (marker.ownerId == g_client->LocalId()) continue;

        auto it = g_remoteMissionMarkerBlips.find(marker.id);
        int handle = (it == g_remoteMissionMarkerBlips.end()) ? -1 : it->second;

        if (handle < 0 || !CRadar::ValidBlipHandle(static_cast<uint32_t>(handle))) {
            const CVector pos(marker.x, marker.y, marker.z);
            handle = CRadar::SetCoordBlip(
                BLIP_COORD,
                pos,
                marker.color,
                BLIP_DISPLAY_MARKER_ONLY,
                nullptr);
            if (handle < 0) continue;
            g_remoteMissionMarkerBlips[marker.id] = handle;
            it = g_remoteMissionMarkerBlips.find(marker.id);
        }

        auto& trace = CRadar::GetBlipFromHandle(static_cast<uint32_t>(handle));
        trace.m_eBlipType = BLIP_COORD;
        trace.m_vecPos = CVector(marker.x, marker.y, marker.z);
        trace.m_nColour = marker.color;
        trace.m_bInUse = 1;
        trace.m_bBlipRemain = 1;
        trace.m_nBlipDisplay = BLIP_DISPLAY_MARKER_ONLY;
        trace.m_nBlipType = BLIP_COORD;
        trace.m_nRadarSprite = marker.sprite;
        trace.m_nBlipSize = marker.scale;
        trace.m_fSphereRadius = marker.radius;
    }
}

void TrackLocalVehicleEntity(CVehicle* vehicle);

void SyncLocalNpcs()
{
    if(!g_client || !g_saUtils || !g_saUtils->IsGameInitialised() || !CPools::ms_pPedPool) return;
    CPlayerPed* local=FindPlayerPed(-1); if(!local) return;
    const uint32_t localRef = static_cast<uint32_t>(CPools::GetPedRef(local));
    const CVector lp=local->GetPosition();
    std::unordered_map<uint32_t,bool> seen;
    struct LocalNpcState { uint32_t ref=0, vehicleKey=0; uint16_t model=0, health=100, armour=0, vehicleNetworkId=0; float x=0,y=0,z=0,heading=0; uint8_t weapon=0,move=1,seat=0; char specialName[16]{}; };
    std::vector<LocalNpcState> states;
    std::unordered_map<uint32_t,CVehicle*> occupiedVehicles;
    auto* pool=CPools::ms_pPedPool;
    const int max=std::min(pool->m_nSize, 256);
    const bool trackRetired=!g_retiredPeds.Empty();
    std::unordered_map<uint32_t,bool> presentPedRefs;
    for(int i=0;i<max;++i){
        CPed* ped=pool->GetAt(i); if(!ped) continue;
        if(trackRetired){
            const uint32_t poolRef=(uint32_t)CPools::GetPedRef(ped);
            presentPedRefs[poolRef]=true;
            bool firstSkip=false;
            if(g_retiredPeds.Check(poolRef,&firstSkip)){
                if(firstSkip) logger->Info("RETIRED_PED_NOT_SYNCED_AS_NPC ref=%u (abandoned remote-player ped still in the ped pool)", poolRef);
                continue;
            }
        }
        if(ped==local) continue;
        bool remote=false; for(auto& kv:g_remotePeds) if(kv.second==ped){remote=true;break;} if(remote) continue;
        for(auto& kv:g_remoteNpcs) if(kv.second==ped){remote=true;break;} if(remote) continue;
        bool test=false; for(auto* n:g_spawnedNpcs) if(n==ped){test=true;break;} if(test) continue;
        const CVector pos=ped->GetPosition(); const float dx=pos.x-lp.x,dy=pos.y-lp.y,dz=pos.z-lp.z; if(dx*dx+dy*dy+dz*dz>80.0f*80.0f) continue;
        const uint32_t ref=(uint32_t)CPools::GetPedRef(ped); if(!ref) continue; seen[ref]=true;
        uint8_t weapon=0; unsigned slot=(unsigned)ped->m_nSelectedWepSlot; if(slot<13) weapon=(uint8_t)ped->m_aWeapons[slot].m_eWeaponType;
        LocalNpcState state{}; state.ref=ref; state.model=(uint16_t)ped->m_nModelIndex; const std::string specialName=StableSpecialNameForPed(ped, state.model); std::snprintf(state.specialName,sizeof(state.specialName),"%s",specialName.c_str()); state.x=pos.x; state.y=pos.y; state.z=pos.z; state.heading=HeadingDegrees(ped); state.health=(uint16_t)std::max(0.f,std::min(100.f,ped->GetHealth())); state.armour=(uint16_t)std::max(0.f,std::min(100.f,ped->m_fArmour)); state.weapon=weapon; state.move=(uint8_t)std::max(1,std::min(7,(int)ped->m_eMoveStateAnim));
        if(CVehicle* vehicle=ped->m_pVehicle){
            TrackLocalVehicleEntity(vehicle);
            uint16_t remoteVehicleId=0; for(const auto& remote:g_remoteVehicles) if(remote.second==vehicle){remoteVehicleId=remote.first;break;}
            if(remoteVehicleId) state.vehicleNetworkId=remoteVehicleId;
            else {state.vehicleKey=(uint32_t)reinterpret_cast<uintptr_t>(vehicle);occupiedVehicles[state.vehicleKey]=vehicle;}
            if(vehicle->m_pDriver==ped) state.seat=0;
            else for(uint8_t si=0;si<8;++si) if(vehicle->m_apPassengers[si]==ped){state.seat=(uint8_t)(si+1);break;}
        }
        states.push_back(state);
    }
    if(trackRetired && pool->m_nSize<=256) g_retiredPeds.Prune(presentPedRefs);



    for (auto it = g_localSpecialIdentities.begin(); it != g_localSpecialIdentities.end();) {
        if (it->first != localRef && !seen.count(it->first)) it = g_localSpecialIdentities.erase(it);
        else ++it;
    }

    std::unordered_map<uint32_t,bool> currentVehicleKeys;
    for(const auto& entry:occupiedVehicles){
        CVehicle* vehicle=entry.second; if(!vehicle) continue;
        currentVehicleKeys[entry.first]=true;
        const CVector& pos=vehicle->GetPosition(); const CVector& speed=reinterpret_cast<CPhysical*>(vehicle)->m_vecMoveSpeed;
        RemoteVehicle state{}; state.modelId=static_cast<uint16_t>(vehicle->m_nModelIndex); state.x=pos.x; state.y=pos.y; state.z=pos.z; state.heading=HeadingDegrees(vehicle); state.vx=speed.x; state.vy=speed.y; state.vz=speed.z; state.health=vehicle->m_fHealth;
        state.primaryColor=vehicle->m_nPrimaryColor; state.secondaryColor=vehicle->m_nSecondaryColor; state.engineOn=vehicle->bEngineOn?1:0; state.lightsOn=vehicle->bLightsOn?1:0; state.sirenOn=vehicle->bSirenOrAlarm?1:0; state.doorLock=static_cast<uint8_t>(vehicle->m_eDoorLock); state.dirt=std::max(0.0f,std::min(15.0f,vehicle->m_fDirtLevel));
        if(vehicle->GetMatrix()){const CMatrix* m=vehicle->GetMatrix();state.rightX=m->right.x;state.rightY=m->right.y;state.rightZ=m->right.z;state.upX=m->up.x;state.upY=m->up.y;state.upZ=m->up.z;state.atX=m->at.x;state.atY=m->at.y;state.atZ=m->at.z;state.orientationValid=true;}
        if(vehicle->IsAutomobile()){auto& d=vehicle->AsAutomobile()->m_damageManager;for(int i=0;i<4;++i)state.tyreStatus|=static_cast<uint8_t>((d.m_anWheelsStatus[i]&3u)<<(i*2));for(int i=0;i<6;++i)state.doorStatus|=static_cast<uint32_t>(d.m_anDoorsStatus[i])<<(i*4);state.lightStatus=d.m_nLightsStatus;state.panelStatus=d.m_nPanelsStatus;}
        g_client->SendNpcVehicleState(entry.first,state);
    }
    for(const auto& state:states)
        g_client->SendNpcState(state.ref,state.model,state.x,state.y,state.z,state.heading,state.health,state.armour,state.weapon,state.move,state.vehicleKey,state.seat,state.specialName,state.vehicleNetworkId);
    static std::unordered_map<uint32_t,uint64_t> lastRefs;
    for(auto& kv:lastRefs) if(!seen[kv.first]) g_client->SendNpcRemove(kv.first);
    lastRefs.clear(); for(auto& kv:seen) lastRefs[kv.first]=1;
    static std::unordered_map<uint32_t,bool> lastVehicleKeys;
    for(const auto& old:lastVehicleKeys) if(!currentVehicleKeys.count(old.first)) g_client->SendNpcVehicleRemove(old.first);
    lastVehicleKeys=std::move(currentVehicleKeys);
}





bool SeatIndexFor(CVehicle* vehicle, uint8_t wireSeat, int& outIndex)
{
    if (wireSeat == 0) { outIndex = -1; return true; }
    const int index = static_cast<int>(wireSeat) - 1;
    const int maxPassengers = std::min<int>(static_cast<int>(vehicle->m_nMaxPassengers), 8);
    if (index >= maxPassengers) return false;
    outIndex = index;
    return true;
}




bool RemotePedOccupiesSeat(CPed* ped, CVehicle* vehicle, int seatIndex)
{
    if (!ped || !vehicle || ped->m_pVehicle != vehicle) return false;
    if (seatIndex < 0) return vehicle->m_pDriver == ped;
    return seatIndex < 8 && vehicle->m_apPassengers[seatIndex] == ped;
}





bool WarpRemotePedIntoVehicle(CPed* ped, CVehicle* vehicle, int seatIndex)
{
    if (!g_saUtils || !ped || !vehicle || seatIndex < -1 || seatIndex > 7) return false;
    if (RemotePedOccupiesSeat(ped, vehicle, seatIndex)) return true;

    const int pedRef = CPools::GetPedRef(ped);
    const int vehicleRef = CPools::GetVehicleRef(vehicle);
    if (pedRef <= 0 || vehicleRef <= 0 ||
        CPools::GetPed(pedRef) != ped || CPools::GetVehicle(vehicleRef) != vehicle) {
        WeaponFileLog("REMOTE_VEHICLE_WARP_INVALID ped=%p pedRef=%d vehicle=%p vehicleRef=%d seat=%d",
                      static_cast<void*>(ped), pedRef, static_cast<void*>(vehicle), vehicleRef, seatIndex);
        return false;
    }




    CVehicle* oldVehicle = ped->m_pVehicle;
    if (oldVehicle && !RemotePedOccupiesSeat(ped, vehicle, seatIndex)) {
        const int oldVehicleRef = CPools::GetVehicleRef(oldVehicle);
        const bool oldVehicleValid = oldVehicleRef > 0 && CPools::GetVehicle(oldVehicleRef) == oldVehicle;
        g_saUtils->ScriptCommand(&kScmRemoveCharFromCarMaintainPosition, pedRef);
        if (oldVehicleValid) {
            if (oldVehicle->m_pDriver == ped) oldVehicle->m_pDriver = nullptr;
            for (unsigned i = 0; i < 8; ++i)
                if (oldVehicle->m_apPassengers[i] == ped) oldVehicle->m_apPassengers[i] = nullptr;
        }
        if (ped->m_pVehicle == oldVehicle) ped->m_pVehicle = nullptr;
        ped->bInVehicle = 0;
    }

    if (seatIndex < 0)
        g_saUtils->ScriptCommand(&kScmWarpCharIntoCar, pedRef, vehicleRef);
    else
        g_saUtils->ScriptCommand(&kScmWarpCharIntoCarAsPassenger, pedRef, vehicleRef, seatIndex);

    const bool seated = RemotePedOccupiesSeat(ped, vehicle, seatIndex);
    if (!seated) {
        WeaponFileLog("REMOTE_VEHICLE_WARP_PENDING pedRef=%d vehicleRef=%d seat=%d pedVehicle=%p driver=%p",
                      pedRef, vehicleRef, seatIndex, static_cast<void*>(ped->m_pVehicle),
                      static_cast<void*>(vehicle->m_pDriver));
    }
    return seated;
}

void UpdateRemoteNpcs()
{
    if(!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;
    static uint8_t healthReportTick=0;
    const bool reportHealth=(++healthReportTick>=15);
    if(reportHealth) healthReportTick=0;
    for(auto id:g_client->DrainRemovedNpcs()){ auto it=g_remoteNpcs.find(id); if(it!=g_remoteNpcs.end()){ if(it->second) g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(it->second)); g_remoteNpcs.erase(it); } g_remoteNpcVisuals.erase(id); g_remoteNpcModelWait.erase(id); ReleaseRemoteSpecialLease(RemoteNpcSpecialLeaseKey(id)); }
    for(const auto& n:g_client->GetRemoteNpcs()){
        if(n.ownerId==g_client->LocalId()) continue;
        const uint64_t specialLeaseKey=RemoteNpcSpecialLeaseKey(n.id);
        char normalizedSpecialName[coop_special::kNameCapacity]{};
        const bool hasSpecialName=coop_special::NormalizeName(n.specialModelName,normalizedSpecialName,sizeof(normalizedSpecialName));
        std::string targetSpecialName=hasSpecialName?normalizedSpecialName:"";
        bool specialModel=!targetSpecialName.empty();
        uint16_t remoteModelId=n.modelId;
        int specialSlot=0;
        auto existingPed=g_remoteNpcs.find(n.id);


        if(specialModel && !RemoteNpcMayTryModel(n.id,kSpecialLoadWaitKey)){
            specialModel=false; targetSpecialName.clear(); specialSlot=0; remoteModelId=0;
        }
        if(specialModel){
            specialSlot=EnsureRemoteSpecialSlot(specialLeaseKey,targetSpecialName);
            if(!specialSlot){if(RemoteNpcLogOnce(n.id,1,0))WeaponFileLog("REMOTE_NPC_SPECIAL_SLOT_UNAVAILABLE id=%u model=%u name=%s",n.id,n.modelId,targetSpecialName.c_str());continue;}
            if(!CStreaming::HasSpecialCharLoaded(specialSlot)){
                CStreaming::RequestSpecialChar(specialSlot,targetSpecialName.c_str(),2);
                CStreaming::LoadAllRequestedModels(false);
            }
            remoteModelId=coop_special::ModelIndexFromSlot(specialSlot);
            if(!CStreaming::HasSpecialCharLoaded(specialSlot)||!HasReadyPedModel(remoteModelId)){

                if(RemoteNpcNoteModelFailure(n.id,kSpecialLoadWaitKey)){
                    if(RemoteNpcLogOnce(n.id,8,specialSlot))WeaponFileLog("REMOTE_NPC_SPECIAL_LOADING id=%u slot=%d name=%s loaded=%u model_ready=%u",n.id,specialSlot,targetSpecialName.c_str(),CStreaming::HasSpecialCharLoaded(specialSlot)?1u:0u,HasReadyPedModel(remoteModelId)?1u:0u);
                    continue;
                }
                WeaponFileLog("REMOTE_NPC_SPECIAL_GIVEUP id=%u slot=%d name=%s loaded=%u model_ready=%u fallback=cj_then_retry",n.id,specialSlot,targetSpecialName.c_str(),CStreaming::HasSpecialCharLoaded(specialSlot)?1u:0u,HasReadyPedModel(remoteModelId)?1u:0u);
                ReleasePendingRemoteSpecialSlot(specialLeaseKey);
                specialModel=false; targetSpecialName.clear(); specialSlot=0; remoteModelId=0;
            }
        }else if(remoteModelId>=coop_special::kFirstModelIndex&&remoteModelId<=coop_special::kLastModelIndex){



            if(!g_remoteNpcs.count(n.id) && RemoteNpcNoteModelFailure(n.id,kUnnamedSpecialWaitKey)) continue;
            remoteModelId=0;
        }
        if(!specialModel&&!IsSafeNetworkPedModel(remoteModelId)){
            if(RemoteNpcLogOnce(n.id,2,remoteModelId)){
                const bool infoOk=remoteModelId<=311&&CStreaming::IsInitialised()&&HasPedModelInfo(remoteModelId);
                WeaponFileLog("REMOTE_NPC_MODEL_UNSAFE id=%u model=%u model_info_ok=%u in_cd_image=%u (NPC skipped)",n.id,remoteModelId,infoOk?1u:0u,(infoOk&&CStreaming::IsObjectInCdImage(static_cast<int>(remoteModelId)))?1u:0u);
            }
            continue;
        }
        CVehicle* seatedVehicle=nullptr;
        if(n.vehicleId){auto vehicleIt=g_remoteVehicles.find(n.vehicleId);if(vehicleIt==g_remoteVehicles.end()||!vehicleIt->second){if(RemoteNpcLogOnce(n.id,3,n.vehicleId))WeaponFileLog("REMOTE_NPC_WAITING_FOR_VEHICLE id=%u vehicle=%u model=%u",n.id,static_cast<unsigned>(n.vehicleId),n.modelId);continue;}seatedVehicle=vehicleIt->second;}
        auto it=g_remoteNpcs.find(n.id);
        if(it==g_remoteNpcs.end()){
            if(!specialModel&&!RemoteNpcMayTryModel(n.id,remoteModelId)) continue;
            if(!specialModel)LoadNetworkPedModel(remoteModelId);
            CPed* ped=g_saUtils->CreatePed(specialModel?23:4,remoteModelId,n.x,n.y,n.z);
            if(!ped&&specialModel){ReleasePendingRemoteSpecialSlot(specialLeaseKey);continue;}
            bool fellBackToCj=false;
            if(!ped&&remoteModelId!=0){
                if(RemoteNpcLogOnce(n.id,4,remoteModelId))WeaponFileLog("REMOTE_NPC_MODEL_CREATE_FAIL id=%u model=%u model_ready=%u path=spawn (will retry)",n.id,remoteModelId,HasReadyPedModel(remoteModelId)?1u:0u);

                if(RemoteNpcNoteModelFailure(n.id,remoteModelId)) continue;
                if(RemoteNpcLogOnce(n.id,5,remoteModelId))WeaponFileLog("REMOTE_NPC_MODEL_GIVEUP id=%u model=%u fallback=0",n.id,remoteModelId);
                g_saUtils->MarkModelAsNotNeeded(remoteModelId);remoteModelId=0;fellBackToCj=true;
                LoadNetworkPedModel(remoteModelId);ped=g_saUtils->CreatePed(4,remoteModelId,n.x,n.y,n.z);
            }
            if(!ped){ReleaseRemoteSpecialLease(specialLeaseKey);continue;}
            if(!fellBackToCj)g_remoteNpcModelWait.erase(n.id);
            g_remoteNpcs[n.id]=ped; it=g_remoteNpcs.find(n.id);
            if(specialModel)CommitRemoteSpecialSlot(specialLeaseKey,specialSlot,targetSpecialName);else ReleaseRemoteSpecialLease(specialLeaseKey);
            if(!specialModel)g_saUtils->MarkModelAsNotNeeded(remoteModelId);
            ApplyRemoteVitals(ped,n.health,n.armour);
            auto& visual=g_remoteNpcVisuals[n.id]; visual.x=n.x; visual.y=n.y; visual.z=n.z; visual.initialized=true;
        }else{
            const uint16_t currentModel=*reinterpret_cast<const uint16_t*>(reinterpret_cast<uintptr_t>(it->second)+gtasa_v200_armv7::kCPedModelIndexOffset);
            auto leaseIt=g_remoteSpecialLeases.find(specialLeaseKey);
            const std::string appliedSpecialName=leaseIt!=g_remoteSpecialLeases.end()?leaseIt->second.activeName:"";
            if((currentModel!=remoteModelId||appliedSpecialName!=targetSpecialName)&&(specialModel||RemoteNpcMayTryModel(n.id,remoteModelId))){
                if(!specialModel)LoadNetworkPedModel(remoteModelId);
                CPed* replacement=g_saUtils->CreatePed(specialModel?23:4,remoteModelId,n.x,n.y,n.z);
                if(!replacement&&specialModel){ReleasePendingRemoteSpecialSlot(specialLeaseKey);continue;}
                if(!replacement&&remoteModelId!=0){


                    if(RemoteNpcLogOnce(n.id,6,remoteModelId))WeaponFileLog("REMOTE_NPC_MODEL_CREATE_FAIL id=%u model=%u model_ready=%u path=replace (keeping current ped, will retry)",n.id,remoteModelId,HasReadyPedModel(remoteModelId)?1u:0u);
                    RemoteNpcNoteModelFailure(n.id,remoteModelId);
                }
                if(replacement){
                    g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(it->second));it->second=replacement;
                    if(specialModel)CommitRemoteSpecialSlot(specialLeaseKey,specialSlot,targetSpecialName);else ReleaseRemoteSpecialLease(specialLeaseKey);
                    if(!specialModel)g_saUtils->MarkModelAsNotNeeded(remoteModelId);
                    g_remoteNpcModelWait.erase(n.id);
                    g_remoteNpcVisuals.erase(n.id);
                    auto& visual=g_remoteNpcVisuals[n.id];visual.x=n.x;visual.y=n.y;visual.z=n.z;visual.initialized=true;
                }
            }
        }
        CPed* ped=it->second; if(!ped) continue;



        {
            auto& weaponVisual=g_remoteNpcVisuals[n.id];
            const uint64_t weaponNowMs=SteadyNowMs();
            if(weaponVisual.weapon!=n.weapon && weaponNowMs>=weaponVisual.nextWeaponTryMs){
                if(ApplyRemoteWeapon(ped,n.weapon)) weaponVisual.weapon=n.weapon;
                else weaponVisual.nextWeaponTryMs=weaponNowMs+250;
            }
        }
        if(seatedVehicle){
            const bool inCorrectSeat=n.vehicleSeat==0?seatedVehicle->m_pDriver==ped:seatedVehicle->m_apPassengers[n.vehicleSeat-1]==ped;
            int npcSeatIndex=-1;
            if(!SeatIndexFor(seatedVehicle,n.vehicleSeat,npcSeatIndex)) continue;
            if(ped->m_pVehicle!=seatedVehicle||!inCorrectSeat) g_saUtils->PutPedInVehicle(ped,seatedVehicle,npcSeatIndex);
            auto& visual=g_remoteNpcVisuals[n.id]; visual.x=n.x; visual.y=n.y; visual.z=n.z; visual.initialized=true;
            if(reportHealth){const uint16_t health=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->GetHealth())));const uint16_t armour=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->m_fArmour)));g_client->SendNpcHealth(n.id,health,armour);}
            continue;
        }
        if(ped->m_pVehicle){g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(ped));g_remoteNpcs.erase(n.id);g_remoteNpcVisuals.erase(n.id);ReleaseRemoteSpecialLease(specialLeaseKey);continue;}
        auto* physical=reinterpret_cast<CPhysical*>(ped);
        auto& visual=g_remoteNpcVisuals[n.id];
        if(!visual.initialized){ visual.x=n.x; visual.y=n.y; visual.z=n.z; visual.initialized=true; }
        const float oldX=visual.x, oldY=visual.y, oldZ=visual.z;
        const float targetDx=n.x-visual.x, targetDy=n.y-visual.y, targetDz=n.z-visual.z;
        const float targetD2=targetDx*targetDx+targetDy*targetDy+targetDz*targetDz;
        const bool snap=targetD2>900.0f;
        if(snap){ visual.x=n.x; visual.y=n.y; visual.z=n.z; }
        else if(targetD2>0.000001f){
            const float factor=targetD2>25.0f?0.45f:0.18f;
            visual.x=SmoothValue(visual.x,n.x,factor);
            visual.y=SmoothValue(visual.y,n.y,factor);
            visual.z=SmoothValue(visual.z,n.z,factor);
            if(std::fabs(n.x-visual.x)<0.015f&&std::fabs(n.y-visual.y)<0.015f&&std::fabs(n.z-visual.z)<0.015f){ visual.x=n.x; visual.y=n.y; visual.z=n.z; }
        }
        const float stepX=visual.x-oldX, stepY=visual.y-oldY, stepZ=visual.z-oldZ;
        physical->m_vecMoveSpeed=snap?CVector(0.0f,0.0f,0.0f):CVector(stepX,stepY,stepZ);
        g_saUtils->SetPosition(physical,visual.x,visual.y,visual.z);
        g_saUtils->SetAngle(physical,(unsigned char)2,n.heading);
        if(reportHealth){
            const uint16_t health=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->GetHealth())));
            const uint16_t armour=static_cast<uint16_t>(std::max(0.0f,std::min(100.0f,ped->m_fArmour)));
            g_client->SendNpcHealth(n.id,health,armour);
        }
        if(g_setMoveState&&g_setMoveAnim){
            const float remainX=n.x-visual.x, remainY=n.y-visual.y, remainZ=n.z-visual.z;
            const bool moving=!snap&&(remainX*remainX+remainY*remainY+remainZ*remainZ>0.0025f||stepX*stepX+stepY*stepY+stepZ*stepZ>0.000001f);
            const int moveState=moving?(n.moveState==kMoveStill?kMoveWalk:(int)n.moveState):(int)kMoveStill;
            g_setMoveState(ped,moveState);
            g_setMoveAnim(ped);
        }
    }
}

void SpawnTestNpc(const NpcSpawnTest& test)
{
    logger->Info("NPC_TEST received: model=%d position=(%0.1f,%0.1f,%0.1f)", test.modelId, test.x, test.y, test.z);
    if (!g_spawnedNpcs.empty() && g_spawnedNpcs.front()) {
        if (ApplyRemoteWeapon(g_spawnedNpcs.front(), test.weapon))
            logger->Info("NPC test switched to room leader weapon ID %u", test.weapon);
        else
            logger->Error("NPC test could not switch to room leader weapon ID %u", test.weapon);
        return;
    }
    if (!g_saUtils) {
        logger->Error("NPC test ignored: SAUtils 1.6 is unavailable");
        return;
    }
    if (!IsSupportedNpcModel(test.modelId)) {
        logger->Error("NPC test ignored: unsupported ped model %d", test.modelId);
        return;
    }
    if (!g_saUtils->IsGameInitialised()) {
        logger->Info("NPC test delayed: GTA world is not initialized yet");
        return;
    }


    g_saUtils->LoadModelId(test.modelId);
    CStreaming::RequestModel(356, 2);
    CStreaming::LoadAllRequestedModels(false);
    CPed* ped = g_saUtils->CreatePed(4, test.modelId, test.x, test.y, test.z);
    if (!ped) {
        logger->Error("NPC test CreatePed returned null for model %d", test.modelId);
        return;
    }

    if (ApplyRemoteWeapon(ped, test.weapon))
        logger->Info("NPC test weapon %u equipped through CPed::GiveWeapon/SetCurrentWeapon", test.weapon);
    else
        logger->Error("NPC test weapon %u could not be equipped; verified weapon symbols unavailable", test.weapon);
    g_spawnedNpcs.push_back(ped);
    g_saUtils->MarkModelAsNotNeeded(test.modelId);
    logger->Info("NPC model %d spawned at (%0.1f, %0.1f, %0.1f)",
                 test.modelId,
                 test.x, test.y, test.z);
}

uint16_t FindRemoteVehicleNetworkId(CVehicle* vehicle);

bool IsLiveVehicleBinding(const VehicleEntityBinding& binding)
{
    return binding.vehicle && binding.poolRef > 0 && CPools::GetVehicle(binding.poolRef) == binding.vehicle;
}

void TrackLocalVehicleEntity(CVehicle* vehicle)
{
    if (!vehicle) return;
    const int ref = CPools::GetVehicleRef(vehicle);
    if (ref <= 0 || CPools::GetVehicle(ref) != vehicle) return;
    const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vehicle));
    if (!key) return;
    VehicleEntityBinding binding{vehicle, ref, false};
    auto prior = g_localVehicleEntities.find(key);
    if (prior != g_localVehicleEntities.end() && prior->second.vehicle == vehicle &&
        prior->second.poolRef == ref) binding.createdByFusion = prior->second.createdByFusion;
    for (const auto& entry : g_vehicleEntityByNetworkId) {
        if (entry.second.vehicle == vehicle && entry.second.poolRef == ref) {
            binding.createdByFusion = entry.second.createdByFusion;
            break;
        }
    }
    g_localVehicleEntities[key] = binding;
}

void RefreshLocalVehicleIdentityBindings()
{
    if (!g_client) return;
    for (auto it = g_localVehicleEntities.begin(); it != g_localVehicleEntities.end();) {
        if (!IsLiveVehicleBinding(it->second)) {
            it = g_localVehicleEntities.erase(it);
            continue;
        }
        const uint16_t networkId = g_client->LocalVehicleNetworkIdForKey(it->first);
        if (networkId) {
            auto known = g_vehicleEntityByNetworkId.find(networkId);
            if (known == g_vehicleEntityByNetworkId.end() || !IsLiveVehicleBinding(known->second) ||
                known->second.vehicle == it->second.vehicle) {
                VehicleEntityBinding binding = it->second;
                g_vehicleEntityByNetworkId[networkId] = binding;
            }
        }
        ++it;
    }
    for (auto it = g_vehicleEntityByNetworkId.begin(); it != g_vehicleEntityByNetworkId.end();) {
        if (!IsLiveVehicleBinding(it->second)) it = g_vehicleEntityByNetworkId.erase(it);
        else ++it;
    }
}

static void CaptureLocalVehicleOrientation(CVehicle* vehicle);
static void CaptureLocalVehicleDetails(CVehicle* vehicle) {
    if (!vehicle || !g_client) return;
    TrackLocalVehicleEntity(vehicle);
    uint8_t tyres=0; uint32_t doors=0,lights=0,panels=0;
    if (vehicle->IsAutomobile()) { auto* car=vehicle->AsAutomobile(); auto& d=car->m_damageManager;
        for(int i=0;i<4;++i) tyres|=static_cast<uint8_t>((d.m_anWheelsStatus[i]&3u)<<(i*2));
        for(int i=0;i<6;++i) doors|=static_cast<uint32_t>(d.m_anDoorsStatus[i])<<(i*4);
        lights=d.m_nLightsStatus; panels=d.m_nPanelsStatus; }
    g_client->SetLocalVehicleDetails(vehicle->m_nPrimaryColor,vehicle->m_nSecondaryColor,vehicle->bEngineOn?1:0,vehicle->bLightsOn?1:0,vehicle->bSirenOrAlarm?1:0,static_cast<uint8_t>(vehicle->m_eDoorLock),std::max(0.0f,std::min(15.0f,vehicle->m_fDirtLevel)),tyres,doors,lights,panels,FindRemoteVehicleNetworkId(vehicle->m_pTrailer));
    CaptureLocalVehicleOrientation(vehicle);
}
static void CaptureLocalVehicleOrientation(CVehicle* vehicle) {
    if (!vehicle || !g_client || !vehicle->GetMatrix()) return;
    const CMatrix* m = vehicle->GetMatrix();
    const CVector& r=m->right; const CVector& u=m->up; const CVector& a=m->at;
    g_client->SetLocalVehicleOrientation(r.x,r.y,r.z,u.x,u.y,u.z,a.x,a.y,a.z);
}
static void ApplyRemoteVehicleOrientation(CVehicle* vehicle,const RemoteVehicle& state) {
    if (!vehicle || !state.orientationValid || !vehicle->GetMatrix()) return;
    CMatrix* m=vehicle->GetMatrix();



    constexpr float kOrientationBlend = 0.62f;
    m->right += (CVector(state.rightX,state.rightY,state.rightZ) - m->right) * kOrientationBlend;
    m->up += (CVector(state.upX,state.upY,state.upZ) - m->up) * kOrientationBlend;
    m->at += (CVector(state.atX,state.atY,state.atZ) - m->at) * kOrientationBlend;
    m->Reorthogonalise(); if (m->m_pAttachMatrix) m->UpdateRwMatrix(m->m_pAttachMatrix);
}
static void ApplyRemoteVehicleDetails(CVehicle* vehicle,const RemoteVehicle& state) {
    if(!vehicle)return; vehicle->m_nPrimaryColor=state.primaryColor; vehicle->m_nSecondaryColor=state.secondaryColor; vehicle->bEngineOn=state.engineOn!=0; vehicle->bLightsOn=state.lightsOn!=0; vehicle->bSirenOrAlarm=state.sirenOn!=0; vehicle->m_eDoorLock=static_cast<eDoorLock>(state.doorLock); vehicle->m_fDirtLevel=std::max(0.0f,std::min(15.0f,state.dirt));
    if(!vehicle->IsAutomobile())return; auto* car=vehicle->AsAutomobile(); auto& d=car->m_damageManager; for(int i=0;i<4;++i)d.m_anWheelsStatus[i]=static_cast<uint8_t>((state.tyreStatus>>(i*2))&3u); for(int i=0;i<6;++i){d.m_anDoorsStatus[i]=static_cast<uint8_t>((state.doorStatus>>(i*4))&0xFu);car->m_doors[i].m_nDoorState=static_cast<eDoorState>(d.m_anDoorsStatus[i]);} d.m_nLightsStatus=state.lightStatus; d.m_nPanelsStatus=state.panelStatus;
}
void UpdateRemoteVehicles()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;



    RefreshLocalVehicleIdentityBindings();
    for (const auto& dead : g_client->DrainRemovedVehicles()) {
        auto remoteIt = g_remoteVehicles.find(dead);
        auto bindingIt = g_vehicleEntityByNetworkId.find(dead);
        CVehicle* oldVehicle = remoteIt != g_remoteVehicles.end() ? remoteIt->second :
            (bindingIt != g_vehicleEntityByNetworkId.end() ? bindingIt->second.vehicle : nullptr);
        const bool createdByFusion = bindingIt != g_vehicleEntityByNetworkId.end()
            ? bindingIt->second.createdByFusion : (remoteIt != g_remoteVehicles.end());
        if (createdByFusion && oldVehicle && IsLiveVehicleBinding(bindingIt != g_vehicleEntityByNetworkId.end()
                ? bindingIt->second : VehicleEntityBinding{oldVehicle, CPools::GetVehicleRef(oldVehicle), true}))
            g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(oldVehicle));
        if (remoteIt != g_remoteVehicles.end()) g_remoteVehicles.erase(remoteIt);
        g_remoteVehicleVisuals.erase(dead);
        g_vehicleEntityByNetworkId.erase(dead);
        for (auto localIt = g_localVehicleEntities.begin(); localIt != g_localVehicleEntities.end();) {
            if (!IsLiveVehicleBinding(localIt->second)) localIt = g_localVehicleEntities.erase(localIt);
            else ++localIt;
        }
        logger->Info("Remote vehicle %u released by network owner (preserve-native=%d)", dead, createdByFusion ? 0 : 1);
    }

    for (const auto& v : g_client->GetRemoteVehicles()) {
        if (v.ownerId == g_client->LocalId()) {



            auto localIt = g_remoteVehicles.find(v.id);
            if (localIt != g_remoteVehicles.end()) {
                CVehicle* physical = localIt->second;
                int ref = physical ? CPools::GetVehicleRef(physical) : 0;
                if (physical && ref > 0 && CPools::GetVehicle(ref) == physical) {
                    bool createdByFusion = true;
                    auto known = g_vehicleEntityByNetworkId.find(v.id);
                    if (known != g_vehicleEntityByNetworkId.end() && known->second.vehicle == physical &&
                        known->second.poolRef == ref) createdByFusion = known->second.createdByFusion;
                    g_vehicleEntityByNetworkId[v.id] = VehicleEntityBinding{physical, ref, createdByFusion};
                }
                g_remoteVehicles.erase(localIt);
                g_remoteVehicleVisuals.erase(v.id);
                TrackLocalVehicleEntity(physical);
                logger->Info("Remote vehicle %u promoted to local authority (entity retained)", v.id);
            }
            continue;
        }
        auto it = g_remoteVehicles.find(v.id);
        if (it != g_remoteVehicles.end() && it->second) {
            auto known = g_vehicleEntityByNetworkId.find(v.id);
            if (known == g_vehicleEntityByNetworkId.end() || known->second.vehicle != it->second) {
                const int ref = CPools::GetVehicleRef(it->second);
                if (ref > 0 && CPools::GetVehicle(ref) == it->second)
                    g_vehicleEntityByNetworkId[v.id] = VehicleEntityBinding{it->second, ref, true};
            }
        }
        if (it == g_remoteVehicles.end() || !it->second) {



            auto known = g_vehicleEntityByNetworkId.find(v.id);
            CVehicle* vehicle = nullptr;
            if (known != g_vehicleEntityByNetworkId.end() && IsLiveVehicleBinding(known->second))
                vehicle = known->second.vehicle;
            if (!vehicle) {
                if (!IsSafeNetworkVehicleModel(v.modelId)) continue;
                g_saUtils->LoadModelId(v.modelId);
                vehicle = g_saUtils->CreateVehicle(v.modelId, v.x, v.y, v.z);
                if (!vehicle) {
                    logger->Error("Remote vehicle %u CreateVehicle failed model=%u", v.id, v.modelId);
                    continue;
                }
                const int ref = CPools::GetVehicleRef(vehicle);
                if (ref > 0 && CPools::GetVehicle(ref) == vehicle)
                    g_vehicleEntityByNetworkId[v.id] = VehicleEntityBinding{vehicle, ref, true};
                g_saUtils->MarkModelAsNotNeeded(v.modelId);
                logger->Info("Remote vehicle %u spawned model=%u at (%.1f,%.1f,%.1f)",
                             v.id, v.modelId, v.x, v.y, v.z);
            } else {
                logger->Info("Remote vehicle %u reused existing local-world vehicle ref=%d", v.id,
                             CPools::GetVehicleRef(vehicle));
            }
            g_remoteVehicles[v.id] = vehicle;
            RemoteVehicleVisualState initial{};
            initial.x = v.x; initial.y = v.y; initial.z = v.z; initial.heading = v.heading;
            initial.rightX = v.rightX; initial.rightY = v.rightY; initial.rightZ = v.rightZ;
            initial.upX = v.upX; initial.upY = v.upY; initial.upZ = v.upZ;
            initial.atX = v.atX; initial.atY = v.atY; initial.atZ = v.atZ;
            initial.initialized = true;
            g_remoteVehicleVisuals[v.id] = initial;
            it = g_remoteVehicles.find(v.id);
        }

        CVehicle* vehicle = it->second;
        if (!vehicle) continue;
        auto& visual = g_remoteVehicleVisuals[v.id];
        if (!visual.initialized) {
            visual.x = v.x; visual.y = v.y; visual.z = v.z; visual.heading = v.heading;
            visual.rightX = v.rightX; visual.rightY = v.rightY; visual.rightZ = v.rightZ;
            visual.upX = v.upX; visual.upY = v.upY; visual.upZ = v.upZ;
            visual.atX = v.atX; visual.atY = v.atY; visual.atZ = v.atZ;
            visual.initialized = true;
        } else {
            const float dx = v.x - visual.x;
            const float dy = v.y - visual.y;
            const float dz = v.z - visual.z;
            if (dx * dx + dy * dy + dz * dz > 900.0f) {
                visual.x = v.x; visual.y = v.y; visual.z = v.z; visual.heading = v.heading;
            } else {
                constexpr float kVehiclePositionSmoothing = 0.42f;
                constexpr float kVehicleHeadingSmoothing = 0.45f;
                visual.x = SmoothValue(visual.x, v.x, kVehiclePositionSmoothing);
                visual.y = SmoothValue(visual.y, v.y, kVehiclePositionSmoothing);
                visual.z = SmoothValue(visual.z, v.z, kVehiclePositionSmoothing);
                visual.heading += AngleDelta(v.heading, visual.heading) * kVehicleHeadingSmoothing;
            }
            if (v.orientationValid) {
                constexpr float kOrientationTargetBlend = 0.55f;
                visual.rightX += (v.rightX - visual.rightX) * kOrientationTargetBlend;
                visual.rightY += (v.rightY - visual.rightY) * kOrientationTargetBlend;
                visual.rightZ += (v.rightZ - visual.rightZ) * kOrientationTargetBlend;
                visual.upX += (v.upX - visual.upX) * kOrientationTargetBlend;
                visual.upY += (v.upY - visual.upY) * kOrientationTargetBlend;
                visual.upZ += (v.upZ - visual.upZ) * kOrientationTargetBlend;
                visual.atX += (v.atX - visual.atX) * kOrientationTargetBlend;
                visual.atY += (v.atY - visual.atY) * kOrientationTargetBlend;
                visual.atZ += (v.atZ - visual.atZ) * kOrientationTargetBlend;
            }
        }
        auto* physical = reinterpret_cast<CPhysical*>(vehicle);
        g_saUtils->SetPosition(physical, visual.x, visual.y, visual.z);
        RemoteVehicle smoothed = v;
        smoothed.rightX = visual.rightX; smoothed.rightY = visual.rightY; smoothed.rightZ = visual.rightZ;
        smoothed.upX = visual.upX; smoothed.upY = visual.upY; smoothed.upZ = visual.upZ;
        smoothed.atX = visual.atX; smoothed.atY = visual.atY; smoothed.atZ = visual.atZ;
        smoothed.orientationValid = v.orientationValid;
        ApplyRemoteVehicleOrientation(vehicle, smoothed);
        physical->m_vecMoveSpeed = CVector(v.vx, v.vy, v.vz);
        vehicle->m_fHealth = std::max(0.0f, std::min(1000.0f, v.health));
        ApplyRemoteVehicleDetails(vehicle, v);
    }
}

uint16_t FindRemoteVehicleNetworkId(CVehicle* vehicle)
{
    if(!vehicle) return 0;
    for(const auto& kv : g_remoteVehicles) if(kv.second == vehicle) return kv.first;
    return 0;
}

void ReapplyRemoteVehicleHeading(CVehicle* vehicle)
{
    if(!vehicle || !g_client) return;
    const uint16_t id=FindRemoteVehicleNetworkId(vehicle); if(!id) return;
    for(const auto& state:g_client->GetRemoteVehicles()) if(state.id==id){
        auto visual = g_remoteVehicleVisuals.find(id);
        if (visual != g_remoteVehicleVisuals.end() && visual->second.initialized) {
            RemoteVehicle smoothed = state;
            smoothed.rightX = visual->second.rightX; smoothed.rightY = visual->second.rightY; smoothed.rightZ = visual->second.rightZ;
            smoothed.upX = visual->second.upX; smoothed.upY = visual->second.upY; smoothed.upZ = visual->second.upZ;
            smoothed.atX = visual->second.atX; smoothed.atY = visual->second.atY; smoothed.atZ = visual->second.atZ;
            ApplyRemoteVehicleOrientation(vehicle,smoothed);
        } else {
            ApplyRemoteVehicleOrientation(vehicle,state);
        }
        return;
    }
}



CPed* LiveRemotePed(uint16_t id)
{
    auto it = g_remotePeds.find(id);
    if (it == g_remotePeds.end() || !it->second) return nullptr;
    CPed* ped = it->second;
    auto refIt = g_remotePedRefs.find(id);
    const bool handleOk = refIt != g_remotePedRefs.end() && CPools::GetPed(refIt->second) == ped;
    if (handleOk && ped->m_pRwClump) return ped;


    g_remoteTaskPermitUntilMs.erase(it->second);
            g_remotePeds.erase(it);
    g_remotePedRefs.erase(id);
    g_remoteVisuals.erase(id);
    if (handleOk) DiscardRemotePed(ped, "body gone");
    WeaponFileLog("REMOTE_PED_RECREATE id=%u handleOk=%d", static_cast<unsigned>(id), handleOk ? 1 : 0);
    return nullptr;
}

void UpdateRemotePlayers()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;

    static uint64_t lastPlayerHealthReportMs = 0;
    const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    const bool reportPlayerHealth = nowMs - lastPlayerHealthReportMs >= 300;
    if (reportPlayerHealth) lastPlayerHealthReportMs = nowMs;
    CPlayerPed* localAreaPlayer = FindPlayerPed(-1);
    const unsigned localAreaCodeForLog = localAreaPlayer ? localAreaPlayer->m_nAreaCode : 0;

    UpdateRemoteVehicles();
    UpdateRemoteNpcs();

    for(const auto& vitals:g_client->DrainNpcVitals()){
        if(vitals.ownerId==g_client->LocalId()){
            auto* pool=CPools::ms_pPedPool;
            if(!pool||!vitals.localRef) continue;
            CPlayerPed* localPlayer=FindPlayerPed(-1);
            const int maxPeds=std::min(pool->m_nSize,256);
            for(int i=0;i<maxPeds;++i){
                CPed* ped=pool->GetAt(i);
                if(!ped||ped==localPlayer||
                   static_cast<uint32_t>(CPools::GetPedRef(ped))!=vitals.localRef) continue;
                ApplyRemoteVitals(ped,vitals.health,vitals.armour);
                break;
            }
        }else{
            auto it=g_remoteNpcs.find(vitals.id);
            if(it!=g_remoteNpcs.end()&&it->second) ApplyRemoteVitals(it->second,vitals.health,vitals.armour);
        }
    }

    for (const auto& removed : g_client->DrainRemovedPlayers()) {
        auto it = g_remotePeds.find(removed.id);
        if (it != g_remotePeds.end()) {


            if (it->second && it->second->m_pVehicle)
                DiscardRemotePed(it->second, "player removed while seated");
            else
                g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(it->second));
                    g_remoteTaskPermitUntilMs.erase(it->second);
            g_remotePeds.erase(it);
            g_remoteVisuals.erase(removed.id);
            logger->Info("Remote player %u despawned", removed.id);
        }
        g_lastRemotePlayerHealth.erase(removed.id);
        ReleaseRemoteSpecialLease(removed.id);
    }

    for (const auto& npcDamage : g_client->DrainNpcDamages()) {
        auto* pool = CPools::ms_pPedPool;
        if (!pool || !npcDamage.localRef) continue;
        CPlayerPed* localPlayer = FindPlayerPed(-1);
        const int maxPeds = std::min(pool->m_nSize, 256);
        for (int i = 0; i < maxPeds; ++i) {
            CPed* ped = pool->GetAt(i);
            if (!ped || ped == localPlayer ||
                static_cast<uint32_t>(CPools::GetPedRef(ped)) != npcDamage.localRef)
                continue;
            bool excluded = false;
            for (const auto& remote : g_remotePeds) if (remote.second == ped) { excluded = true; break; }
            if (!excluded) for (const auto& remote : g_remoteNpcs) if (remote.second == ped) { excluded = true; break; }
            if (!excluded) for (CPed* testNpc : g_spawnedNpcs) if (testNpc == ped) { excluded = true; break; }
            if (excluded) continue;
            ApplyRemoteVitals(ped, npcDamage.health, npcDamage.armour);
            WeaponFileLog("NPC_DAMAGE_OWNER_APPLY id=%u ref=%u health=%u armour=%u weapon=%u",
                          npcDamage.id, npcDamage.localRef, npcDamage.health, npcDamage.armour, npcDamage.weapon);
            break;
        }
    }

    for (const auto& damage : g_client->DrainDamages()) {
        if (damage.targetId == g_client->LocalId()) {
            if (auto* local = FindPlayerPed(-1)) {
                ApplyRemoteVitals(local, damage.health, damage.armour);
                logger->Info("DAMAGE_LOCAL weapon=%u health=%u armour=%u", damage.weapon, damage.health, damage.armour);
            }
        } else {
            auto target = g_remotePeds.find(damage.targetId);
            if (target != g_remotePeds.end() && !LiveRemotePed(damage.targetId)) target = g_remotePeds.end();
            if (target != g_remotePeds.end()) ApplyRemoteVitals(target->second, damage.health, damage.armour);
        }
    }
    for (const auto& vitals : g_client->DrainPlayerVitals()) {
        CPed* target = vitals.id == g_client->LocalId()
            ? static_cast<CPed*>(FindPlayerPed(-1))
            : LiveRemotePed(vitals.id);
        if (target) ApplyRemoteVitals(target, vitals.health, vitals.armour);
    }
    const auto pendingShots = g_client->DrainShots();
    for (const auto& enter : g_client->DrainVehicleEnters()) {
        auto pit = g_remotePeds.find(enter.id);
        if (pit != g_remotePeds.end() && !LiveRemotePed(enter.id)) pit = g_remotePeds.end();
        auto vit = g_remoteVehicles.find(enter.vehicleId);
        if (pit != g_remotePeds.end() && vit != g_remoteVehicles.end() && pit->second && vit->second) {
            int seatIndex = -1;
            if (SeatIndexFor(vit->second, enter.seat, seatIndex)) {



                CPed* occupant = seatIndex < 0 ? vit->second->m_pDriver : vit->second->m_apPassengers[seatIndex];
                if (occupant && occupant != pit->second) continue;
                if (!RemotePedOccupiesSeat(pit->second, vit->second, seatIndex))
                    WarpRemotePedIntoVehicle(pit->second, vit->second, seatIndex);
                if (RemotePedOccupiesSeat(pit->second, vit->second, seatIndex))
                    logger->Info("Remote player %u seated by direct warp in vehicle %u seat %u", enter.id, enter.vehicleId, enter.seat);
            } else {
                logger->Info("Remote player %u: vehicle %u has no seat %u; not placed", enter.id, enter.vehicleId, enter.seat);
            }
        }
    }

    for (const auto& player : g_client->GetRemotePlayers()) {
        uint16_t remoteModelId = player.modelId;
        char normalizedSpecialName[coop_special::kNameCapacity]{};
        const bool hasSpecialName = coop_special::NormalizeName(
            player.specialModelName, normalizedSpecialName, sizeof(normalizedSpecialName));
        std::string targetSpecialName = hasSpecialName ? normalizedSpecialName : "";
        bool specialModel = !targetSpecialName.empty();
        int specialSlot = 0;
        auto it = g_remotePeds.find(player.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(player.id)) it = g_remotePeds.end();


        if (it == g_remotePeds.end() && !specialModel && remoteModelId != 0 &&
            !WeaponModelResident(static_cast<int>(remoteModelId))) {
            CStreaming::RequestModel(remoteModelId, 2);
            CStreaming::LoadAllRequestedModels(false);
            if (!WeaponModelResident(static_cast<int>(remoteModelId))) continue;
        }
        bool remoteVehicleExit = false;
        float spawnX = player.x, spawnY = player.y, spawnZ = player.z;
        if (it != g_remotePeds.end()) {
            auto previousVisual = g_remoteVisuals.find(player.id);
            remoteVehicleExit = previousVisual != g_remoteVisuals.end() &&
                                previousVisual->second.wasInVehicle && !player.vehicleId;
        }

        const auto lastHealth = g_lastRemotePlayerHealth.find(player.id);
        const bool respawned = lastHealth != g_lastRemotePlayerHealth.end() &&
                               lastHealth->second == 0 && player.health > 0;
        g_lastRemotePlayerHealth[player.id] = player.health;
        if (respawned && it != g_remotePeds.end() && it->second &&
            (it->second->m_ePedState == PEDSTATE_DIE || it->second->m_ePedState == PEDSTATE_DEAD)) {
            g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(it->second));
                    g_remotePeds.erase(it);
            g_remoteVisuals.erase(player.id);
            it = g_remotePeds.end();
            logger->Info("Remote player %u respawned; recreated stale death-state ped", player.id);
        }




        if (it != g_remotePeds.end() && !player.vehicleId && it->second && it->second->m_pVehicle) {
            CVehicle* oldVehicle = it->second->m_pVehicle;
            const CVector vehiclePosition = oldVehicle->GetPosition();
            const float dx = spawnX - vehiclePosition.x;
            const float dy = spawnY - vehiclePosition.y;



            if (dx * dx + dy * dy < 6.25f) {
                float sideX = 1.0f, sideY = 0.0f;
                if (const CMatrix* matrix = oldVehicle->GetMatrix()) {
                    sideX = matrix->right.x;
                    sideY = matrix->right.y;
                }
                const auto previousVisual = g_remoteVisuals.find(player.id);
                const uint8_t previousSeat = previousVisual != g_remoteVisuals.end()
                    ? previousVisual->second.lastVehicleSeat : 0;
                const float side = previousSeat == 0 ? -1.0f : 1.0f;
                spawnX = vehiclePosition.x + sideX * side * 1.6f;
                spawnY = vehiclePosition.y + sideY * side * 1.6f;
                spawnZ = vehiclePosition.z + 0.25f;
            }
            DiscardRemotePed(it->second, "vehicle exit");
            g_remotePeds.erase(player.id);
            g_remoteVisuals.erase(player.id);
            it = g_remotePeds.end();
            remoteVehicleExit = true;
            logger->Info("Remote player %u vehicle exit: replaced stale seated ped with clean on-foot ped", player.id);
        }






        if (player.vehicleId && it != g_remotePeds.end() && it->second && g_remoteVehicles.count(player.vehicleId)) {
            CVehicle* targetVehicle = g_remoteVehicles[player.vehicleId];
            if (it->second->m_pVehicle && it->second->m_pVehicle != targetVehicle) {
                DiscardRemotePed(it->second, "vehicle switch");
                g_remotePeds.erase(player.id);
                g_remoteVisuals.erase(player.id);
                it = g_remotePeds.end();
                logger->Info("Remote player %u vehicle switch: replaced stale attached ped", player.id);
            }
        }

        if (specialModel) {
            specialSlot = EnsureRemoteSpecialSlot(player.id, targetSpecialName);
            if (!specialSlot) {
                auto rejected = g_lastRejectedRemoteModels.find(player.id);
                if (rejected == g_lastRejectedRemoteModels.end() || rejected->second != player.modelId) {
                    WeaponFileLog("REMOTE_SPECIAL_SLOT_UNAVAILABLE id=%u model=%u name=%s fallback=keep_or_cj",
                                  player.id, player.modelId, targetSpecialName.c_str());
                    g_lastRejectedRemoteModels[player.id] = player.modelId;
                }
                continue;
            } else {
                if (!CStreaming::HasSpecialCharLoaded(specialSlot)) {
                    CStreaming::RequestSpecialChar(specialSlot, targetSpecialName.c_str(), 2);
                    CStreaming::LoadAllRequestedModels(false);
                    if (!CStreaming::HasSpecialCharLoaded(specialSlot)) continue;
                }
                remoteModelId = coop_special::ModelIndexFromSlot(specialSlot);
                if (!HasReadyPedModel(remoteModelId)) {
                    WeaponFileLog("REMOTE_SPECIAL_MODEL_INFO_INVALID id=%u slot=%d model=%u name=%s fallback=0",
                                  player.id, specialSlot, remoteModelId, targetSpecialName.c_str());
                    ReleasePendingRemoteSpecialSlot(player.id);
                    continue;
                }
            }
        } else {
            const bool isUnidentifiedSpecial = remoteModelId >= coop_special::kFirstModelIndex &&
                                               remoteModelId <= coop_special::kLastModelIndex;
            const bool streamingReady = CStreaming::IsInitialised();
            const bool modelInfoOk = remoteModelId == 0 ||
                                     (remoteModelId <= 311 && streamingReady && HasPedModelInfo(remoteModelId));
            const bool inCdImage = remoteModelId > 0 && !isUnidentifiedSpecial && modelInfoOk &&
                                   CStreaming::IsObjectInCdImage(static_cast<int>(remoteModelId));
            const bool safeModel = remoteModelId == 0 || (!isUnidentifiedSpecial && modelInfoOk && inCdImage);
            if (!safeModel) {
                auto rejected = g_lastRejectedRemoteModels.find(player.id);
                if (rejected == g_lastRejectedRemoteModels.end() || rejected->second != remoteModelId) {
                    if (isUnidentifiedSpecial)
                        WeaponFileLog("REMOTE_SPECIAL_NAME_MISSING id=%u model=%u fallback=0", player.id, remoteModelId);
                    else
                        WeaponFileLog("REMOTE_MODEL_FALLBACK id=%u model=%u range_ok=%u stream_ready=%u model_info_ok=%u in_cd_image=%u",
                                      player.id, remoteModelId, remoteModelId <= 311, streamingReady, modelInfoOk, inCdImage);
                    g_lastRejectedRemoteModels[player.id] = remoteModelId;
                }
                remoteModelId = 0;
            } else {
                g_lastRejectedRemoteModels.erase(player.id);
            }
        }

        if (it != g_remotePeds.end()) {
            const auto currentModel = *reinterpret_cast<const uint16_t*>(
                reinterpret_cast<uintptr_t>(it->second) + gtasa_v200_armv7::kCPedModelIndexOffset);
            auto leaseIt = g_remoteSpecialLeases.find(player.id);
            const std::string appliedSpecialName = leaseIt != g_remoteSpecialLeases.end()
                ? leaseIt->second.activeName : "";
            if (currentModel != remoteModelId || appliedSpecialName != targetSpecialName) {
                if (!specialModel) LoadNetworkPedModel(remoteModelId);


                const int pedType = specialModel ? 23 : 4;
                CPed* replacement = g_saUtils->CreatePed(pedType, remoteModelId, player.x, player.y, player.z);
                if (!replacement && specialModel) {
                    ReleasePendingRemoteSpecialSlot(player.id);
                    continue;
                }
                if (!replacement && remoteModelId != 0) {
                    WeaponFileLog("REMOTE_MODEL_CREATE_FAIL id=%u model=%u special=%s path=change",
                                  player.id, remoteModelId, specialModel ? targetSpecialName.c_str() : "");
                    if (!specialModel) {  }
                    remoteModelId = 0;
                    specialModel = false;
                    targetSpecialName.clear();
                    specialSlot = 0;
                    LoadNetworkPedModel(remoteModelId);
                    replacement = g_saUtils->CreatePed(4, remoteModelId, player.x, player.y, player.z);
                }
                if (!replacement) {
                    logger->Error("Remote player %u skin change failed; keeping existing ped", player.id);
                    ReleasePendingRemoteSpecialSlot(player.id);
                    continue;
                }
                g_saUtils->MarkEntityAsNotNeeded(reinterpret_cast<CEntity*>(it->second));
                it->second = replacement;
                g_remotePedRefs[player.id] = CPools::GetPedRef(replacement);
                if (specialModel) CommitRemoteSpecialSlot(player.id, specialSlot, targetSpecialName);
                else ReleaseRemoteSpecialLease(player.id);
                if (!specialModel) {  }
                g_remoteVisuals.erase(player.id);
                logger->Info("Remote player %u model changed %u -> %u special=%s",
                             player.id, currentModel, remoteModelId,
                             specialModel ? targetSpecialName.c_str() : "");
            }
        }
        if (it == g_remotePeds.end()) {
            if (!specialModel) LoadNetworkPedModel(remoteModelId);

            const int pedType = specialModel ? 23 : 4;
            CPed* ped = g_saUtils->CreatePed(pedType, remoteModelId, spawnX, spawnY, spawnZ);
            if (!ped && specialModel) {
                ReleasePendingRemoteSpecialSlot(player.id);
                continue;
            }
            if (!ped && remoteModelId != 0) {
                WeaponFileLog("REMOTE_MODEL_CREATE_FAIL id=%u model=%u special=%s path=spawn",
                              player.id, remoteModelId, specialModel ? targetSpecialName.c_str() : "");
                logger->Error("Remote player %u model %u could not be created; falling back to CJ",
                              player.id, remoteModelId);
                if (!specialModel) {  }
                remoteModelId = 0;
                specialModel = false;
                targetSpecialName.clear();
                specialSlot = 0;
                LoadNetworkPedModel(remoteModelId);
                ped = g_saUtils->CreatePed(4, remoteModelId, spawnX, spawnY, spawnZ);
            }
            if (!ped) {
                logger->Error("Remote player %u ped creation failed (requested model %u)",
                              player.id, remoteModelId);
                ReleaseRemoteSpecialLease(player.id);
                continue;
            }
            g_remotePeds.emplace(player.id, ped);
            g_remotePedRefs[player.id] = CPools::GetPedRef(ped);
            g_remoteVisuals[player.id] = MakeInitialRemoteVisualState(player);
            g_remoteVisuals[player.id].lastVehicleSeat = player.vehicleSeat;
            if (specialModel) CommitRemoteSpecialSlot(player.id, specialSlot, targetSpecialName);
            else ReleaseRemoteSpecialLease(player.id);
            if (!specialModel) {  }
            auto* physical = reinterpret_cast<CPhysical*>(ped);
            g_saUtils->SetAngle(physical, static_cast<unsigned char>(2), player.heading);
            if (!player.vehicleId && !remoteVehicleExit) {
                ApplyRemoteAnimationProfile(ped, player.animation, false, false);
            } else if (remoteVehicleExit && g_setMoveState && g_setMoveAnim) {



                g_setMoveState(ped, static_cast<int>(kMoveStill));
                g_setMoveAnim(ped);
            }
            if (ApplyRemoteWeapon(ped, player.weapon))
            {
                g_remoteVisuals[player.id].weapon = player.weapon;
                ApplyRemoteVitals(ped, player.health, player.armour);
                logger->Info("Remote player %u native weapon apply complete: weapon=%u", player.id, player.weapon);
            }
            else
                logger->Error("Remote player %u weapon %u application deferred for retry", player.id, player.weapon);
            logger->Info("Remote player %u spawned at (%0.1f, %0.1f, %0.1f)",
                         player.id, player.x, player.y, player.z);
        } else {
            auto& visual = g_remoteVisuals[player.id];
            if (!visual.initialized) {
                visual = MakeInitialRemoteVisualState(player);
            } else {
                constexpr float kPositionSmoothing = 0.28f;
                constexpr float kHeadingSmoothing = 0.32f;
                visual.lastTargetX = player.x;
                visual.lastTargetY = player.y;
                visual.x = SmoothValue(visual.x, player.x, kPositionSmoothing);
                visual.y = SmoothValue(visual.y, player.y, kPositionSmoothing);
                visual.z = SmoothValue(visual.z, player.z, kPositionSmoothing);



                visual.heading += AngleDelta(player.heading, visual.heading) * kHeadingSmoothing;
            }
            const bool preserveNativeAnimation = !it->second->IsPedInControl() ||
                it->second->m_ePedState == PEDSTATE_DIE ||
                it->second->m_ePedState == PEDSTATE_DEAD;
            const bool animationChanged = !visual.animationInitialized ||
                visual.animationSequence != player.animation.sequence ||
                visual.animationLocomotion != player.animation.locomotion ||
                visual.animationAction != static_cast<uint8_t>(player.animation.action) ||
                visual.animationFlags != player.animation.flags;
            const auto position = reinterpret_cast<CPhysical*>(it->second);

            const auto networkVehicleIt = player.vehicleId ? g_remoteVehicles.find(player.vehicleId) : g_remoteVehicles.end();
            const bool seatedOnNetwork = networkVehicleIt != g_remoteVehicles.end() && networkVehicleIt->second != nullptr;
            const bool areaChanged = it->second->m_nAreaCode != player.areaCode;
            const CVector networkTarget(player.x, player.y, player.z);
            const CVector current = it->second->GetPosition();
            const bool largePositionJump = !seatedOnNetwork &&
                DistanceBetweenPoints(current, networkTarget) > kRemoteTeleportDistance;
            if (seatedOnNetwork) {
                it->second->m_nAreaCode = player.areaCode;
                networkVehicleIt->second->m_nAreaCode = player.areaCode;
                visual.x = player.x; visual.y = player.y; visual.z = player.z;
                visual.lastTargetX = player.x; visual.lastTargetY = player.y;
            } else if (areaChanged || largePositionJump) {


                if (g_entityTeleport) {
                    gtasa_v200_armv7::CVectorAbi target{player.x, player.y, player.z};
                    g_entityTeleport(reinterpret_cast<void*>(it->second), target, 1);
                } else {
                    g_saUtils->SetPosition(position, player.x, player.y, player.z);
                }
                it->second->m_nAreaCode = player.areaCode;
                visual.x = player.x; visual.y = player.y; visual.z = player.z;
                visual.lastTargetX = player.x; visual.lastTargetY = player.y;
                visual.heading = player.heading;
                g_saUtils->SetAngle(position, static_cast<unsigned char>(2), player.heading);
                logger->Info("REMOTE_HARD_RESYNC id=%u localArea=%u remoteArea=%u jump=%d net=(%.1f,%.1f,%.1f)",
                              static_cast<unsigned>(player.id), localAreaCodeForLog,
                              static_cast<unsigned>(player.areaCode), largePositionJump ? 1 : 0,
                              player.x, player.y, player.z);
            } else if (!preserveNativeAnimation) {
                g_saUtils->SetPosition(position, visual.x, visual.y, visual.z);
                g_saUtils->SetAngle(position, static_cast<unsigned char>(2), visual.heading);
            } else {





                static std::unordered_map<unsigned, uint64_t> lastSkipLogMs;
                const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                uint64_t& lastMs = lastSkipLogMs[static_cast<unsigned>(player.id)];
                if (nowMs - lastMs > 1000) {
                    lastMs = nowMs;
                    const CVector pedPos = it->second->GetPosition();
                    logger->Info("REMOTE_POS_SKIPPED id=%u pedState=%d bInVehicle=%d m_pVehicle=%p netVehicleId=%u net=(%.1f,%.1f,%.1f) ped=(%.1f,%.1f,%.1f)",
                                 static_cast<unsigned>(player.id), static_cast<int>(it->second->m_ePedState),
                                 it->second->bInVehicle ? 1 : 0, static_cast<void*>(it->second->m_pVehicle),
                                 static_cast<unsigned>(player.vehicleId),
                                 visual.x, visual.y, visual.z, pedPos.x, pedPos.y, pedPos.z);
                }
            }
            if (reportPlayerHealth) {
                const float observedHealth = it->second->GetHealth();
                const float observedArmour = it->second->m_fArmour;
                if (std::isfinite(observedHealth) && std::isfinite(observedArmour)) {
                    const auto health = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, observedHealth)));
                    const auto armour = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, observedArmour)));
                    g_client->SendPlayerHealth(player.id, health, armour);
                }
            }
            ApplyRemoteVitals(it->second, player.health, player.armour);
            {
            }
            UpdateRemoteAimAndShooting(it->second, player, visual);
            if (!preserveNativeAnimation) {
                const bool inVehicle = it->second->m_pVehicle != nullptr;
                const bool networkInVehicle = player.vehicleId != 0;

                const bool shooting = (player.animation.flags & ANIM_FIRING) != 0 ||
                                      (visual.shootingFrames != 0 && visual.fireAnimFrames == 0);
                const bool justExitedVehicle = visual.wasInVehicle && !inVehicle && !networkInVehicle;
                if ((justExitedVehicle || remoteVehicleExit) && g_setMoveState && g_setMoveAnim) {
                    g_setMoveState(it->second, static_cast<int>(kMoveStill));
                    g_setMoveAnim(it->second);
                    visual.animationInitialized = false;
                    visual.animationAssociationCount = 0;
                }






                if (inVehicle || player.vehicleId != 0) {
                    visual.animationAssociationCount = 0;
                    visual.animationInitialized = false;
                } else if (remoteVehicleExit) {


                    visual.animationInitialized = true;
                    visual.animationSequence = player.animation.sequence;
                    visual.animationLocomotion = kMoveStill;
                    visual.animationAction = static_cast<uint8_t>(AnimationAction::None);
                    visual.animationFlags = 0;
                } else {


                    if (animationChanged) {
                        ApplyRemoteAnimationProfile(it->second, player.animation, inVehicle || player.vehicleId != 0, shooting);
                        visual.animationSequence = player.animation.sequence;
                        visual.animationLocomotion = player.animation.locomotion;
                        visual.animationAction = static_cast<uint8_t>(player.animation.action);
                        visual.animationFlags = player.animation.flags;
                        visual.animationInitialized = true;
                    }
                    if (player.animationAssociationCount || visual.animationAssociationCount)
                        ApplyAssociationStates(it->second, player, visual);
                }
                if (kBonePoseSyncEnabled) ApplyRemoteBones(it->second, player);
                if (visual.shootingFrames != 0) --visual.shootingFrames;
                if (visual.fireAnimFrames != 0 && --visual.fireAnimFrames == 0) FadeRemoteFireAnimation(visual, it->second);
                if (networkInVehicle) visual.lastVehicleSeat = player.vehicleSeat;
                visual.wasInVehicle = inVehicle || networkInVehicle;
            }
            if (visual.weapon != player.weapon) {
                if (ApplyRemoteWeapon(it->second, player.weapon))
                {
                    visual.weapon = player.weapon;
                    logger->Info("Remote player %u native weapon switch complete: weapon=%u", player.id, player.weapon);
                }
                else
                    logger->Error("Remote player %u weapon %u application failed; retrying", player.id, player.weapon);
            }
        }
    }


    for (const auto& player : g_client->GetRemotePlayers()) {
        if (!player.vehicleId) continue;
        auto pit = g_remotePeds.find(player.id);
        if (pit != g_remotePeds.end() && !LiveRemotePed(player.id)) pit = g_remotePeds.end();
        auto vit = g_remoteVehicles.find(player.vehicleId);
        if (pit != g_remotePeds.end() && vit != g_remoteVehicles.end() && pit->second && vit->second) {
            int seatIndex = -1;
            if (!SeatIndexFor(vit->second, player.vehicleSeat, seatIndex)) {
                logger->Info("Remote player %u: vehicle %u has no seat %u; not placed",
                             player.id, player.vehicleId, player.vehicleSeat);
                continue;
            }
            if (RemotePedOccupiesSeat(pit->second, vit->second, seatIndex)) continue;
            CPed* occupant = seatIndex < 0 ? vit->second->m_pDriver : vit->second->m_apPassengers[seatIndex];
            if (occupant && occupant != pit->second) {




                if (occupant == FindPlayerPed(-1) || IsRemotePlayerPed(occupant)) {
                    logger->Info("Remote player %u waiting for occupied vehicle %u seat %u",
                                 player.id, player.vehicleId, player.vehicleSeat);
                    continue;
                }
                const int occupantRef = CPools::GetPedRef(occupant);
                if (occupantRef > 0) g_saUtils->ScriptCommand(&kScmRemoveCharFromCarMaintainPosition, occupantRef);



                const bool stillOccupiesSeat = seatIndex < 0
                    ? vit->second->m_pDriver == occupant
                    : vit->second->m_apPassengers[seatIndex] == occupant;
                if (stillOccupiesSeat) {
                    if (seatIndex < 0) vit->second->m_pDriver = nullptr;
                    else vit->second->m_apPassengers[seatIndex] = nullptr;
                    occupant->m_pVehicle = nullptr;
                    occupant->bInVehicle = 0;
                }
                logger->Info("Remote player %u cleared stale NPC from vehicle %u seat %u",
                             player.id, player.vehicleId, player.vehicleSeat);
            }
            if (WarpRemotePedIntoVehicle(pit->second, vit->second, seatIndex)) {
                logger->Info("Remote player %u seat verified after direct warp vehicle=%u seat=%u",
                             player.id, player.vehicleId, player.vehicleSeat);
            } else {
                static std::unordered_map<uint16_t, uint64_t> lastWarpRetryLog;
                const uint64_t logNow = NowSteadyMs();
                uint64_t& lastLog = lastWarpRetryLog[player.id];
                if (logNow - lastLog >= 1000) {
                    lastLog = logNow;
                    logger->Error("Remote player %u seat pending after direct warp vehicle=%u seat=%u pedVehicle=%p driver=%p",
                                  player.id, player.vehicleId, player.vehicleSeat,
                                  static_cast<void*>(pit->second->m_pVehicle),
                                  static_cast<void*>(vit->second->m_pDriver));
                }
            }
        }
    }
    for (const auto& shot : pendingShots) {
        auto it = g_remotePeds.find(shot.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(shot.id)) it = g_remotePeds.end();
        if (it != g_remotePeds.end()) ExecuteRemoteShot(shot, it->second);
        else logger->Info("Remote player %u shot deferred; remote ped is unavailable", shot.id);
    }
}

void OnSaUtilsPlayerUpdate(uintptr_t )
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;
    for (const auto& test : g_client->DrainNpcSpawnTests()) SpawnTestNpc(test);
    UpdateRemotePlayers();
}

struct NameTagScreenData
{
    CVector2D posXY;
    CVector2D sizeXY;
    bool isWithinScreen = false;
};

NameTagScreenData ConvertRemoteNameTagCoord(CVector pos, bool nearClip, bool farClip, CVector2D size)
{
    NameTagScreenData out{};
    RwV3d screenPos2D{};
    float sizeX = 0.0f, sizeY = 0.0f;
    const bool result = CSprite::CalcScreenCoors(pos, &screenPos2D, &sizeX, &sizeY, farClip, nearClip);
    if (!result || RsGlobal.maximumWidth <= 0 || RsGlobal.maximumHeight <= 0) return out;
    out.posXY.x = (screenPos2D.x / RsGlobal.maximumWidth) * 640.0f;
    out.posXY.y = (screenPos2D.y / RsGlobal.maximumHeight) * 448.0f;
    sizeX = (sizeX / RsGlobal.maximumWidth) * 15.0f;
    sizeY = (sizeY / RsGlobal.maximumHeight) * 15.0f;
    out.sizeXY.x = sizeX * size.x;
    out.sizeXY.y = sizeY * size.y;
    out.isWithinScreen = true;
    return out;
}

struct RemoteNameTagLayout
{
    float screenX = 0.0f;
    float screenY = 0.0f;
    float iconSize = 0.0f;
    float gap = 0.0f;
    float barWidth = 0.0f;
    float barHeight = 0.0f;
    float tagLeft = 0.0f;
    float iconCx = 0.0f;
    float iconCy = 0.0f;
    float textX = 0.0f;
    float textY = 0.0f;
    float healthY = 0.0f;
    float fontSize = 0.0f;
};

bool CalculateRemoteNameTagLayout(CPed* ped, RemoteNameTagLayout& layout)
{
    if (!ped || RsGlobal.maximumWidth <= 0 || RsGlobal.maximumHeight <= 0)
        return false;

    CVector pos = ped->GetPosition();
    pos.z += 1.65f;
    if (DistanceBetweenPoints(pos, TheCamera.GetPosition()) > 60.0f)
        return false;

    const auto convert = ConvertRemoteNameTagCoord(pos, true, true, CVector2D(1.5f, 1.5f));
    if (!convert.isWithinScreen)
        return false;

    layout.screenX = convert.posXY.x * RsGlobal.maximumWidth * 0.0015625f;
    layout.screenY = convert.posXY.y * RsGlobal.maximumHeight * 0.002232143f;

    const float screenH = static_cast<float>(RsGlobal.maximumHeight);
    const float scale = g_remoteNameTagScale;




    layout.iconSize = std::max(40.0f, screenH * 0.12f) * scale;
    layout.gap = std::max(3.0f, screenH * 0.008f) * scale;
    layout.barWidth = std::max(72.0f, screenH * 0.21f) * scale;
    layout.barHeight = std::max(8.0f, screenH * 0.032f) * scale;
    const float totalWidth = layout.iconSize + layout.gap + layout.barWidth;
    layout.tagLeft = layout.screenX - totalWidth * 0.5f;
    layout.iconCx = layout.tagLeft + layout.iconSize * 0.5f;




    layout.iconCy = layout.screenY - layout.iconSize * 0.02f;
    layout.textX = layout.tagLeft + layout.iconSize + layout.gap;
    layout.textY = layout.iconCy - layout.iconSize * 0.10f;
    layout.healthY = layout.iconCy + layout.iconSize * 0.15f;

    layout.fontSize = std::max(9.0f, layout.iconSize * 0.17f);
    return true;
}

void DrawRemoteWeaponIconsInternal()
{
    if (!g_client || !g_client->IsConnected() || !g_saUtils || !g_saUtils->IsGameInitialised())
        return;

    for (const auto& player : g_client->GetRemotePlayers())
    {
        auto it = g_remotePeds.find(player.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(player.id)) it = g_remotePeds.end();
        if (it == g_remotePeds.end() || !it->second)
            continue;

        RemoteNameTagLayout layout{};
        if (!CalculateRemoteNameTagLayout(it->second, layout))
            continue;

        DrawRemoteWeaponIcon(it->second, player.weapon, layout.iconCx, layout.iconCy, layout.iconSize);
    }
}

void DrawRemoteImGuiMeter(ImDrawList* drawList, float left, float top, float width, float height,
                          float value, ImU32 background, ImU32 fill)
{
    const float clamped = std::max(0.0f, std::min(1.0f, value));
    const float border = std::max(2.0f, height * 0.07f);
    drawList->AddRectFilled(ImVec2(left, top), ImVec2(left + width, top + height), IM_COL32(0, 0, 0, 255));
    const float innerLeft = left + border;
    const float innerTop = top + border;
    const float innerWidth = std::max(0.0f, width - border * 2.0f);
    const float innerHeight = std::max(0.0f, height - border * 2.0f);
    if (innerWidth <= 0.0f || innerHeight <= 0.0f) return;
    drawList->AddRectFilled(ImVec2(innerLeft, innerTop),
        ImVec2(innerLeft + innerWidth, innerTop + innerHeight), background);
    const float fillWidth = innerWidth * clamped;
    if (fillWidth > 0.0f) {
        drawList->AddRectFilled(ImVec2(innerLeft, innerTop),
            ImVec2(innerLeft + fillWidth, innerTop + innerHeight), fill);
    }
}

void RenderRemotePlayerNameTagsInternal(ImDrawList* drawList)
{
    if (!drawList || !g_client || !g_client->IsConnected() || !g_saUtils || !g_saUtils->IsGameInitialised())
        return;

    for (const auto& player : g_client->GetRemotePlayers())
    {
        auto it = g_remotePeds.find(player.id);
        if (it != g_remotePeds.end() && !LiveRemotePed(player.id)) it = g_remotePeds.end();
        if (it == g_remotePeds.end() || !it->second)
            continue;

        RemoteNameTagLayout layout{};
        if (!CalculateRemoteNameTagLayout(it->second, layout))
            continue;

        if (g_remoteNameTagsVisible && player.name[0] != '\0')
        {

            UI::OutlinedText(drawList, layout.fontSize, ImVec2(layout.textX, layout.textY),
                NameTagColor(player.colorIndex), player.name);
        }

        if (g_remoteHealthBarsVisible)
        {

            if (player.armour > 0) {
                const float armourHeight = std::max(4.0f, layout.barHeight * 0.22f);
                DrawRemoteImGuiMeter(drawList, layout.textX,
                    layout.healthY - armourHeight - std::max(3.0f, layout.gap * 0.16f),
                    layout.barWidth, armourHeight, static_cast<float>(player.armour) / 100.0f,
                    IM_COL32(35, 35, 35, 255), IM_COL32(190, 195, 205, 255));
            }
            DrawRemoteImGuiMeter(drawList, layout.textX, layout.healthY, layout.barWidth, layout.barHeight,
                static_cast<float>(player.health) / 100.0f,
                IM_COL32(75, 11, 20, 255), IM_COL32(185, 34, 30, 255));
        }
    }
}

void OnGameProcess()
{
    if (!g_client || !g_saUtils || !g_saUtils->IsGameInitialised()) return;
    for (const RemoteGameTime& time : g_client->DrainGameTimes()) {
        if (CClock::ms_nGameClockHours != time.hour || CClock::ms_nGameClockMinutes != time.minute || CClock::ms_nGameClockDays != time.day)
            CClock::SetGameClock(time.hour, time.minute, time.day);
    }
    g_client->SetLocalGameTime(CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes, CClock::ms_nGameClockDays);
    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (localPlayer) {
        g_client->SetLocalAreaCode(localPlayer->m_nAreaCode);


        const uint8_t weapon = CurrentWeaponTypeForPed(localPlayer);
        g_client->SetLocalWeaponType(weapon);
        const auto health = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, localPlayer->GetHealth())));
        const auto armour = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, localPlayer->m_fArmour)));
        g_client->SetLocalHealthArmour(health, armour);
        if (kBonePoseSyncEnabled) CaptureAndSendLocalBones(localPlayer);
    }
    for (const auto& test : g_client->DrainNpcSpawnTests()) SpawnTestNpc(test);
    UpdateRemotePlayers();
    UpdateRemoteMissionMarkers();
    static int npcTick=0; if((++npcTick % 6)==0) { SyncLocalNpcs(); SyncLocalMissionMarkers(); }
}

void HookPedProcess(void* ped)
{
    if (g_originalPedProcess) g_originalPedProcess(ped);




    CPlayerPed* localPlayer = FindPlayerPed(-1);
    if (g_client && g_getBoundCentre && ped &&
        reinterpret_cast<void*>(localPlayer) == ped) {
        gtasa_v200_armv7::CVectorAbi centre{};
        g_getBoundCentre(ped, centre);
        static bool havePrevious = false;
        static gtasa_v200_armv7::CVectorAbi previous{};
        auto* localPed = static_cast<CPed*>(ped);
        float heading = localPed->m_placement.m_fHeading * 57.2957795f;
        if (const auto* matrix = localPed->GetMatrix()) {
            const auto& forward = matrix->GetForward();
            if (std::isfinite(forward.x) && std::isfinite(forward.y) &&
                (std::fabs(forward.x) > 0.0001f || std::fabs(forward.y) > 0.0001f))
                heading = std::atan2(-forward.x, forward.y) * 57.2957795f;
        }
        heading = std::fmod(heading, 360.0f);
        if (heading < 0.0f) heading += 360.0f;
    uint8_t animation = kMoveStill;
        if (havePrevious) {
            const float dx = centre.x - previous.x;
            const float dy = centre.y - previous.y;
            const float speed2 = dx * dx + dy * dy;
            if (speed2 > 0.0001f) {



                animation = speed2 > 0.01f ? kMoveRun : kMoveWalk;
            }
        }
        previous = centre;
        havePrevious = true;
        const auto modelId = *reinterpret_cast<const uint16_t*>(
            reinterpret_cast<uintptr_t>(ped) + gtasa_v200_armv7::kCPedModelIndexOffset);
        const auto weaponType = CurrentWeaponTypeForPed(static_cast<CPed*>(ped));
    const std::string specialName = StableSpecialNameForPed(static_cast<CPed*>(ped), modelId);
        g_client->SetLocalModel(modelId, specialName.c_str());
        g_client->SetLocalWeaponType(weaponType);
        const auto health = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, static_cast<CPed*>(ped)->GetHealth())));
        const auto armour = static_cast<uint16_t>(std::max(0.0f, std::min(100.0f, static_cast<CPed*>(ped)->m_fArmour)));
        g_client->SetLocalHealthArmour(health, armour);




        uint8_t localSeat = 0;
        CVehicle* localVehicle = GetSeatedVehicle(localPed, &localSeat);
        {



            static uint64_t lastStaleLogMs = 0;
            if (!localVehicle && localPed->m_pVehicle) {
                const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                if (nowMs - lastStaleLogMs > 1000) {
                    lastStaleLogMs = nowMs;
                    logger->Info("LOCAL_STALE_VEHICLE_PTR m_pVehicle=%p driverMatch=%d bInVehicle=%d pedState=%d -> sending on-foot position",
                                 static_cast<void*>(localPed->m_pVehicle),
                                 localPed->m_pVehicle->m_pDriver == localPed ? 1 : 0,
                                 localPed->bInVehicle ? 1 : 0,
                                 static_cast<int>(localPed->m_ePedState));
                }
            }
        }
        if (localVehicle) {
            const CVector& vehiclePosition = localVehicle->GetPosition();
            g_client->SetLocalTransform(vehiclePosition.x, vehiclePosition.y,
                                         vehiclePosition.z, HeadingDegrees(localVehicle));
        } else {




            const CVector& worldPosition = localPed->GetPosition();
            g_client->SetLocalTransform(worldPosition.x, worldPosition.y,
                                         worldPosition.z, heading);
        }
        g_client->SetLocalAnimationState(animation);
        static uint32_t animationSequence = 0;
        static AnimationProfile lastAnimationProfile{};
        AnimationProfile localAnimation = CaptureAnimationProfile(localPed, 0);
        const bool semanticAnimationChanged = animationSequence == 0 ||
            localAnimation.locomotion != lastAnimationProfile.locomotion ||
            localAnimation.action != lastAnimationProfile.action ||
            localAnimation.flags != lastAnimationProfile.flags ||
            localAnimation.weaponState != lastAnimationProfile.weaponState ||
            localAnimation.animGroup != lastAnimationProfile.animGroup;
        if (semanticAnimationChanged) ++animationSequence;
        localAnimation.sequence = animationSequence;
        lastAnimationProfile = localAnimation;
        g_client->SetLocalAnimationProfile(localAnimation);
        AnimationAssociationState localAssociations[64]{};
        static LocalAssociationGate associationGate;
        static bool lastAssociationsSuppressed = false;
        const uint64_t associationNowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        const bool suppressAssociations = associationGate.ShouldSuppress(localVehicle, localPed->m_pVehicle, associationNowMs);
        if (lastAssociationsSuppressed && !suppressAssociations && localPed->m_pVehicle) {

            logger->Info("LOCAL_ASSOC_RESUMED_AFTER_EXIT m_pVehicle=%p bInVehicle=%d pedState=%d -> on-foot animation associations resumed",
                         static_cast<void*>(localPed->m_pVehicle), localPed->bInVehicle ? 1 : 0,
                         static_cast<int>(localPed->m_ePedState));
        }
        lastAssociationsSuppressed = suppressAssociations;
        g_client->SetLocalAnimationAssociations(
            suppressAssociations ? nullptr : localAssociations,
            suppressAssociations ? 0 : CaptureActiveAssociations(localPed, localAssociations));

        if (localVehicle) {
            const CVector& vp = localVehicle->GetPosition();
            const CVector& vv = reinterpret_cast<CPhysical*>(localVehicle)->m_vecMoveSpeed;
            const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(localVehicle));
            const uint8_t seat=localSeat;
            const uint16_t remoteVehicleId=FindRemoteVehicleNetworkId(localVehicle);
            if(remoteVehicleId && seat==0){



                const uint64_t now=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                auto& last=g_lastVehicleClaimMs[remoteVehicleId];
                if(now-last>500){
                    g_client->SendVehicleClaim(remoteVehicleId,seat,key);
                    last=now;
                }
            }
            g_client->SetLocalVehicle(key, localVehicle->m_nModelIndex,
                                      vp.x, vp.y, vp.z,
                                      HeadingDegrees(localVehicle),
                                      vv.x, vv.y, vv.z, localVehicle->m_fHealth, seat);
            g_client->SetLocalVehicleNetworkId(remoteVehicleId);
            CaptureLocalVehicleDetails(localVehicle);
        } else {
            g_client->SetLocalVehicleNetworkId(0);
            g_client->SetLocalVehicle(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }


    if (g_client && g_saUtils && g_saUtils->IsGameInitialised()) {
        for (const auto& test : g_client->DrainNpcSpawnTests()) SpawnTestNpc(test);
        UpdateRemotePlayers();
    }
}

void HookVehicleProcess(void* vehicle)
{
    if (g_originalVehicleProcess) g_originalVehicleProcess(vehicle);
    if (!g_client || !vehicle) return;

    CPlayerPed* localPlayer = FindPlayerPed(-1);
    CVehicle* localVehicle = reinterpret_cast<CVehicle*>(vehicle);
    if (localPlayer && GetSeatedVehicle(localPlayer) == localVehicle) {
        const CVector& vp = localVehicle->GetPosition();
        const CVector& vv = reinterpret_cast<CPhysical*>(localVehicle)->m_vecMoveSpeed;
        const uint32_t key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(localVehicle));
        g_client->SetLocalVehicle(key, localVehicle->m_nModelIndex,
                                  vp.x, vp.y, vp.z,
                                  HeadingDegrees(localVehicle),
                                  vv.x, vv.y, vv.z, localVehicle->m_fHealth, ([&](){ uint8_t seat=0; if(localVehicle->m_pDriver==localPlayer) return seat; for(uint8_t si=0;si<8;++si) if(localVehicle->m_apPassengers[si]==localPlayer) return (uint8_t)(si+1); return seat; })());
        CaptureLocalVehicleDetails(localVehicle);
    } else {


        ReapplyRemoteVehicleHeading(localVehicle);
    }
}
}

void DrawRemoteWeaponIcons()
{
    DrawRemoteWeaponIconsInternal();
}

void RenderRemotePlayerNameTags(ImDrawList* drawList)
{
    RenderRemotePlayerNameTagsInternal(drawList);
}

void InstallGameHooks(uintptr_t gameLibrary, CoopClient& client, ISAUtils* saUtils)
{
    g_game = gameLibrary;
    void* gameHandle = aml->GetLibHandle("libGTASA.so");
    if (gameHandle) {
        g_getFirstAssociation = reinterpret_cast<GetFirstAssociationFn>(aml->GetSym(gameHandle, "_Z35RpAnimBlendClumpGetFirstAssociationP7RpClump"));
        g_getNextAssociation = reinterpret_cast<GetNextAssociationFn>(aml->GetSym(gameHandle, "_Z29RpAnimBlendGetNextAssociationP21CAnimBlendAssociation"));
        g_removeAllAssociations = reinterpret_cast<RemoveAllAssociationsFn>(aml->GetSym(gameHandle, "_Z37RpAnimBlendClumpRemoveAllAssociationsP7RpClump"));
        g_blendAnimation = reinterpret_cast<BlendAnimationFn>(aml->GetSym(gameHandle, "_ZN12CAnimManager14BlendAnimationEP7RpClump12AssocGroupId11AnimationIdf"));
        g_blendAnimationHierarchy = reinterpret_cast<BlendAnimationHierarchyFn>(aml->GetSym(gameHandle, "_ZN12CAnimManager14BlendAnimationEP7RpClumpP19CAnimBlendHierarchyif"));
        g_setAssocBlend = reinterpret_cast<SetAssocBlendFn>(aml->GetSym(gameHandle, "_ZN21CAnimBlendAssociation8SetBlendEff"));
        g_setAssocTime = reinterpret_cast<SetAssocTimeFn>(aml->GetSym(gameHandle, "_ZN21CAnimBlendAssociation14SetCurrentTimeEf"));
        g_getAnimBlockName = reinterpret_cast<GetAnimBlockNameFn>(aml->GetSym(gameHandle, "_ZN12CAnimManager16GetAnimBlockNameE12AssocGroupId"));
        g_getAnimationBlockIndex = reinterpret_cast<GetAnimationBlockIndexFn>(aml->GetSym(gameHandle, "_ZN12CAnimManager22GetAnimationBlockIndexEPKc"));
        g_requestModel = reinterpret_cast<RequestModelFn>(aml->GetSym(gameHandle, "_ZN10CStreaming12RequestModelEii"));
        g_clumpIsInitialized = reinterpret_cast<ClumpIsInitializedFn>(aml->GetSym(gameHandle, "_Z29RpAnimBlendClumpIsInitializedP7RpClump"));
        WeaponFileLog("ANIM_SYNC block streaming: name=%d index=%d requestModel=%d clumpInit=%d",
                      g_getAnimBlockName != nullptr, g_getAnimationBlockIndex != nullptr, g_requestModel != nullptr, g_clumpIsInitialized != nullptr);
        WeaponFileLog("ANIM_SYNC symbols: first=%d next=%d blendByGroup=%d blendByHierarchy=%d setBlend=%d setTime=%d",
                      g_getFirstAssociation != nullptr, g_getNextAssociation != nullptr, g_blendAnimation != nullptr,
                      g_blendAnimationHierarchy != nullptr, g_setAssocBlend != nullptr, g_setAssocTime != nullptr);


        g_doGunFlash = reinterpret_cast<DoGunFlashFn>(aml->GetSym(gameHandle, "_ZN4CPed10DoGunFlashEib"));
        if (!g_doGunFlash) logger->Error("CPed::DoGunFlash symbol not found; remote muzzle flashes disabled");
        g_getWeaponInfoForIcon = reinterpret_cast<GetWeaponInfoForIconFn>(aml->GetSym(gameHandle, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponTypea"));
        g_requestWeaponIconModel = reinterpret_cast<RequestWeaponIconModelFn>(aml->GetSym(gameHandle, "_ZN10CStreaming12RequestModelEii"));
        g_loadWeaponIconModels = reinterpret_cast<LoadWeaponIconModelsFn>(aml->GetSym(gameHandle, "_ZN10CStreaming22LoadAllRequestedModelsEb"));
        g_drawWeaponIcon = reinterpret_cast<DrawWeaponIconFn>(aml->GetSym(gameHandle, "_ZN17CWidgetPlayerInfo14DrawWeaponIconEP4CPed5CRectf"));
        logger->Info("Weapon icon bridge: info=%d request=%d load=%d draw=%d",
                     g_getWeaponInfoForIcon != nullptr, g_requestWeaponIconModel != nullptr,
                     g_loadWeaponIconModels != nullptr, g_drawWeaponIcon != nullptr);
    }
    g_client = &client;
    g_saUtils = saUtils;
    uintptr_t specialCharRequest = reinterpret_cast<uintptr_t>(CStreaming::RequestSpecialChar);
    if (!specialCharRequest)
        specialCharRequest = aml->GetSym(g_game, "_ZN10CStreaming18RequestSpecialCharEiPKci");
    const bool specialCharHooked = specialCharRequest && aml->Hook(
        reinterpret_cast<void*>(specialCharRequest), reinterpret_cast<void*>(HookRequestSpecialChar),
        reinterpret_cast<void**>(&g_originalRequestSpecialChar));
    if (specialCharHooked) logger->Info("CStreaming::RequestSpecialChar identity hook installed");
    else logger->Error("CStreaming::RequestSpecialChar hook unavailable; unnamed special player/NPC models will safely fall back to CJ");
    Events::gameProcessEvent += OnGameProcess;
    const auto getBoundCentreSymbol = aml->GetSym(g_game, "_ZNK7CEntity14GetBoundCentreER7CVector");
    g_giveWeapon = reinterpret_cast<GiveWeaponFn>(aml->GetSym(g_game, "_ZN4CPed10GiveWeaponE11eWeaponTypejb"));
    g_setCurrentWeapon = reinterpret_cast<SetCurrentWeaponFn>(aml->GetSym(g_game, "_ZN4CPed16SetCurrentWeaponE11eWeaponType"));
    if (g_giveWeapon && g_setCurrentWeapon) logger->Info("Verified CPed weapon methods resolved");
    else logger->Error("CPed weapon methods unavailable; weapon visuals disabled");
    g_getBoundCentre = reinterpret_cast<gtasa_v200_armv7::EntityGetBoundCentreFn>(
        getBoundCentreSymbol ? getBoundCentreSymbol : g_game + gtasa_v200_armv7::kCEntityGetBoundCentre);
    g_setMoveState = reinterpret_cast<gtasa_v200_armv7::PedSetMoveStateFn>(g_game + gtasa_v200_armv7::kCPedSetMoveState);
    g_entityTeleport = reinterpret_cast<gtasa_v200_armv7::EntityTeleportFn>(g_game + gtasa_v200_armv7::kCPedTeleport);
    g_setMoveAnim = reinterpret_cast<gtasa_v200_armv7::PedSetMoveAnimFn>(g_game + gtasa_v200_armv7::kCPedSetMoveAnim);

    const auto ped = reinterpret_cast<gtasa_v200_armv7::ProcessControlFn>(g_game + gtasa_v200_armv7::kCPlayerPedProcessControl);

    if (g_saUtils) {
        g_saUtils->AddPlayerUpdateListener(OnSaUtilsPlayerUpdate, true);
        logger->Info("SAUtils post-player-update NPC listener installed");
    }

    const auto vehicle = reinterpret_cast<gtasa_v200_armv7::ProcessControlFn>(
        g_game + gtasa_v200_armv7::kCAutomobileProcessControl);
    const auto fireInstantHit = reinterpret_cast<void*>(g_game + gtasa_v200_armv7::kCWeaponFireInstantHit);

    const bool pedHooked = aml->Hook(reinterpret_cast<void*>(ped),
        reinterpret_cast<void*>(HookPedProcess), reinterpret_cast<void**>(&g_originalPedProcess));
    const bool vehicleHooked = aml->Hook(reinterpret_cast<void*>(vehicle),
        reinterpret_cast<void*>(HookVehicleProcess), reinterpret_cast<void**>(&g_originalVehicleProcess));
    const bool fireHooked = aml->Hook(fireInstantHit,
        reinterpret_cast<void*>(HookFireInstantHit), reinterpret_cast<void**>(&g_originalFireInstantHit));

    if (pedHooked) logger->Info("CPlayerPed::ProcessControl hook installed");
    else logger->Error("CPlayerPed::ProcessControl hook failed");
    if (vehicleHooked) logger->Info("CAutomobile::ProcessControl hook installed");
    else logger->Error("CAutomobile::ProcessControl hook failed");
    if (fireHooked) logger->Info("CWeapon::FireInstantHit hook installed");
    else logger->Error("CWeapon::FireInstantHit hook failed");

    InstallRemotePedAiHooks();
}

void SetRemoteNameTagsVisible(bool visible)
{
    g_remoteNameTagsVisible = visible;
}
void SetRemoteNameTagScale(float scale)
{
    g_remoteNameTagScale = std::max(0.5f, std::min(2.0f, scale));
}
void SetRemoteHealthBarsVisible(bool visible)
{
    g_remoteHealthBarsVisible = visible;
}
