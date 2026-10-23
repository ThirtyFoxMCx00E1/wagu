#include "coop_client.h"
#include <aml-psdk/game_sa/engine/Radar.h>
#include "../diagnostics/weapon_log.h"
#include "../world/special_models.h"
#include <android/log.h>
#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr char kTag[] = "CoopFusion32";

void logSocketError(const char* operation) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s: %s",
                        operation, std::strerror(errno));
}

bool isFiniteNumber(float value) {
    return std::isfinite(value);
}

float normalizeHeadingDegrees(float heading) {
    heading = std::fmod(heading, 360.0f);
    if (heading < 0.0f) {
        heading += 360.0f;
    }
    return heading;
}
}

void CoopClient::Configure(const char* host, int port, const char* nickname) {
    m_host = host ? host : "127.0.0.1";
    m_port = port > 0 ? port : 6767;
    m_nickname = nickname ? nickname : "CJ";
}

void CoopClient::SendChat(const char* rawText) {
    if (!rawText || !m_connected.load()) {
        return;
    }

    std::string messageText(rawText);
    for (char& character : messageText) {
        if (character == '|' || character == '\r' || character == '\n') {
            character = ' ';
        }
    }

    while (!messageText.empty() &&
           (messageText.front() == ' ' || messageText.front() == '\t')) {
        messageText.erase(messageText.begin());
    }

    while (!messageText.empty() &&
           (messageText.back() == ' ' || messageText.back() == '\t')) {
        messageText.pop_back();
    }

    if (messageText.empty()) {
        return;
    }

    if (messageText.size() > 160) {
        messageText.resize(160);
    }

    SendLine("CHATMSG|" + messageText + "\n");
}

std::vector<RemoteChatMessage> CoopClient::DrainChatMessages() {
    std::scoped_lock lock(m_mutex);
    auto messages = std::move(m_chatMessages);
    m_chatMessages.clear();
    return messages;
}

bool CoopClient::Connect() {
    Disconnect();

    addrinfo addressHints{};
    addressHints.ai_family = AF_INET;
    addressHints.ai_socktype = SOCK_STREAM;

    addrinfo* addressInfo = nullptr;
    const std::string portText = std::to_string(m_port);
    const int addressLookupStatus = getaddrinfo(
        m_host.c_str(), portText.c_str(), &addressHints, &addressInfo);

    if (addressLookupStatus != 0) {
        __android_log_print(
            ANDROID_LOG_ERROR, kTag,
            "getaddrinfo(%s:%s) failed: %s",
            m_host.c_str(), portText.c_str(), gai_strerror(addressLookupStatus));
        return false;
    }

    const int socketDescriptor = socket(
        addressInfo->ai_family,
        addressInfo->ai_socktype,
        addressInfo->ai_protocol);

    if (socketDescriptor < 0) {
        logSocketError("socket");
        freeaddrinfo(addressInfo);
        return false;
    }

    if (connect(socketDescriptor, addressInfo->ai_addr,
                addressInfo->ai_addrlen) < 0) {
        const std::string operation = "connect " + m_host + ":" + portText;
        logSocketError(operation.c_str());
        close(socketDescriptor);
        freeaddrinfo(addressInfo);
        return false;
    }

    freeaddrinfo(addressInfo);
    m_socket.store(socketDescriptor);
    m_connected = true;

    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "Connected to co-op server %s:%d as %s",
        m_host.c_str(), m_port, m_nickname.c_str());

    SendLine("JOIN|" + m_nickname + "|" +
             std::to_string(m_colorIndex.load()) + "\n");
    return true;
}





void CoopClient::ShutdownSocket() {
    const int socketDescriptor = m_socket.load();
    if (socketDescriptor >= 0) {
        shutdown(socketDescriptor, SHUT_RDWR);
    }
}

void CoopClient::CloseSocket() {
    const int socketDescriptor = m_socket.exchange(-1);
    if (socketDescriptor >= 0) {
        close(socketDescriptor);
    }
}

void CoopClient::Disconnect() {
    ShutdownSocket();
    m_connected = false;

    std::scoped_lock lock(m_mutex);
    m_remotePlayers.clear();
    m_remoteNpcs.clear();
    m_remoteVehicles.clear();
    m_removedVehicles.clear();
    m_removedPlayers.clear();
    m_shots.clear();
    m_chatMessages.clear();
    m_damages.clear();
    m_playerVitals.clear();
    m_vehicleEnters.clear();
    m_localVehicleNetworkId = 0;
    m_localVehicleIdsByKey.clear();
    m_rx.clear();
}

