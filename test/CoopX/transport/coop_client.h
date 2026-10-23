#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../world/sync_types.h"


enum : uint8_t {
    kMoveNone = 0,
    kMoveStill = 1,
    kMoveTurnLeft = 2,
    kMoveTurnRight = 3,
    kMoveWalk = 4,
    kMoveJog = 5,
    kMoveRun = 6,
    kMoveSprint = 7,
};

struct RemotePlayer {
    uint16_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float heading = 0.0f;
    uint8_t areaCode = 0;
    uint16_t health = 100;
    uint16_t armour = 0;
    uint8_t weapon = 0;
    uint16_t modelId = 7;
    uint8_t animationState = kMoveStill;
    AnimationProfile animation{};
    uint16_t vehicleId = 0;
    uint8_t vehicleSeat = 0;
    char name[15] = "CJ";
    char specialModelName[16]{};
    uint8_t colorIndex = 0;
    std::array<NetworkBone, 19> bones{};
    uint8_t boneCount = 0;
    std::array<AnimationAssociationState, 64> animationAssociations{};
    uint8_t animationAssociationCount = 0;
};

struct RemoteVehicle {
    uint16_t id = 0;
    uint16_t modelId = 0;
    uint16_t ownerId = 0;
    uint8_t driverSeat = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float heading = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float health = 1000.0f;
    uint8_t primaryColor = 0;
    uint8_t secondaryColor = 0;
    uint8_t engineOn = 0;
    uint8_t lightsOn = 0;
    uint8_t sirenOn = 0;
    uint8_t doorLock = 0;
    uint8_t tyreStatus = 0;
    float dirt = 0.0f;
    uint32_t lightStatus = 0;
    uint32_t panelStatus = 0;
    uint32_t doorStatus = 0;
    uint16_t trailerId = 0;
    float rightX = 1.0f;
    float rightY = 0.0f;
    float rightZ = 0.0f;
    float upX = 0.0f;
    float upY = 1.0f;
    float upZ = 0.0f;
    float atX = 0.0f;
    float atY = 0.0f;
    float atZ = 1.0f;
    bool orientationValid = false;
};

struct RemotePlayerRemoved {
    uint16_t id = 0;
};

struct RemoteChatMessage {
    char from[16]{};
    char text[161]{};
    bool system = false;
};

struct RemotePlayerVitals {
    uint16_t id = 0;
    uint16_t health = 100;
    uint16_t armour = 0;
};

struct RemoteShot {
    uint16_t id = 0;
    uint8_t weapon = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    uint16_t animGroup = 0;
    uint16_t animId = 0;
};

struct RemoteDamage {
    uint16_t targetId = 0;
    uint16_t health = 0;
    uint16_t armour = 0;
    uint8_t weapon = 0;
};

struct RemoteVehicleEnter {
    uint16_t id = 0;
    uint16_t vehicleId = 0;
    uint8_t seat = 0;
};

struct LocalInput {
    float moveX = 0.0f;
    float moveY = 0.0f;
    float aimX = 0.0f;
    float aimY = 0.0f;
    bool fire = false;
    bool jump = false;
    bool enterVehicle = false;
};

struct NpcSpawnTest {
    int modelId = 265;
    float x = 2495.0f;
    float y = -1687.0f;
    float z = 13.0f;
    float heading = 90.0f;
    uint8_t weapon = 1;
    bool threat = true;
};

class CoopClient {
public:
    void Configure(const char* host, int port, const char* nickname);
    void SendChat(const char* text);
    std::vector<RemoteChatMessage> DrainChatMessages();
    bool Connect();
    void Disconnect();
    void ShutdownSocket();
    void CloseSocket();
    void Pump();

    void SetLocalTransform(float x, float y, float z, float heading);
    void SetLocalAreaCode(uint8_t areaCode);
    void SetLocalAnimationState(uint8_t moveState);
    void SetLocalAnimationProfile(const AnimationProfile& profile);
    void SetLocalAnimationAssociations(
        const AnimationAssociationState* states, uint8_t count);
    void SetLocalModel(uint16_t modelId, const char* specialModelName);
    void SetLocalWeaponType(uint8_t weaponType);
    void SetLocalHealthArmour(uint16_t health, uint16_t armour);
    void SetLocalColorIndex(uint8_t colorIndex);
    void SetInput(const LocalInput& input);

