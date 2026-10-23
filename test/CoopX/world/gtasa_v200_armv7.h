#pragma once

#include <cstdint>

namespace gtasa_v200_armv7
{
struct CVectorAbi { float x; float y; float z; };

constexpr char kSha256[] = "46c1a97f5423520e4965850cc034648e8a2a11c95f55b24788a4fa208506b9db";
constexpr char kLibrary[] = "libGTASA.so";

constexpr uintptr_t kCPlayerPedProcessControl = 0x004C4779;
constexpr uintptr_t kCPedProcessControl = 0x004A24D1;
constexpr uintptr_t kCAutomobileProcessControl = 0x00553DD5;
constexpr uintptr_t kCAutomobileProcessControlInputs = 0x0055C511;
constexpr uintptr_t kCWorldProcess = 0x004276F5;
constexpr uintptr_t kCTimerUpdate = 0x00420BE5;
constexpr uintptr_t kCWeaponFireInstantHit = 0x005DC129;
constexpr uintptr_t kCTouchInterfaceUpdate = 0x002B0B01;
constexpr uintptr_t kCTouchInterfaceGetNumTouches = 0x002B0A71;
constexpr uintptr_t kCTouchInterfaceIsTouched = 0x002B0CBD;
constexpr uintptr_t kCTouchInterfaceIsReleased = 0x002B0D5D;
constexpr uintptr_t kCTouchInterfaceGetTouchDelta = 0x002B2629;
constexpr uintptr_t kCPedNew = 0x0049F8C1;
constexpr uintptr_t kCPedConstructor = 0x0049F041;
constexpr uintptr_t kCWorldAdd = 0x004233C9;
constexpr uintptr_t kCEntityTeleport = 0x00280211;
constexpr uintptr_t kCEntityGetBoundCentre = 0x003EBF95;
constexpr uintptr_t kCPedModelIndexOffset = 0x26;
constexpr uintptr_t kCPedSetMoveState = 0x004A0BC1;
constexpr uintptr_t kCPedSetMoveAnim = 0x004A0BC9;
constexpr uintptr_t kCStreamingRequestModel = 0x002D292D;
constexpr uintptr_t kCStreamingLoadAllRequestedModels = 0x002D4639;
constexpr uintptr_t kCRadarSetCoordBlip = 0x00442721;
constexpr uintptr_t kCRadarSetBlipSprite = 0x00442B91;
constexpr uintptr_t kCPedTeleport = 0x004A71C1;

constexpr uintptr_t kCPedUpdatePosition = 0x004A1929;
constexpr uintptr_t kCPadGetPad = 0x003F8CA5;
constexpr uintptr_t kCPedDoGunFlash = 0x004A546D;
constexpr uintptr_t kCRunningScriptProcessOneCommand = 0x0032B6B9;
constexpr uintptr_t kCPlayerPedConstructor = 0x004C360D;
constexpr uintptr_t kCAnimManagerBlendAnimation = 0x0038E2F1;

using ProcessControlFn = void (*)(void* self);
using ProcessControlInputsFn = void (*)(void* self, unsigned char playerNumber);
using TouchUpdateFn = void (*)();
using GetNumTouchesFn = int (*)();
using IsTouchedFn = bool (*)(int widgetId, void* vector2d, int pointerId);
using GetTouchDeltaFn = void* (*)(int pointerId);






using FireInstantHitFn = void (*)(void* self, void* firingEntity, void* origin, void* muzzlePosn,
                                  void* targetEntity, void* target, void* originForDriveBy, bool arg6, bool muzzle);
using PedNewFn = void* (*)(unsigned int size);
using PedConstructorFn = void (*)(void* self, unsigned int modelId);
using WorldAddFn = void (*)(void* entity);
using EntityTeleportFn = void (*)(void* self, struct CVectorAbi position, unsigned char resetArea);
using EntityGetBoundCentreFn = void (*)(const void* self, struct CVectorAbi& result);
using PedSetMoveStateFn = void (*)(void* self, int moveState);
using PedSetMoveAnimFn = void (*)(void* self);
using RequestModelFn = void (*)(int modelId, int streamingFlags);
using LoadModelsFn = void (*)(bool onlyPriority);
using RadarSetCoordBlipFn = int (*)(int blipType, struct CVectorAbi position, unsigned int color, int display, char* name);
}