void CoopClient::Pump() {
 if (!m_connected) return;
 const int fd = m_socket.load();
 if (fd < 0) { m_connected = false; return; }
 char b[4096];
 auto n = recv(fd, b, sizeof(b), MSG_DONTWAIT);
 if (n > 0) {
 m_rx.append(b, (size_t)n);
 if (m_rx.size() > 65536) { m_rx.clear(); return; }
 size_t e;
 while ((e = m_rx.find('\n')) != std::string::npos) {
 auto line = m_rx.substr(0, e);
 m_rx.erase(0, e + 1);
 if (line.size() < 4096) ParseLine(line);
 }
 } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
 CloseSocket();
 m_connected = false;
 return;
 }

	uint16_t modelId = 0, vehicleNetworkId = 0; uint32_t vehicleKey = 0; uint8_t vehicleSeat = 0; char specialName[16]{}; LocalInput input{};
	{
	 std::scoped_lock lock(m_mutex);
	 modelId = m_modelId;
	 vehicleNetworkId = m_localVehicleNetworkId; vehicleKey = m_localVehicleKey; vehicleSeat = m_localVehicleSeat;
	 std::memcpy(specialName, m_specialModelName, sizeof(specialName));
	 input = m_input;
	 m_input.enterVehicle = false;
	 m_input.jump = false;
	}
 char p[256];
	std::snprintf(p, sizeof(p), "INPUT|%.3f|%.3f|%.3f|%.3f|%d|%d|%d|%.3f|%.3f|%.3f|%.3f|%u|%u|%u|%u|%u|%u|%s|%u|%u|%u|%u\n",
	 m_x.load(), m_y.load(), m_z.load(), m_heading.load(), input.fire, input.jump, input.enterVehicle,
	 input.moveX, input.moveY, input.aimX, input.aimY, modelId, m_animationState.load(),
	 m_weaponType.load(), m_health.load(), m_armour.load(), m_colorIndex.load(), specialName,
	 vehicleNetworkId, vehicleSeat, vehicleKey, static_cast<unsigned>(m_localAreaCode.load()));
	 if (m_weaponType.load() != m_lastLoggedSentWeapon) { WeaponFileLog("WEAPON_SEND id=local weapon=%u", m_weaponType.load()); m_lastLoggedSentWeapon = m_weaponType.load(); }
	 SendLine(p);
	 const uint64_t vehicleNow = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	 if (vehicleNow - m_lastVehicleSendMs >= 50) {
		 RemoteVehicle vehicle{}; uint32_t key = 0; uint8_t localSeat = 0; bool active = false;
		 { std::scoped_lock lock(m_mutex); active = m_localVehicleActive; key = m_localVehicleKey;
		   localSeat=m_localVehicleSeat; vehicle.driverSeat=localSeat;
		   vehicle.modelId = m_localVehicleModel; vehicle.x=m_localVehicleX; vehicle.y=m_localVehicleY; vehicle.z=m_localVehicleZ; vehicle.heading=m_localVehicleHeading;
		   vehicle.vx=m_localVehicleVx; vehicle.vy=m_localVehicleVy; vehicle.vz=m_localVehicleVz; vehicle.health=m_localVehicleHealth;
		   vehicle.primaryColor=m_vehiclePrimary; vehicle.secondaryColor=m_vehicleSecondary; vehicle.engineOn=m_vehicleEngine; vehicle.lightsOn=m_vehicleLights; vehicle.sirenOn=m_vehicleSiren; vehicle.doorLock=m_vehicleDoorLock; vehicle.dirt=m_vehicleDirt; vehicle.tyreStatus=m_vehicleTyres; vehicle.doorStatus=m_vehicleDoors; vehicle.lightStatus=m_vehicleLightDamage; vehicle.panelStatus=m_vehiclePanelDamage; vehicle.trailerId=m_vehicleTrailerId;
		   vehicle.rightX=m_vehicleRightX; vehicle.rightY=m_vehicleRightY; vehicle.rightZ=m_vehicleRightZ; vehicle.upX=m_vehicleUpX; vehicle.upY=m_vehicleUpY; vehicle.upZ=m_vehicleUpZ; vehicle.atX=m_vehicleAtX; vehicle.atY=m_vehicleAtY; vehicle.atZ=m_vehicleAtZ; vehicle.orientationValid=m_vehicleOrientationValid; }
		 if (active && key) SendVehicleState(key, vehicle);
		 m_lastVehicleSendMs = vehicleNow;
	 }

 AnimationProfile animation{}; { std::scoped_lock lock(m_mutex); animation = m_animationProfile; }
 const uint64_t animationNow = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 if (animationNow - m_lastAnimationSendMs >= 66) {
 char ap[256];
 std::snprintf(ap, sizeof(ap), "ANIM2|%u|%u|%u|%u|%u|%u|%u|%.4f|%.4f|%.4f|%.4f\n",
 static_cast<unsigned>(m_localId.load()), animation.sequence,
 static_cast<unsigned>(animation.locomotion), static_cast<unsigned>(animation.action),
 static_cast<unsigned>(animation.flags), static_cast<unsigned>(animation.weaponState),
 static_cast<unsigned>(animation.animGroup), animation.phase, animation.blend,
 animation.aimYaw, animation.aimPitch);
 SendLine(ap); m_lastAnimationSendMs = animationNow;
 }
 if (animationNow - m_lastAssociationSendMs >= 66) {
 uint8_t associationCount = 0; std::array<AnimationAssociationState,64> associations{};
 { std::scoped_lock lock(m_mutex); associationCount = m_animationAssociationCount; associations = m_animationAssociations; }
 char header[96]; std::snprintf(header, sizeof(header), "ANIM3|%u|%u", static_cast<unsigned>(m_localId.load()), static_cast<unsigned>(associationCount));
 std::string packet(header); char field[128];
 for (uint8_t i = 0; i < associationCount; ++i) {
 const auto& a = associations[i];
 std::snprintf(field, sizeof(field), "|%u|%d|%u|%.5f|%.5f|%.5f|%.5f|%.5f", a.group, a.animationId, a.flags, a.blend, a.blendDelta, a.currentTime, a.speed, a.timeStep);
 packet += field;
 }
 packet += "\n"; SendLine(packet); m_lastAssociationSendMs = animationNow;
 }
}

void CoopClient::SetLocalTransform(float x, float y, float z, float h) { m_x.store(x); m_y.store(y); m_z.store(z); m_heading.store(h); }
void CoopClient::SetLocalAreaCode(uint8_t areaCode) { m_localAreaCode.store(areaCode); }
void CoopClient::SetLocalVehicle(uint32_t key, uint16_t model, float x, float y, float z, float heading, float vx, float vy, float vz, float health, uint8_t seat) {
    std::scoped_lock lock(m_mutex);
    if (key != m_localVehicleKey) {



        auto known = m_localVehicleIdsByKey.find(key);
        m_localVehicleNetworkId = key && known != m_localVehicleIdsByKey.end() ? known->second : 0;
    }
    m_localVehicleActive = key != 0 && seat == 0;
    m_localVehicleKey = key; m_localVehicleModel = model;
    m_localVehicleX=x; m_localVehicleY=y; m_localVehicleZ=z; m_localVehicleHeading=heading;
    m_localVehicleVx=vx; m_localVehicleVy=vy; m_localVehicleVz=vz; m_localVehicleHealth=health; m_localVehicleSeat=seat;
    if (!key) { m_localVehicleNetworkId=0; m_vehicleOrientationValid=false; }
}
void CoopClient::SetLocalVehicleNetworkId(uint16_t id) { if (!id) return; std::scoped_lock lock(m_mutex); m_localVehicleNetworkId=id; }
uint16_t CoopClient::LocalVehicleNetworkIdForKey(uint32_t key) const {
    if (!key) return 0;
    std::scoped_lock lock(m_mutex);
    auto it = m_localVehicleIdsByKey.find(key);
    return it == m_localVehicleIdsByKey.end() ? 0 : it->second;
}
void CoopClient::SetLocalVehicleDetails(uint8_t primary,uint8_t secondary,uint8_t engine,uint8_t lights,uint8_t siren,uint8_t doorLock,float dirt,uint8_t tyres,uint32_t doors,uint32_t lightsDamage,uint32_t panels,uint16_t trailerId) {
	 std::scoped_lock lock(m_mutex); m_vehiclePrimary=primary; m_vehicleSecondary=secondary; m_vehicleEngine=engine; m_vehicleLights=lights; m_vehicleSiren=siren; m_vehicleDoorLock=doorLock; m_vehicleDirt=std::isfinite(dirt)?std::max(0.0f,std::min(15.0f,dirt)):0.0f; m_vehicleTyres=tyres; m_vehicleDoors=doors; m_vehicleLightDamage=lightsDamage; m_vehiclePanelDamage=panels; m_vehicleTrailerId=trailerId;
}
void CoopClient::SetLocalVehicleOrientation(float rx,float ry,float rz,float ux,float uy,float uz,float ax,float ay,float az) {
	 if (!isFiniteNumber(rx)||!isFiniteNumber(ry)||!isFiniteNumber(rz)||!isFiniteNumber(ux)||!isFiniteNumber(uy)||!isFiniteNumber(uz)||!isFiniteNumber(ax)||!isFiniteNumber(ay)||!isFiniteNumber(az)) return;
	 std::scoped_lock lock(m_mutex); m_vehicleRightX=rx; m_vehicleRightY=ry; m_vehicleRightZ=rz; m_vehicleUpX=ux; m_vehicleUpY=uy; m_vehicleUpZ=uz; m_vehicleAtX=ax; m_vehicleAtY=ay; m_vehicleAtZ=az; m_vehicleOrientationValid=true;
}
void CoopClient::SetLocalAnimationState(uint8_t s) { m_animationState.store(s <= kMoveSprint ? s : kMoveStill); }
void CoopClient::SetLocalModel(uint16_t id, const char* specialName) {
 char normalized[16]{};
 coop_special::NormalizeName(specialName, normalized, sizeof(normalized));
 uint16_t oldModel = 0; char oldSpecial[16]{};
 { std::scoped_lock lock(m_mutex); oldModel = m_modelId; std::memcpy(oldSpecial, m_specialModelName, sizeof(oldSpecial)); m_modelId = id <= 311 ? id : 0; std::memcpy(m_specialModelName, normalized, sizeof(m_specialModelName)); }
 if (oldModel != m_modelId || std::strcmp(oldSpecial, normalized) != 0) WeaponFileLog("LOCAL_MODEL_CAPTURE old=%hu new=%hu special=%s", oldModel, m_modelId, normalized);
}
void CoopClient::SetLocalWeaponType(uint8_t w) { if (w <= 0x3A) m_weaponType.store(w); }
void CoopClient::SetLocalHealthArmour(uint16_t health, uint16_t armour) { m_health.store(health > 100 ? 100 : health); m_armour.store(armour > 100 ? 100 : armour); }
void CoopClient::SetLocalColorIndex(uint8_t c) { m_colorIndex.store(c <= 7 ? c : 0); }
void CoopClient::SetInput(const LocalInput& i) { std::scoped_lock lock(m_mutex); m_input = i; }