    void SendShot(uint8_t weapon, float x, float y, float z,
                  float dx, float dy, float dz,
                  uint16_t animGroup = 0, uint16_t animId = 0);
    void SendBoneSnapshot(const NetworkBone* bones, uint8_t count);
    void SendNpcState(
        uint32_t localRef, uint16_t modelId,
        float x, float y, float z, float heading,
        uint16_t health, uint16_t armour, uint8_t weapon,
        uint8_t moveState, uint32_t vehicleKey, uint8_t vehicleSeat,
        const char* specialModelName = "", uint16_t vehicleNetworkId = 0);
    void SendNpcVehicleState(uint32_t vehicleKey, const RemoteVehicle& state);
    void SendNpcVehicleRemove(uint32_t vehicleKey);
    void SendNpcHealth(uint32_t npcId, uint16_t health, uint16_t armour);
    void SendPlayerHealth(uint16_t playerId, uint16_t health, uint16_t armour);
    void SendNpcRemove(uint32_t localRef);
    void SendMissionMarker(uint16_t sourceSlot, uint8_t type, uint8_t display,
                           uint8_t sprite, uint16_t scale, uint32_t color,
                           float x, float y, float z, float radius);
    void SendMissionMarkerRemove(uint16_t sourceSlot);
    void SendVehicleEnter(uint16_t vehicleId, uint8_t seat);
    void SendVehicleClaim(uint16_t vehicleId, uint8_t seat, uint32_t ownerKey);

    void SetLocalVehicleNetworkId(uint16_t id);
    uint16_t LocalVehicleNetworkIdForKey(uint32_t clientVehicleKey) const;
    void SetLocalVehicle(uint32_t clientVehicleKey, uint16_t modelId,
                         float x, float y, float z, float heading,
                         float vx, float vy, float vz, float health,
                         uint8_t seat);
    void SetLocalVehicleDetails(
        uint8_t primary, uint8_t secondary, uint8_t engine,
        uint8_t lights, uint8_t siren, uint8_t doorLock, float dirt,
        uint8_t tyres, uint32_t doors, uint32_t lightsDamage,
        uint32_t panels, uint16_t trailerId);
    void SetLocalVehicleOrientation(
        float rightX, float rightY, float rightZ,
        float upX, float upY, float upZ,
        float atX, float atY, float atZ);

    void SetLocalGameTime(uint8_t hour, uint8_t minute, uint8_t day);
    void PumpGameTime();

    std::vector<RemoteGameTime> DrainGameTimes() {
        std::scoped_lock lock(m_mutex);
        auto events = std::move(m_gameTimes);
        m_gameTimes.clear();
        return events;
    }

    std::vector<RemoteNpcDamage> DrainNpcDamages() {
        std::scoped_lock lock(m_mutex);
        auto events = std::move(m_npcDamages);
        m_npcDamages.clear();
        return events;
    }

    std::vector<RemoteNpcVitals> DrainNpcVitals() {
        std::scoped_lock lock(m_mutex);
        auto events = std::move(m_npcVitals);
        m_npcVitals.clear();
        return events;
    }

    std::vector<RemotePlayerVitals> DrainPlayerVitals() {
        std::scoped_lock lock(m_mutex);
        auto events = std::move(m_playerVitals);
        m_playerVitals.clear();
        return events;
    }

    std::vector<RemoteVehicle> GetRemoteVehicles();
    std::vector<uint16_t> DrainRemovedVehicles();
    std::vector<RemotePlayer> GetRemotePlayers();
    std::vector<RemotePlayerRemoved> DrainRemovedPlayers();
    std::vector<RemoteShot> DrainShots();
    std::vector<RemoteDamage> DrainDamages();
    std::vector<RemoteVehicleEnter> DrainVehicleEnters();
    std::vector<NpcSpawnTest> DrainNpcSpawnTests();
    std::vector<RemoteNpc> GetRemoteNpcs();
    std::vector<uint32_t> DrainRemovedNpcs();
    std::vector<RemoteMissionMarker> GetRemoteMissionMarkers();
    std::vector<uint32_t> DrainRemovedMissionMarkers();
    uint16_t LocalId() const;
    bool IsConnected() const;

private:
    void SendLine(const std::string& line);
    void SendVehicleState(uint32_t localKey, const RemoteVehicle& state);
    void ParseLine(const std::string& line);





    std::atomic<int> m_socket{-1};

    std::string m_host = "127.0.0.1";
    int m_port = 6767;
    std::string m_nickname = "CJ";
    mutable std::mutex m_mutex;
    std::mutex m_sendMutex;