void CoopClient::SendBoneSnapshot(const NetworkBone* bones, uint8_t count) {
 if (!bones || count == 0 || count > 19) return;
 std::string p = "BONES2|" + std::to_string((unsigned)m_localId.load()) + "|" + std::to_string(count);
 char b[96];
 for (uint8_t i = 0; i < count; ++i) {
 std::snprintf(b, sizeof(b), "|%u|%.5f|%.5f|%.5f|%.5f", bones[i].index, bones[i].qx, bones[i].qy, bones[i].qz, bones[i].qw);
 p += b;
 }
 p += "\n";
 SendLine(p);
}

void CoopClient::SendNpcState(uint32_t localRef, uint16_t modelId, float x, float y, float z, float heading, uint16_t health, uint16_t armour, uint8_t weapon, uint8_t moveState, uint32_t vehicleKey, uint8_t vehicleSeat, const char* specialModelName, uint16_t vehicleNetworkId) {
 char normalized[coop_special::kNameCapacity]{};
 if (modelId >= coop_special::kFirstModelIndex && modelId <= coop_special::kLastModelIndex)
 coop_special::NormalizeName(specialModelName, normalized, sizeof(normalized));
 char p[384];
	 std::snprintf(p, sizeof(p), "NPC_STATE|%u|%u|%.3f|%.3f|%.3f|%.3f|%u|%u|%u|%u|%u|%u|%s|%u\n",
	 localRef, modelId, x, y, z, heading, health, armour, weapon, moveState, vehicleKey, vehicleSeat, normalized, vehicleNetworkId);
 SendLine(p);
}
void CoopClient::SendNpcVehicleState(uint32_t key,const RemoteVehicle& state) { if (key) SendVehicleState(key,state); }
void CoopClient::SendNpcVehicleRemove(uint32_t key) { if (key) SendLine("VEHICLE_REMOVE_KEY|" + std::to_string(key) + "\n"); }
void CoopClient::SendNpcHealth(uint32_t npcId, uint16_t health, uint16_t armour) { if (!npcId) return; char p[96]; std::snprintf(p, sizeof(p), "NPC_HEALTH|%u|%u|%u\n", npcId, std::min<uint16_t>(100, health), std::min<uint16_t>(100, armour)); SendLine(p); }
void CoopClient::SendPlayerHealth(uint16_t playerId, uint16_t health, uint16_t armour) { if (!playerId) return; char p[96]; std::snprintf(p, sizeof(p), "PLAYER_HEALTH|%u|%u|%u\n", playerId, std::min<uint16_t>(100, health), std::min<uint16_t>(100, armour)); SendLine(p); }
void CoopClient::SendNpcRemove(uint32_t localRef) { SendLine("NPC_REMOVE|" + std::to_string(localRef) + "\n"); }
void CoopClient::SendMissionMarker(uint16_t sourceSlot, uint8_t type, uint8_t display, uint8_t sprite,
                                    uint16_t scale, uint32_t color, float x, float y, float z, float radius) {
  if (sourceSlot > 174 || (display != 1 && display != 3) ||
      !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(radius)) return;
  char p[256];
  std::snprintf(p, sizeof(p), "MISSION_MARKER|%u|%u|%u|%u|%u|%u|%.3f|%.3f|%.3f|%.3f\n",
                sourceSlot, type, display, sprite, scale, color, x, y, z,
                std::max(0.1f, std::min(100.0f, radius)));
  SendLine(p);
}
void CoopClient::SendMissionMarkerRemove(uint16_t sourceSlot) {
  if (sourceSlot <= 174) SendLine("MISSION_MARKER_REMOVE|" + std::to_string(sourceSlot) + "\n");
}

void CoopClient::SendShot(uint8_t w, float x, float y, float z, float dx, float dy, float dz, uint16_t animGroup, uint16_t animId) {
 char p[224];
 int n = std::snprintf(p, sizeof(p), "SHOT|%u|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f", w, x, y, z, dx, dy, dz);

 if (animGroup != 0 && (animId == kAnimationFireId || animId == kAnimationCrouchFireId) && n > 0 && n < static_cast<int>(sizeof(p)) - 24)
  n += std::snprintf(p + n, sizeof(p) - n, "|%u|%u", static_cast<unsigned>(animGroup), static_cast<unsigned>(animId));
 std::snprintf(p + n, sizeof(p) - n, "\n");
 WeaponFileLog("SHOT_PACKET_SEND weapon=%u fire=%u/%u", w, static_cast<unsigned>(animGroup), static_cast<unsigned>(animId)); SendLine(p);
}
void CoopClient::SendVehicleEnter(uint16_t id,uint8_t seat) { if (id) SendLine("VEHICLE_ENTER|" + std::to_string(id) + "|" + std::to_string(seat) + "\n"); }
void CoopClient::SendVehicleClaim(uint16_t id,uint8_t seat,uint32_t key) { if (id && key) SendLine("VEHICLE_CLAIM|" + std::to_string(id) + "|" + std::to_string(seat) + "|" + std::to_string(key) + "\n"); }

std::vector<RemotePlayer> CoopClient::GetRemotePlayers() { std::scoped_lock l(m_mutex); return m_remotePlayers; }
std::vector<RemoteNpc> CoopClient::GetRemoteNpcs() { std::scoped_lock l(m_mutex); return m_remoteNpcs; }
std::vector<uint32_t> CoopClient::DrainRemovedNpcs() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedNpcs); m_removedNpcs.clear(); return x; }

std::vector<RemoteMissionMarker> CoopClient::GetRemoteMissionMarkers() { std::scoped_lock l(m_mutex); return m_remoteMissionMarkers; }
std::vector<uint32_t> CoopClient::DrainRemovedMissionMarkers() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedMissionMarkers); m_removedMissionMarkers.clear(); return x; }std::vector<RemoteVehicle> CoopClient::GetRemoteVehicles() { std::scoped_lock l(m_mutex); return m_remoteVehicles; }
std::vector<uint16_t> CoopClient::DrainRemovedVehicles() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedVehicles); m_removedVehicles.clear(); return x; }
std::vector<RemotePlayerRemoved> CoopClient::DrainRemovedPlayers() { std::scoped_lock l(m_mutex); auto x = std::move(m_removedPlayers); m_removedPlayers.clear(); return x; }
std::vector<RemoteShot> CoopClient::DrainShots() { std::scoped_lock l(m_mutex); auto x = std::move(m_shots); m_shots.clear(); return x; }
std::vector<RemoteDamage> CoopClient::DrainDamages() { std::scoped_lock l(m_mutex); auto x = std::move(m_damages); m_damages.clear(); return x; }
std::vector<RemoteVehicleEnter> CoopClient::DrainVehicleEnters() { std::scoped_lock l(m_mutex); auto x = std::move(m_vehicleEnters); m_vehicleEnters.clear(); return x; }
std::vector<NpcSpawnTest> CoopClient::DrainNpcSpawnTests() { std::scoped_lock l(m_mutex); auto x = std::move(m_npcSpawnTests); m_npcSpawnTests.clear(); return x; }
uint16_t CoopClient::LocalId() const { return m_localId; }
bool CoopClient::IsConnected() const { return m_connected; }

void CoopClient::SendLine(const std::string& s) {

    std::scoped_lock sendLock(m_sendMutex);
	 const int fd = m_socket.load();
 if (fd < 0) return;
 size_t sent = 0;
 while (sent < s.size()) {
 auto n = send(fd, s.data() + sent, s.size() - sent, MSG_NOSIGNAL);
 if (n <= 0) { logSocketError("send"); return; }
	 sent += (size_t)n;
	 }
}

void CoopClient::SendVehicleState(uint32_t localKey,const RemoteVehicle& v) {
	 if (!localKey || v.modelId < 400 || v.modelId > 611 || !isFiniteNumber(v.x) || !isFiniteNumber(v.y) || !isFiniteNumber(v.z) || !isFiniteNumber(v.heading) || !isFiniteNumber(v.vx) || !isFiniteNumber(v.vy) || !isFiniteNumber(v.vz) || !isFiniteNumber(v.health)) return;
	 char p[768];
	 std::snprintf(p,sizeof(p),"VEHICLE_STATE|%u|%u|%.3f|%.3f|%.3f|%.3f|%.4f|%.4f|%.4f|%.1f|%u|%u|%u|%u|%u|%u|%u|%.3f|%u|%u|%u|%u|%u|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f\n",
	 localKey,v.modelId,v.x,v.y,v.z,v.heading,v.vx,v.vy,v.vz,v.health,v.driverSeat,v.primaryColor,v.secondaryColor,v.engineOn,v.lightsOn,v.sirenOn,v.doorLock,v.dirt,v.tyreStatus,v.doorStatus,v.lightStatus,v.panelStatus,v.trailerId,
	 v.orientationValid?v.rightX:1.0f,v.orientationValid?v.rightY:0.0f,v.orientationValid?v.rightZ:0.0f,v.orientationValid?v.upX:0.0f,v.orientationValid?v.upY:1.0f,v.orientationValid?v.upZ:0.0f,v.orientationValid?v.atX:0.0f,v.orientationValid?v.atY:0.0f,v.orientationValid?v.atZ:1.0f);
	 SendLine(p);
}