    std::vector<RemoteChatMessage> m_chatMessages;
    std::vector<RemotePlayer> m_remotePlayers;
    std::vector<RemoteNpc> m_remoteNpcs;
    std::vector<RemoteVehicle> m_remoteVehicles;
    std::vector<uint16_t> m_removedVehicles;
    std::vector<uint32_t> m_removedNpcs;
    std::vector<RemotePlayerRemoved> m_removedPlayers;
    std::vector<RemoteShot> m_shots;
    std::vector<RemoteDamage> m_damages;
    std::vector<RemoteVehicleEnter> m_vehicleEnters;
    std::vector<NpcSpawnTest> m_npcSpawnTests;
    LocalInput m_input;
    std::vector<RemoteNpcDamage> m_npcDamages;
    std::vector<RemoteNpcVitals> m_npcVitals;
    std::vector<RemotePlayerVitals> m_playerVitals;
    std::vector<RemoteMissionMarker> m_remoteMissionMarkers;
    std::vector<uint32_t> m_removedMissionMarkers;

    bool m_localVehicleActive = false;
    bool m_localVehicleExitPending = false;
    std::vector<RemoteGameTime> m_gameTimes;
    std::unordered_map<uint32_t, uint16_t> m_localVehicleIdsByKey;
    std::atomic<uint16_t> m_timeAuthorityId{0};

    std::atomic<uint8_t> m_localGameHour{0};
    std::atomic<uint8_t> m_localGameMinute{0};
    std::atomic<uint8_t> m_localGameDay{1};
    std::atomic<bool> m_localGameTimeValid{false};
    uint8_t m_lastSentGameHour = 0;
    uint8_t m_lastSentGameMinute = 0;
    uint8_t m_lastSentGameDay = 1;
    uint64_t m_lastGameTimeSendMs = 0;
    bool m_sentGameTime = false;
    bool m_forceGameTimeSend = false;
    bool m_hasRemoteGameTime = false;
    RemoteGameTime m_lastRemoteGameTime{};

    uint32_t m_localVehicleKey = 0;
    uint16_t m_localVehicleNetworkId = 0;
    uint16_t m_localVehicleModel = 0;
    uint8_t m_localVehicleSeat = 0;
    uint8_t m_vehiclePrimary = 0;
    uint8_t m_vehicleSecondary = 0;
    uint8_t m_vehicleEngine = 0;
    uint8_t m_vehicleLights = 0;
    uint8_t m_vehicleSiren = 0;
    uint8_t m_vehicleDoorLock = 0;
    uint8_t m_vehicleTyres = 0;
    float m_vehicleDirt = 0.0f;
    uint32_t m_vehicleDoors = 0;
    uint32_t m_vehicleLightDamage = 0;
    uint32_t m_vehiclePanelDamage = 0;
    uint16_t m_vehicleTrailerId = 0;

    float m_vehicleRightX = 1.0f;
    float m_vehicleRightY = 0.0f;
    float m_vehicleRightZ = 0.0f;
    float m_vehicleUpX = 0.0f;
    float m_vehicleUpY = 1.0f;
    float m_vehicleUpZ = 0.0f;
    float m_vehicleAtX = 0.0f;
    float m_vehicleAtY = 0.0f;
    float m_vehicleAtZ = 1.0f;
    bool m_vehicleOrientationValid = false;

    char m_specialModelName[16]{};
    float m_localVehicleX = 0.0f;
    float m_localVehicleY = 0.0f;
    float m_localVehicleZ = 0.0f;
    float m_localVehicleHeading = 0.0f;
    float m_localVehicleVx = 0.0f;
    float m_localVehicleVy = 0.0f;
    float m_localVehicleVz = 0.0f;
    float m_localVehicleHealth = 1000.0f;


    std::atomic<float> m_x{0.0f};
    std::atomic<float> m_y{0.0f};
    std::atomic<float> m_z{0.0f};
    std::atomic<float> m_heading{0.0f};
    std::atomic<uint8_t> m_localAreaCode{0};
    std::atomic<uint8_t> m_animationState{kMoveStill};
    std::atomic<uint8_t> m_weaponType{0};
    std::atomic<uint8_t> m_colorIndex{0};
    AnimationProfile m_animationProfile{};
    uint16_t m_modelId = 7;
    std::atomic<uint16_t> m_health{100};
    std::atomic<uint16_t> m_armour{0};
    std::atomic<uint16_t> m_localId{0};
    std::atomic<bool> m_connected{false};
    std::string m_rx;

    uint8_t m_lastLoggedSentWeapon = 0xFF;
    std::unordered_map<uint16_t, uint8_t> m_lastLoggedRemoteWeapons;
    uint64_t m_lastBoneSendMs = 0;
    uint64_t m_lastAnimationSendMs = 0;
    uint64_t m_lastAssociationSendMs = 0;
    uint64_t m_lastVehicleSendMs = 0;
    std::array<AnimationAssociationState, 64> m_animationAssociations{};
    uint8_t m_animationAssociationCount = 0;
};