void CoopClient::ParseLine(const std::string& line) {
 if (line.rfind("WELCOME|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "WELCOME|%u", &id) == 1 && id <= 65535) {
 m_localId = (uint16_t)id; m_timeAuthorityId = 0; m_hasRemoteGameTime = false;
 m_sentGameTime = false; m_forceGameTimeSend = false; m_lastGameTimeSendMs = 0;
 std::scoped_lock l(m_mutex); m_gameTimes.clear();
 }
 return;
 }
	 if (line.rfind("VEHICLE_ID|", 0) == 0) {
		 unsigned key=0,id=0;
		 if (std::sscanf(line.c_str(),"VEHICLE_ID|%u|%u",&key,&id)==2 && key && id && id<=65535) {
			 std::scoped_lock lock(m_mutex);
             m_localVehicleIdsByKey[static_cast<uint32_t>(key)] = static_cast<uint16_t>(id);
			 if (key==m_localVehicleKey) {
                 m_localVehicleNetworkId=static_cast<uint16_t>(id);




                 const uint16_t localId = m_localId.load();
                 for (auto& vehicle : m_remoteVehicles) {
                     if (vehicle.id == static_cast<uint16_t>(id)) {
                         vehicle.ownerId = localId;
                         vehicle.driverSeat = 0;
                     }
                 }
             }
		 }
		 return;
	 }
	 if (line.rfind("TIME_OWNER|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "TIME_OWNER|%u", &id) == 1 && id <= 65535) {
 m_timeAuthorityId = (uint16_t)id;
 if (id && id == m_localId.load()) m_forceGameTimeSend = true;
 }
 return;
 }
 if (line.rfind("TIME|", 0) == 0) {
 unsigned hour = 0, minute = 0, day = 0;
 if (std::sscanf(line.c_str(), "TIME|%u|%u|%u", &hour, &minute, &day) == 3 && hour <= 23 && minute <= 59 && day >= 1 && day <= 31) {
 RemoteGameTime t{static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), static_cast<uint8_t>(day)};
 if (!m_hasRemoteGameTime || t.hour != m_lastRemoteGameTime.hour || t.minute != m_lastRemoteGameTime.minute || t.day != m_lastRemoteGameTime.day) {
 std::scoped_lock l(m_mutex); m_gameTimes.push_back(t); m_lastRemoteGameTime = t; m_hasRemoteGameTime = true;
 }
 }
 return;
 }
 if (line.rfind("NPC_VITALS|", 0) == 0) {
 RemoteNpcVitals e{}; unsigned owner = 0;
 if (std::sscanf(line.c_str(), "NPC_VITALS|%u|%u|%u|%hu|%hu", &e.id, &owner, &e.localRef, &e.health, &e.armour) == 5 && e.id && owner <= 65535 && e.localRef && e.health <= 100 && e.armour <= 100) {
 e.ownerId = (uint16_t)owner; std::scoped_lock l(m_mutex); m_npcVitals.push_back(e);
 }
 return;
 }
 if (line.rfind("PLAYER_VITALS|", 0) == 0) {
 RemotePlayerVitals e{};
 if (std::sscanf(line.c_str(), "PLAYER_VITALS|%hu|%hu|%hu", &e.id, &e.health, &e.armour) == 3 && e.id && e.health <= 100 && e.armour <= 100) {
 std::scoped_lock l(m_mutex); m_playerVitals.push_back(e);
 if (e.id != m_localId.load()) {
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& p){ return p.id == e.id; });
 if (it != m_remotePlayers.end()) { it->health = e.health; it->armour = e.armour; }
 }
 }
 return;
 }
 if (line.rfind("NPC_DAMAGE|", 0) == 0) {
 RemoteNpcDamage e{}; unsigned w = 0;
 if (std::sscanf(line.c_str(), "NPC_DAMAGE|%u|%u|%hu|%hu|%u", &e.id, &e.localRef, &e.health, &e.armour, &w) == 5 && e.id && e.localRef && e.health <= 100 && e.armour <= 100 && w <= 0x3A) {
 e.weapon = (uint8_t)w; std::scoped_lock l(m_mutex); m_npcDamages.push_back(e);
 }
 return;
 }
 if (line.rfind("NPC_TEST|", 0) == 0) {
 NpcSpawnTest t{}; int threat = 1; unsigned weapon = 1;
 int n = std::sscanf(line.c_str(), "NPC_TEST|%d|%f|%f|%f|%f|%u|%d", &t.modelId, &t.x, &t.y, &t.z, &t.heading, &weapon, &threat);
 if (n != 7) n = std::sscanf(line.c_str(), "NPC_TEST|%d|%f|%f|%f|%f|%d", &t.modelId, &t.x, &t.y, &t.z, &t.heading, &threat);
 if ((n == 7 || n == 6) && weapon <= 0x3A) {
 t.weapon = (uint8_t)weapon; t.threat = threat != 0;
 std::scoped_lock l(m_mutex); m_npcSpawnTests.push_back(t);
 __android_log_print(ANDROID_LOG_INFO, kTag, "NPC_TEST packet queued: model=%d weapon=%u pos=(%.1f,%.1f,%.1f)", t.modelId, weapon, t.x, t.y, t.z);
 }
 return;
 }
 if (line.rfind("BONES2|", 0) == 0) {
 std::vector<std::string> f; size_t b = 7;
 while (b <= line.size()) { size_t e = line.find('|', b); if (e == std::string::npos) e = line.size(); f.emplace_back(line.substr(b, e - b)); if (e == line.size()) break; b = e + 1; }
 if (f.size() >= 2) {
 unsigned id = (unsigned)std::strtoul(f[0].c_str(), nullptr, 10), count = (unsigned)std::strtoul(f[1].c_str(), nullptr, 10);
 if (id && id <= 65535 && count <= 19 && f.size() == 2 + count * 5) {
 std::scoped_lock l(m_mutex);
 for (auto& rp : m_remotePlayers) if (rp.id == id) {
 rp.boneCount = (uint8_t)count;
 for (unsigned i = 0; i < count; ++i) {
 size_t o = 2 + i * 5;
 rp.bones[i].index = (uint8_t)std::strtoul(f[o].c_str(), nullptr, 10);
 rp.bones[i].qx = std::strtof(f[o+1].c_str(), nullptr);
 rp.bones[i].qy = std::strtof(f[o+2].c_str(), nullptr);
 rp.bones[i].qz = std::strtof(f[o+3].c_str(), nullptr);
 rp.bones[i].qw = std::strtof(f[o+4].c_str(), nullptr);
 }
 break;
 }
 }
 }
 return;
 }
 if (line.rfind("NPC_NET|", 0) == 0) {
 RemoteNpc n{}; unsigned id=0, owner=0, ref=0, model=0, weapon=0, move=1, vehicle=0, seat=0; char specialName[16]{};
 int c = std::sscanf(line.c_str(), "NPC_NET|%u|%u|%u|%u|%f|%f|%f|%f|%hu|%hu|%u|%u|%u|%u|%15[^|\r\n]",
 &id, &owner, &ref, &model, &n.x, &n.y, &n.z, &n.heading, &n.health, &n.armour, &weapon, &move, &vehicle, &seat, specialName);
 if ((c == 12 || c == 14 || c == 15) && id && owner <= 65535 && ref && model <= 2000 && weapon <= 0x3A && move <= 7 && vehicle <= 65535 && seat <= 8 &&
 isFiniteNumber(n.x) && isFiniteNumber(n.y) && isFiniteNumber(n.z) && isFiniteNumber(n.heading)) {
 n.id = id; n.ownerId = (uint16_t)owner; n.localRef = ref; n.modelId = (uint16_t)model;
 n.weapon = (uint8_t)weapon; n.moveState = (uint8_t)move; n.vehicleId = (uint16_t)vehicle; n.vehicleSeat = (uint8_t)seat;
 if (c == 15 && n.modelId >= coop_special::kFirstModelIndex && n.modelId <= coop_special::kLastModelIndex)
 coop_special::NormalizeName(specialName, n.specialModelName, sizeof(n.specialModelName));
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remoteNpcs.begin(), m_remoteNpcs.end(), [&](const RemoteNpc& q){ return q.id == n.id; });
 if (it == m_remoteNpcs.end()) m_remoteNpcs.push_back(n); else *it = n;
 }
 return;
 }
 if (line.rfind("MISSION_MARKER|", 0) == 0) {
 RemoteMissionMarker m{}; unsigned owner=0, slot=0, type=0, display=0, sprite=0, scale=1, color=0;
 int c = std::sscanf(line.c_str(), "MISSION_MARKER|%u|%u|%u|%u|%u|%u|%u|%u|%f|%f|%f|%f",
     &m.id, &owner, &slot, &type, &display, &sprite, &scale, &color,
     &m.x, &m.y, &m.z, &m.radius);
 if (c == 12 && m.id && owner <= 65535 && slot <= 65535 && type <= 8 &&
     (display == 1 || display == 3) && sprite < RADAR_SPRITE_COUNT &&
     scale <= 65535 && std::isfinite(m.x) && std::isfinite(m.y) &&
     std::isfinite(m.z) && std::isfinite(m.radius)) {
   m.ownerId = static_cast<uint16_t>(owner);
   m.sourceSlot = static_cast<uint16_t>(slot);
   m.type = static_cast<uint8_t>(type);
   m.display = static_cast<uint8_t>(display);
   m.sprite = static_cast<uint8_t>(sprite);
   m.scale = static_cast<uint16_t>(scale);
   m.color = color;
   m.radius = std::max(0.1f, std::min(100.0f, m.radius));
   std::scoped_lock l(m_mutex);
   auto it = std::find_if(m_remoteMissionMarkers.begin(), m_remoteMissionMarkers.end(),
       [&](const RemoteMissionMarker& q){ return q.id == m.id; });
   if (it == m_remoteMissionMarkers.end()) m_remoteMissionMarkers.push_back(m);
   else *it = m;
 }
 return;
 }
 if (line.rfind("MISSION_MARKER_REMOVE|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "MISSION_MARKER_REMOVE|%u", &id) == 1 && id) {
   std::scoped_lock l(m_mutex);
   m_remoteMissionMarkers.erase(std::remove_if(m_remoteMissionMarkers.begin(), m_remoteMissionMarkers.end(),
     [&](const RemoteMissionMarker& m){ return m.id == id; }), m_remoteMissionMarkers.end());
   m_removedMissionMarkers.push_back(id);
 }
 return;
 }
 if (line.rfind("NPC_REMOVE|", 0) == 0) {
 unsigned id = 0;
 if (std::sscanf(line.c_str(), "NPC_REMOVE|%u", &id) == 1 && id) {
 std::scoped_lock l(m_mutex);
 m_remoteNpcs.erase(std::remove_if(m_remoteNpcs.begin(), m_remoteNpcs.end(), [&](const RemoteNpc& n){ return n.id == id; }), m_remoteNpcs.end());
 m_removedNpcs.push_back(id);
 }
 return;
 }
 if (line.rfind("REMOVE|", 0) == 0) {
 RemotePlayerRemoved e{};
 if (std::sscanf(line.c_str(), "REMOVE|%hu", &e.id) == 1) {
 std::scoped_lock l(m_mutex);
 m_remotePlayers.erase(std::remove_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& p){ return p.id == e.id; }), m_remotePlayers.end());
 m_removedPlayers.push_back(e);
 }
 return;
 }
 if (line.rfind("SHOT|", 0) == 0) {
 RemoteShot e{}; unsigned w = 0, group = 0, anim = 0;
 const int shotFields = std::sscanf(line.c_str(), "SHOT|%hu|%u|%f|%f|%f|%f|%f|%f|%u|%u", &e.id, &w, &e.x, &e.y, &e.z, &e.dx, &e.dy, &e.dz, &group, &anim);
 if ((shotFields == 8 || shotFields == 10) && w <= 255 &&
 isFiniteNumber(e.x) && isFiniteNumber(e.y) && isFiniteNumber(e.z) && isFiniteNumber(e.dx) && isFiniteNumber(e.dy) && isFiniteNumber(e.dz)) {
 if (shotFields == 10 && group != 0 && group <= 0xFFFF && (static_cast<int>(anim) == kAnimationFireId || static_cast<int>(anim) == kAnimationCrouchFireId)) {
    e.animGroup = static_cast<uint16_t>(group); e.animId = static_cast<uint16_t>(anim);
 }
 e.weapon = (uint8_t)w; std::scoped_lock l(m_mutex); m_shots.push_back(e);
 }
 return;
 }
 if (line.rfind("DAMAGE|", 0) == 0) {
 RemoteDamage e{}; unsigned w = 0;
 if (std::sscanf(line.c_str(), "DAMAGE|%hu|%hu|%hu|%u", &e.targetId, &e.health, &e.armour, &w) == 4 && w <= 255 && e.health <= 100 && e.armour <= 100) {
 e.weapon = (uint8_t)w;
 WeaponFileLog("DAMAGE_PACKET_RECEIVE target=%hu health=%hu armour=%hu weapon=%u", e.targetId, e.health, e.armour, e.weapon);
 std::scoped_lock l(m_mutex); m_damages.push_back(e);
 }
 return;
 }
 if (line.rfind("VEHICLE_ENTER|", 0) == 0) {
 RemoteVehicleEnter e{}; unsigned seat = 0;
 if (std::sscanf(line.c_str(), "VEHICLE_ENTER|%hu|%hu|%u", &e.id, &e.vehicleId, &seat) == 3 && seat <= 255) {
 e.seat = (uint8_t)seat; std::scoped_lock l(m_mutex); m_vehicleEnters.push_back(e);
 }
 return;
 }
 if (line.rfind("VEHICLE_REMOVE|", 0) == 0) {
 uint16_t id = 0;
 if (std::sscanf(line.c_str(), "VEHICLE_REMOVE|%hu", &id) == 1) {
 std::scoped_lock l(m_mutex);
 m_remoteVehicles.erase(std::remove_if(m_remoteVehicles.begin(), m_remoteVehicles.end(), [&](const RemoteVehicle& v){ return v.id == id; }), m_remoteVehicles.end());
 for (auto it = m_localVehicleIdsByKey.begin(); it != m_localVehicleIdsByKey.end();) {
     if (it->second == id) it = m_localVehicleIdsByKey.erase(it); else ++it;
 }
 if (m_localVehicleNetworkId == id) m_localVehicleNetworkId = 0;
 m_removedVehicles.push_back(id);
 }
 return;
 }
 if (line.rfind("VEHICLE|", 0) == 0) {
 RemoteVehicle v{}; unsigned model=0, owner=0, seat=0, primary=0, secondary=0, engine=0, lights=0, siren=0, lock=0, tyres=0, trailer=0;
 int n = std::sscanf(line.c_str(), "VEHICLE|%hu|%u|%u|%u|%f|%f|%f|%f|%f|%f|%f|%f|%u|%u|%u|%u|%u|%u|%f|%u|%u|%u|%u|%u|%f|%f|%f|%f|%f|%f|%f|%f|%f",
 &v.id, &model, &owner, &seat, &v.x, &v.y, &v.z, &v.heading, &v.vx, &v.vy, &v.vz, &v.health,
 &primary, &secondary, &engine, &lights, &siren, &lock, &v.dirt, &tyres, &v.doorStatus, &v.lightStatus, &v.panelStatus, &trailer,
 &v.rightX, &v.rightY, &v.rightZ, &v.upX, &v.upY, &v.upZ, &v.atX, &v.atY, &v.atZ);
	 if ((n == 12 || n == 24 || n == 33) && v.id && model >= 400 && model <= 611 && owner <= 65535 && seat <= 7 &&
 isFiniteNumber(v.x) && isFiniteNumber(v.y) && isFiniteNumber(v.z) && isFiniteNumber(v.heading) && isFiniteNumber(v.vx) && isFiniteNumber(v.vy) && isFiniteNumber(v.vz) && std::isfinite(v.health)) {
 v.modelId = (uint16_t)model; v.ownerId = (uint16_t)owner; v.driverSeat = (uint8_t)seat;
 if (n >= 24) {
 v.primaryColor=(uint8_t)primary; v.secondaryColor=(uint8_t)secondary; v.engineOn=(uint8_t)engine; v.lightsOn=(uint8_t)lights; v.sirenOn=(uint8_t)siren;
 v.doorLock=(uint8_t)lock; v.tyreStatus=(uint8_t)tyres; v.trailerId=(uint16_t)trailer; if (n == 33) v.orientationValid = true;
 }
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remoteVehicles.begin(), m_remoteVehicles.end(), [&](const RemoteVehicle& q){ return q.id == v.id; });
 if (it == m_remoteVehicles.end()) m_remoteVehicles.push_back(v); else *it = v;
 }
 return;
 }
 if (line.rfind("CHAT|", 0) == 0) {
     const size_t fromStart = 5;
     const size_t sep = line.find('|', fromStart);
     if (sep != std::string::npos) {
         RemoteChatMessage msg{};
         const std::string from = line.substr(fromStart, std::min<size_t>(sep - fromStart, sizeof(msg.from) - 1));
         const std::string body = line.substr(sep + 1, sizeof(msg.text) - 1);
         std::snprintf(msg.from, sizeof(msg.from), "%s", from.c_str());
         std::snprintf(msg.text, sizeof(msg.text), "%s", body.c_str());
         msg.system = from == "SERVER";
         std::scoped_lock lock(m_mutex);
         if (m_chatMessages.size() >= 80) m_chatMessages.erase(m_chatMessages.begin());
         m_chatMessages.push_back(msg);
     }
     return;
 }
 if (line.rfind("NAME|", 0) == 0) {
 uint16_t id = 0; unsigned color = 0; char remoteName[15] = {0};
 const int parsed = std::sscanf(line.c_str(), "NAME|%hu|%14[^|\r\n]|%u", &id, remoteName, &color);
 if ((parsed == 2 || parsed == 3) && id != 0) {
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == id; });
 if (it != m_remotePlayers.end()) { std::snprintf(it->name, sizeof(it->name), "%s", remoteName); if (parsed == 3 && color <= 7) it->colorIndex = (uint8_t)color; }
 }
 return;
 }
 if (line.rfind("ANIM3|", 0) == 0) {
 std::vector<std::string> f; size_t b = 6;
 while (b <= line.size()) { size_t e = line.find('|', b); if (e == std::string::npos) e = line.size(); f.emplace_back(line.substr(b, e - b)); if (e == line.size()) break; b = e + 1; }
 if (f.size() >= 2) {
 char* e = nullptr; const unsigned id = std::strtoul(f[0].c_str(), &e, 10); const unsigned count = std::strtoul(f[1].c_str(), &e, 10);
 if (id && count <= 64 && f.size() == 2 + count * 8) {
 std::array<AnimationAssociationState,64> states{}; bool valid = true;
 for (unsigned i = 0; i < count; ++i) {
 const size_t o = 2 + i * 8; auto& a = states[i];
 a.group = static_cast<uint16_t>(std::strtoul(f[o].c_str(), &e, 10));
 a.animationId = static_cast<int16_t>(std::strtol(f[o+1].c_str(), &e, 10));
 a.flags = static_cast<uint16_t>(std::strtoul(f[o+2].c_str(), &e, 10));
 a.blend = std::strtof(f[o+3].c_str(), &e); a.blendDelta = std::strtof(f[o+4].c_str(), &e);
 a.currentTime = std::strtof(f[o+5].c_str(), &e); a.speed = std::strtof(f[o+6].c_str(), &e); a.timeStep = std::strtof(f[o+7].c_str(), &e);
 valid &= std::isfinite(a.blend) && std::isfinite(a.blendDelta) && std::isfinite(a.currentTime) && std::isfinite(a.speed) && std::isfinite(a.timeStep);
 }
 if (valid) {
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == id; });
 if (it != m_remotePlayers.end()) { it->animationAssociationCount = static_cast<uint8_t>(count); it->animationAssociations = states; }
 }
 }
 }
 return;
 }
 if (line.rfind("ANIM2|", 0) == 0) {
 std::vector<std::string> f; size_t b = 6;
 while (b <= line.size()) { size_t e = line.find('|', b); if (e == std::string::npos) e = line.size(); f.emplace_back(line.substr(b, e - b)); if (e == line.size()) break; b = e + 1; }
 if (f.size() == 11) {
 char* e = nullptr; const unsigned id = std::strtoul(f[0].c_str(), &e, 10);
 AnimationProfile profile{};
 profile.sequence = static_cast<uint32_t>(std::strtoul(f[1].c_str(), &e, 10));
 profile.locomotion = static_cast<uint8_t>(std::strtoul(f[2].c_str(), &e, 10));
 profile.action = static_cast<AnimationAction>(std::strtoul(f[3].c_str(), &e, 10));
 profile.flags = static_cast<uint16_t>(std::strtoul(f[4].c_str(), &e, 10));
 profile.weaponState = static_cast<uint8_t>(std::strtoul(f[5].c_str(), &e, 10));
 profile.animGroup = static_cast<uint16_t>(std::strtoul(f[6].c_str(), &e, 10));
 profile.phase = std::strtof(f[7].c_str(), &e); profile.blend = std::strtof(f[8].c_str(), &e);
 profile.aimYaw = std::strtof(f[9].c_str(), &e); profile.aimPitch = std::strtof(f[10].c_str(), &e);
 if (id && profile.locomotion <= kMoveSprint && static_cast<unsigned>(profile.action) <= static_cast<unsigned>(AnimationAction::Swim) &&
 std::isfinite(profile.phase) && std::isfinite(profile.blend) && std::isfinite(profile.aimYaw) && std::isfinite(profile.aimPitch)) {
 profile.phase = std::max(0.0f, std::min(1.0f, profile.phase)); profile.blend = std::max(0.0f, std::min(1.0f, profile.blend));
 std::scoped_lock l(m_mutex);
 auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == id; });
 if (it != m_remotePlayers.end() && profile.sequence >= it->animation.sequence) { it->animation = profile; it->animationState = profile.locomotion; }
 }
 }
 return;
 }
 if (line.rfind("PLAYER|", 0) != 0) return;
 std::vector<std::string> fields;
 size_t begin = 7;
 while (begin <= line.size()) {
 size_t end = line.find('|', begin);
 if (end == std::string::npos) end = line.size();
 fields.emplace_back(line.substr(begin, end - begin));
 if (end == line.size()) break;
 begin = end + 1;
 }

 RemotePlayer p{};
 unsigned a = 0, w = 0, vehicleId = 0, vehicleSeat = 0;
 bool hasName = false;
 auto get = [&](size_t i) -> const char* { return i < fields.size() ? fields[i].c_str() : ""; };

 if (fields.size() >= 12) {
 char* e = nullptr;
 p.id = (uint16_t)std::strtoul(get(0), &e, 10); if (!e || *e) return;
 const unsigned long parsedModel = std::strtoul(get(1), &e, 10); if (!e || *e) return;
 if (parsedModel > 311) { WeaponFileLog("REMOTE_MODEL_REJECT id=%hu model=%lu fallback=0", p.id, parsedModel); p.modelId = 0; }
 else { p.modelId = static_cast<uint16_t>(parsedModel); }
 a = (unsigned)std::strtoul(get(2), &e, 10); if (!e || *e) return;
 p.x = std::strtof(get(3), &e); if (!e || *e) return;
 p.y = std::strtof(get(4), &e); if (!e || *e) return;
 p.z = std::strtof(get(5), &e); if (!e || *e) return;
 p.heading = std::strtof(get(6), &e); if (!e || *e) return;
 p.health = (uint16_t)std::strtoul(get(7), &e, 10); if (!e || *e) return;
 p.armour = (uint16_t)std::strtoul(get(8), &e, 10); if (!e || *e) return;
 w = (unsigned)std::strtoul(get(9), &e, 10); if (!e || *e) return;
 vehicleId = (unsigned)std::strtoul(get(10), &e, 10); if (!e || *e) return;
 vehicleSeat = (unsigned)std::strtoul(get(11), &e, 10); if (!e || *e) return;
 if (fields.size() >= 13 && !fields[12].empty()) { std::snprintf(p.name, sizeof(p.name), "%s", fields[12].c_str()); hasName = true; }
 if (fields.size() >= 14 && !fields[13].empty()) {
 unsigned parsedColor = (unsigned)std::strtoul(get(13), &e, 10);
 if (e && *e == '\0' && parsedColor <= 7) p.colorIndex = (uint8_t)parsedColor;
 }
 if (fields.size() >= 15 && p.modelId >= coop_special::kFirstModelIndex && p.modelId <= coop_special::kLastModelIndex)
 coop_special::NormalizeName(get(14), p.specialModelName, sizeof(p.specialModelName));
 if (fields.size() >= 16) {
     char* areaEnd = nullptr;
     const unsigned long parsedArea = std::strtoul(get(15), &areaEnd, 10);
     if (areaEnd && *areaEnd == '\0' && parsedArea <= 255) p.areaCode = static_cast<uint8_t>(parsedArea);
 }
 } else if (fields.size() >= 8) {
 char* e = nullptr;
 p.id = (uint16_t)std::strtoul(get(0), &e, 10); if (!e || *e) return;
 p.x = std::strtof(get(1), &e); if (!e || *e) return;
 p.y = std::strtof(get(2), &e); if (!e || *e) return;
 p.z = std::strtof(get(3), &e); if (!e || *e) return;
 p.heading = std::strtof(get(4), &e); if (!e || *e) return;
 p.health = (uint16_t)std::strtoul(get(5), &e, 10); if (!e || *e) return;
 p.armour = (uint16_t)std::strtoul(get(6), &e, 10); if (!e || *e) return;
 w = (unsigned)std::strtoul(get(7), &e, 10); if (!e || *e) return;
 p.modelId = 7; a = kMoveStill;
 } else return;

 if (vehicleId <= 65535 && vehicleSeat <= 7) { p.vehicleId = (uint16_t)vehicleId; p.vehicleSeat = (uint8_t)vehicleSeat; }
 if (!hasName) {
 p.name[0] = '\0';
 std::scoped_lock l(m_mutex);
 auto old = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == p.id; });
 if (old != m_remotePlayers.end()) std::snprintf(p.name, sizeof(p.name), "%s", old->name);
 }
 if (p.id == 0 || a > kMoveSprint || w > 255 || !isFiniteNumber(p.x) || !isFiniteNumber(p.y) || !isFiniteNumber(p.z) || !isFiniteNumber(p.heading)) return;
 p.heading = normalizeHeadingDegrees(p.heading); p.animationState = (uint8_t)a; p.weapon = (uint8_t)w;
 auto weaponIt = m_lastLoggedRemoteWeapons.find(p.id);
 if (weaponIt == m_lastLoggedRemoteWeapons.end() || weaponIt->second != p.weapon) { WeaponFileLog("WEAPON_REMOTE_RECEIVE id=%hu weapon=%u", p.id, p.weapon); m_lastLoggedRemoteWeapons[p.id] = p.weapon; }
	std::scoped_lock l(m_mutex);
	auto it = std::find_if(m_remotePlayers.begin(), m_remotePlayers.end(), [&](const RemotePlayer& x){ return x.id == p.id; });
	if (it == m_remotePlayers.end() || it->modelId != p.modelId || std::strcmp(it->specialModelName, p.specialModelName) != 0)
	WeaponFileLog("REMOTE_MODEL_RECEIVE id=%hu model=%hu special=%s", p.id, p.modelId, p.specialModelName);
	if (it == m_remotePlayers.end()) {
	 m_remotePlayers.push_back(p);
	} else {



	 if (fields.size() < 16) p.areaCode = it->areaCode;
	 p.animation = it->animation;
	 p.animationAssociations = it->animationAssociations;
	 p.animationAssociationCount = it->animationAssociationCount;
	 p.bones = it->bones;
	 p.boneCount = it->boneCount;
	 *it = p;
	}
}

void CoopClient::SetLocalAnimationProfile(const AnimationProfile& profile) {
 AnimationProfile sanitized = profile;
 if (sanitized.locomotion > kMoveSprint) sanitized.locomotion = kMoveStill;
 if (static_cast<unsigned>(sanitized.action) > static_cast<unsigned>(AnimationAction::Swim)) sanitized.action = AnimationAction::Idle;
 sanitized.phase = std::max(0.0f, std::min(1.0f, sanitized.phase));
 sanitized.blend = std::max(0.0f, std::min(1.0f, sanitized.blend));
 std::scoped_lock lock(m_mutex); m_animationProfile = sanitized; m_animationState.store(sanitized.locomotion);
}

void CoopClient::SetLocalAnimationAssociations(const AnimationAssociationState* states, uint8_t count) {
 if (count > 64) count = 64;
 std::scoped_lock lock(m_mutex);
 m_animationAssociationCount = count;
 if (states && count) std::copy(states, states + count, m_animationAssociations.begin());
}

void CoopClient::SetLocalGameTime(uint8_t hour, uint8_t minute, uint8_t day) {
 if (hour > 23 || minute > 59 || day < 1 || day > 31) return;
 m_localGameHour.store(hour); m_localGameMinute.store(minute); m_localGameDay.store(day); m_localGameTimeValid.store(true);
}

void CoopClient::PumpGameTime() {
 const uint16_t localId = m_localId.load();
 if (!m_connected || !localId || m_timeAuthorityId.load() != localId || !m_localGameTimeValid.load()) return;
 const uint8_t hour = m_localGameHour.load(), minute = m_localGameMinute.load(), day = m_localGameDay.load();
 const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 const bool changed = !m_sentGameTime || hour != m_lastSentGameHour || minute != m_lastSentGameMinute || day != m_lastSentGameDay;
 if (m_forceGameTimeSend || changed || now - m_lastGameTimeSendMs >= 5000) {
 char packet[64]; std::snprintf(packet, sizeof(packet), "TIME|%u|%u|%u\n", hour, minute, day);
 SendLine(packet);
 m_lastSentGameHour = hour; m_lastSentGameMinute = minute; m_lastSentGameDay = day;
 m_lastGameTimeSendMs = now; m_sentGameTime = true; m_forceGameTimeSend = false;
 }
}
