#include "embedded_server.h"
#include "../world/special_models.h"

#include <android/log.h>
#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string>
#include <unordered_map>
#include <vector>





namespace {
constexpr char kTag[] = "CoopFusion32 Host";
constexpr size_t kMaxLine = 4096;
constexpr uint16_t kMaxPlayers = 64;
constexpr uint32_t kMaxNpcId = 1000000;




constexpr uint64_t kPlayerBroadcastMs = 15;
constexpr uint64_t kVehicleBroadcastMs = 50;

constexpr size_t kMaxPendingBytes = 4 * 1024 * 1024;



constexpr uint64_t kHealthResumeDelayMs = 1000;

using Parts = std::vector<std::string>;

struct Bone {
    uint8_t index = 0;
    float qx = 0, qy = 0, qz = 0, qw = 1;
};

struct AnimationAssociation {
    uint16_t group = 0;
    int16_t animationId = 0;
    uint16_t flags = 0;
    float blend = 0, blendDelta = 0, currentTime = 0, speed = 1, timeStep = 0;
};


struct AnimationState {
    uint32_t sequence = 0;
    uint8_t locomotion = 1, action = 1, weaponState = 0;
    uint16_t flags = 0x30, animGroup = 0;
    float phase = 0, blend = 1, aimYaw = 0, aimPitch = 0;
};



struct HealthReport {
    uint16_t health = 100, armour = 0;
};

struct Client {
    int fd = -1;
    uint16_t id = 0;
    bool joined = false;
    std::string rx;
    std::string tx;
    std::string name = "CJ";
    uint8_t color = 0, weapon = 0, move = 1;
    uint16_t health = 100, armour = 0, model = 7;

    uint16_t vehicleId = 0;
    uint8_t vehicleSeat = 0;
    char specialModelName[16]{};
    bool vitalsInitialized = false;
    uint64_t healthReportResumeAtMs = 0;
    float x = 0, y = 0, z = 3, heading = 0;
    uint8_t areaCode = 0;
    std::unordered_map<uint16_t, HealthReport> healthReports;
    AnimationState animation;
    std::vector<Bone> bones;
    std::vector<AnimationAssociation> associations;
};

struct Npc {
    uint32_t id = 0;
    uint16_t ownerId = 0;
    uint32_t localRef = 0;
    uint16_t model = 7;
    char specialModelName[16]{};
    float x = 0, y = 0, z = 0, heading = 0;
    uint16_t health = 100, armour = 0;
    uint8_t weapon = 0, move = 1;
    uint16_t vehicleId = 0;
    uint8_t vehicleSeat = 0;
    std::unordered_map<uint16_t, HealthReport> healthReports;
};

struct Vehicle {
    uint16_t id=0,ownerId=0,model=0,driverSeat=0,trailerId=0;
    uint32_t localKey=0;
    float x=0,y=0,z=0,heading=0,vx=0,vy=0,vz=0,health=1000,dirt=0;
    uint8_t primary=0,secondary=0,engine=0,lights=0,siren=0,doorLock=0,tyres=0;
    uint32_t doors=0,lightStatus=0,panelStatus=0;
    float rightX=1,rightY=0,rightZ=0,upX=0,upY=1,upZ=0,atX=0,atY=0,atZ=1;
};

struct MissionMarker {
    uint32_t id=0;
    uint16_t ownerId=0, sourceSlot=0;
    uint8_t type=0, display=0, sprite=0;
    uint16_t scale=1;
    uint32_t color=0;
    float x=0,y=0,z=0,radius=1;
};

std::mutex g_serverMutex;
std::vector<Client> g_clients;
std::unordered_map<uint64_t, Npc> g_npcs;
std::unordered_map<uint64_t, MissionMarker> g_missionMarkers;
std::unordered_map<uint16_t, Vehicle> g_vehicles;
std::unordered_map<uint64_t, uint16_t> g_vehicleKeys;
uint16_t g_nextId = 1;
uint32_t g_nextNpcId = 1;
uint16_t g_nextVehicleId = 1;
bool g_clockValid = false;
uint8_t g_clockHour = 0, g_clockMinute = 0, g_clockDay = 1;

uint64_t NpcKey(uint16_t owner, uint32_t ref) { return (static_cast<uint64_t>(owner) << 32) | ref; }
uint64_t VehicleKey(uint16_t owner, uint32_t key) { return (static_cast<uint64_t>(owner) << 32) | key; }
uint64_t MissionMarkerKey(uint16_t owner, uint16_t slot) { return (static_cast<uint64_t>(owner) << 16) | slot; }
uint32_t MissionMarkerId(uint16_t owner, uint16_t slot) { return 0x80000000u | (static_cast<uint32_t>(owner) << 16) | slot; }

std::string MissionMarkerSnapshot(const MissionMarker& m) {
    char b[256];
    std::snprintf(b, sizeof(b),
        "MISSION_MARKER|%u|%u|%u|%u|%u|%u|%u|%u|%.3f|%.3f|%.3f|%.3f\n",
        m.id, m.ownerId, m.sourceSlot, m.type, m.display, m.sprite, m.scale, m.color,
        m.x, m.y, m.z, m.radius);
    return b;
}

uint16_t AllocateVehicleId() {
    for (unsigned i = 0; i < 65535; ++i) {
        uint16_t id = g_nextVehicleId++;
        if (!g_nextVehicleId) g_nextVehicleId = 1;
        if (id && !g_vehicles.count(id)) return id;
    }
    return 0;
}

uint64_t MonotonicMilliseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string SafeName(const std::string& name) {
    std::string out = name;
    for (char& c : out) if (c == '|' || c == '\r' || c == '\n') c = '_';
    if (out.empty()) out = "CJ";
    if (out.size() > 14) out.resize(14);
    return out;
}


bool ParseInteger(const char* s, long long& out) {
    if (!s || !*s) return false;
    char* e = nullptr;
    out = std::strtoll(s, &e, 10);
    return e && *e == 0;
}

bool ParseDouble(const char* s, double& out) {
    if (!s || !*s) return false;
    char* e = nullptr;
    out = std::strtod(s, &e);
    return e && *e == 0 && std::isfinite(out);
}

float Float(const char* s, float fallback) {
    char* e = nullptr;
    float n = std::strtof(s, &e);
    return e && *e == 0 && std::isfinite(n) ? n : fallback;
}

unsigned UNum(const char* s, unsigned fallback) {
    char* e = nullptr;
    unsigned long n = std::strtoul(s, &e, 10);
    return e && *e == 0 ? static_cast<unsigned>(n) : fallback;
}

const char* At(const Parts& p, size_t k) { return k < p.size() ? p[k].c_str() : ""; }

float FloatOr(const Parts& p, size_t k, float fallback) {
    return k < p.size() ? Float(p[k].c_str(), fallback) : fallback;
}

double ClampD(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }


uint8_t ColorIndex(const char* s) {
    long long v = 0;
    return ParseInteger(s, v) && v >= 0 && v <= 7 ? static_cast<uint8_t>(v) : 0;
}

uint8_t WeaponIndex(const char* s) {
    long long v = 0;
    return ParseInteger(s, v) && v >= 0 && v <= 0x3A ? static_cast<uint8_t>(v) : 0;
}

uint8_t MoveState(const char* s) {
    long long v = 0;
    return ParseInteger(s, v) && v >= 1 && v <= 7 ? static_cast<uint8_t>(v) : 1;
}

uint16_t ModelIndex(const char* s) {
    long long v = 0;
    return ParseInteger(s, v) && v >= 0 && v <= 311 ? static_cast<uint16_t>(v) : 0;
}


uint16_t VitalValue(const char* s, uint16_t fallback) {
    double v = 0;
    if (!ParseDouble(s, v)) return fallback;
    return static_cast<uint16_t>(ClampD(std::trunc(v), 0, 100));
}


void AssignSpecialName(const std::string& raw, char (&out)[16]) {
    const size_t begin = raw.find_first_not_of(" \t\r\n");
    const size_t end = raw.find_last_not_of(" \t\r\n");
    const std::string trimmed = begin == std::string::npos ? std::string() : raw.substr(begin, end - begin + 1);
    coop_special::NormalizeName(trimmed.c_str(), out, sizeof(out));
}



void FlushClient(Client& c) {
    while (c.fd >= 0 && !c.tx.empty()) {
        const ssize_t n = send(c.fd, c.tx.data(), c.tx.size(), MSG_NOSIGNAL);
        if (n > 0) {
            c.tx.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;

        c.tx.clear();
        shutdown(c.fd, SHUT_RDWR);
        return;
    }
}

void Send(Client& c, const std::string& line) {
    if (c.fd < 0 || line.empty()) return;
    c.tx += line;
    if (c.tx.size() > kMaxPendingBytes) {
        c.tx.clear();
        shutdown(c.fd, SHUT_RDWR);
        return;
    }
    FlushClient(c);
}

void Broadcast(const std::string& line, uint16_t except = 0) {
    for (auto& c : g_clients)
        if (c.joined && c.id != except) Send(c, line);
}




std::string PlayerSnapshot(const Client& p) {
    char b[512];
    std::snprintf(b, sizeof(b),
        "PLAYER|%u|%u|%u|%.3f|%.3f|%.3f|%.3f|%u|%u|%u|%u|%u|%s|%u|%s|%u\n",
        p.id, p.model, p.move, p.x, p.y, p.z, p.heading,
        p.health, p.armour, p.weapon,
        p.vehicleId, p.vehicleSeat,
        SafeName(p.name).c_str(), p.color, p.specialModelName, p.areaCode);
    return b;
}


std::string AnimationPacket(const Client& p) {
    char b[256];
    std::snprintf(b, sizeof(b), "ANIM2|%u|%u|%u|%u|%u|%u|%u|%.4f|%.4f|%.4f|%.4f\n",
        p.id, p.animation.sequence, p.animation.locomotion, p.animation.action,
        p.animation.flags, p.animation.weaponState, p.animation.animGroup,
        p.animation.phase, p.animation.blend, p.animation.aimYaw, p.animation.aimPitch);
    return b;
}


std::string PlayerVitalsPacket(const Client& p) {
    char b[96];
    std::snprintf(b, sizeof(b), "PLAYER_VITALS|%u|%u|%u\n", p.id, p.health, p.armour);
    return b;
}

std::string AssociationsSnapshot(const Client& p) {
    std::string out = "ANIM3|" + std::to_string(p.id) + "|" + std::to_string(p.associations.size());
    char b[128];
    for (const auto& a : p.associations) {
        std::snprintf(b, sizeof(b), "|%u|%d|%u|%.5f|%.5f|%.5f|%.5f|%.5f",
            a.group, a.animationId, a.flags, a.blend, a.blendDelta, a.currentTime, a.speed, a.timeStep);
        out += b;
    }
    out += '\n';
    return out;
}




std::string NpcSnapshot(const Npc& n) {
    char b[512];
    std::snprintf(b, sizeof(b),
        "NPC_NET|%u|%u|%u|%u|%.3f|%.3f|%.3f|%.3f|%u|%u|%u|%u|%u|%u|%s\n",
        n.id, n.ownerId, n.localRef, n.model, n.x, n.y, n.z, n.heading,
        n.health, n.armour, n.weapon, n.move,
        n.vehicleId, n.vehicleSeat, n.specialModelName);
    return b;
}

std::string NpcVitals(const Npc& n) {
    char b[128];
    std::snprintf(b, sizeof(b), "NPC_VITALS|%u|%u|%u|%u|%u\n", n.id, n.ownerId, n.localRef, n.health, n.armour);
    return b;
}

std::string VehicleSnapshot(const Vehicle& v) {
    char b[768];
    std::snprintf(b, sizeof(b),
        "VEHICLE|%u|%u|%u|%u|%.3f|%.3f|%.3f|%.3f|%.4f|%.4f|%.4f|%.1f|%u|%u|%u|%u|%u|%u|%.3f|%u|%u|%u|%u|%u|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f\n",
        v.id, v.model, v.ownerId, v.driverSeat, v.x, v.y, v.z, v.heading, v.vx, v.vy, v.vz, v.health,
        v.primary, v.secondary, v.engine, v.lights, v.siren, v.doorLock, v.dirt, v.tyres,
        v.doors, v.lightStatus, v.panelStatus, v.trailerId,
        v.rightX, v.rightY, v.rightZ, v.upX, v.upY, v.upZ, v.atX, v.atY, v.atZ);
    return b;
}

void RemoveVehicle(uint16_t id, uint16_t except = 0) {
    auto it = g_vehicles.find(id);
    if (it == g_vehicles.end()) return;
    for (auto key = g_vehicleKeys.begin(); key != g_vehicleKeys.end();)
        if (key->second == id) key = g_vehicleKeys.erase(key); else ++key;
    g_vehicles.erase(it);
    char b[64];
    std::snprintf(b, sizeof(b), "VEHICLE_REMOVE|%u\n", id);
    Broadcast(b, except);
}



template <typename Entity>
bool ReconcileVitals(Entity& e) {
    uint16_t health = e.health, armour = e.armour;
    for (const auto& report : e.healthReports) {
        health = std::min(health, report.second.health);
        armour = std::min(armour, report.second.armour);
    }
    const bool changed = health != e.health || armour != e.armour;
    e.health = health;
    e.armour = armour;
    return changed;
}

std::string BonesSnapshot(const Client& p) {
    if (p.bones.empty()) return {};
    std::string out = "BONES2|" + std::to_string(p.id) + "|" + std::to_string(p.bones.size());
    char b[128];
    for (const Bone& bone : p.bones) {
        std::snprintf(b, sizeof(b), "|%u|%.5f|%.5f|%.5f|%.5f", bone.index, bone.qx, bone.qy, bone.qz, bone.qw);
        out += b;
    }
    out += '\n';
    return out;
}


void SendInitialState(Client& c) {
    std::vector<const Client*> players;
    for (const auto& other : g_clients)
        if (other.joined && other.id != c.id) players.push_back(&other);
    std::sort(players.begin(), players.end(), [](const Client* a, const Client* b) { return a->id < b->id; });
    for (const Client* other : players) {
        Send(c, PlayerSnapshot(*other));
        Send(c, AnimationPacket(*other));
        Send(c, AssociationsSnapshot(*other));
        const std::string bones = BonesSnapshot(*other);
        if (!bones.empty()) Send(c, bones);
    }

    std::vector<const Vehicle*> vehicles;
    for (const auto& entry : g_vehicles) vehicles.push_back(&entry.second);
    std::sort(vehicles.begin(), vehicles.end(), [](const Vehicle* a, const Vehicle* b) { return a->id < b->id; });
    for (const Vehicle* v : vehicles) Send(c, VehicleSnapshot(*v));

    for (const auto& [key, m] : g_missionMarkers) { (void)key; Send(c, MissionMarkerSnapshot(m)); }

    std::vector<const Npc*> npcs;
    for (const auto& entry : g_npcs) if (entry.second.ownerId != c.id) npcs.push_back(&entry.second);
    std::sort(npcs.begin(), npcs.end(), [](const Npc* a, const Npc* b) { return a->id < b->id; });
    for (const Npc* n : npcs) Send(c, NpcSnapshot(*n));
}

Client* FindClient(uint16_t id) {
    for (auto& c : g_clients) if (c.joined && c.id == id) return &c;
    return nullptr;
}



uint16_t TimeAuthorityId() {
    uint16_t leader = 0;
    for (const auto& c : g_clients)
        if (c.joined && (!leader || c.id < leader)) leader = c.id;
    return leader;
}

void AnnounceTimeAuthority() {
    const uint16_t leader = TimeAuthorityId();
    if (leader) {
        char b[64];
        std::snprintf(b, sizeof(b), "TIME_OWNER|%u\n", leader);
        Broadcast(b);
    } else {
        g_clockValid = false;
    }
}

void RemoveOwnedState(uint16_t ownerId) {
    for (auto it = g_npcs.begin(); it != g_npcs.end();) {
        if (it->second.ownerId == ownerId) {
            char b[96];
            std::snprintf(b, sizeof(b), "NPC_REMOVE|%u\n", it->second.id);
            Broadcast(b, ownerId);
            it = g_npcs.erase(it);
        } else ++it;
    }
    for (auto it = g_missionMarkers.begin(); it != g_missionMarkers.end();) {
        if (it->second.ownerId == ownerId) {
            char b[64];
            std::snprintf(b, sizeof(b), "MISSION_MARKER_REMOVE|%u\n", it->second.id);
            Broadcast(b, ownerId);
            it = g_missionMarkers.erase(it);
        } else ++it;
    }
    std::vector<uint16_t> ownedVehicles;
    for (const auto& entry : g_vehicles) if (entry.second.ownerId == ownerId) ownedVehicles.push_back(entry.first);
    for (uint16_t id : ownedVehicles) RemoveVehicle(id, ownerId);
    for (auto key = g_vehicleKeys.begin(); key != g_vehicleKeys.end();)
        if (static_cast<uint16_t>(key->first >> 32) == ownerId) key = g_vehicleKeys.erase(key); else ++key;
}

void RemoveClient(size_t index) {
    if (index >= g_clients.size()) return;
    const uint16_t id = g_clients[index].id;
    const bool joined = g_clients[index].joined;
    if (joined) {
        RemoveOwnedState(id);
        for (auto& other : g_clients) other.healthReports.erase(id);
    }
    if (g_clients[index].fd >= 0) close(g_clients[index].fd);
    g_clients.erase(g_clients.begin() + index);
    if (joined) {
        AnnounceTimeAuthority();
        char b[64];
        std::snprintf(b, sizeof(b), "REMOVE|%u\n", id);
        Broadcast(b);
    }
}



void HandleShot(Client& c, const Parts& p) {
    if (p.size() != 7 && p.size() != 9) return;
    const uint8_t weapon = WeaponIndex(p[0].c_str());
    double v[6];
    for (size_t i = 0; i < 6; ++i) if (!ParseDouble(p[i + 1].c_str(), v[i])) return;



    constexpr long long kFireAnimId = 224, kCrouchFireAnimId = 225;
    long long animGroup = 0, animId = 0;
    const bool hasFireAnim = p.size() == 9 && ParseInteger(p[7].c_str(), animGroup) &&
        ParseInteger(p[8].c_str(), animId) && animGroup > 0 && animGroup <= 0xFFFF &&
        (animId == kFireAnimId || animId == kCrouchFireAnimId);
    if (!hasFireAnim) { animGroup = 0; animId = 0; }

    char shot[224];
    int shotLength = std::snprintf(shot, sizeof(shot), "SHOT|%u|%u|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f",
        c.id, weapon, v[0], v[1], v[2], v[3], v[4], v[5]);
    if (animGroup) shotLength += std::snprintf(shot + shotLength, sizeof(shot) - shotLength, "|%lld|%lld", animGroup, animId);
    shot[shotLength] = '\n';
    shot[shotLength + 1] = '\0';
    Broadcast(shot, c.id);

    const float sx = static_cast<float>(v[0]), sy = static_cast<float>(v[1]), sz = static_cast<float>(v[2]);
    const float dx = static_cast<float>(v[3]), dy = static_cast<float>(v[4]), dz = static_cast<float>(v[5]);
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(length) || length < 0.001f) return;
    const float ux = dx / length, uy = dy / length, uz = dz / length;

    Client* hit = nullptr;
    float bestT = 45.0f;
    constexpr float kRadius = 1.35f;
    for (auto& target : g_clients) {
        if (!target.joined || target.id == c.id || target.health == 0) continue;
        const float centerZ = target.z + 0.85f;
        const float rx = target.x - sx, ry = target.y - sy, rz = centerZ - sz;
        const float t = rx * ux + ry * uy + rz * uz;
        if (t < 0 || t > bestT) continue;
        const float qx = rx - t * ux, qy = ry - t * uy, qz = rz - t * uz;
        if (qx * qx + qy * qy + qz * qz <= kRadius * kRadius &&
            std::fabs(centerZ - (sz + t * uz)) <= 1.15f) {
            hit = &target;
            bestT = t;
        }
    }
    if (!hit) return;


    const uint16_t damage = weapon >= 22 ? 18 : 12;
    const uint16_t armourDamage = std::min<uint16_t>(hit->armour, static_cast<uint16_t>((damage * 6 + 9) / 10));
    hit->armour = static_cast<uint16_t>(hit->armour - armourDamage);
    const uint16_t healthDamage = static_cast<uint16_t>(damage - armourDamage);
    hit->health = hit->health > healthDamage ? static_cast<uint16_t>(hit->health - healthDamage) : 0;
    hit->healthReports[hit->id] = {hit->health, hit->armour};

    char d[128];
    std::snprintf(d, sizeof(d), "DAMAGE|%u|%u|%u|%u\n", hit->id, hit->health, hit->armour, weapon);
    Send(*hit, d);
    Broadcast(PlayerVitalsPacket(*hit));
}

void HandleJoin(Client& c, const Parts& p) {
    if (g_clients.size() >= kMaxPlayers) return;
    c.joined = true;
    c.id = g_nextId++;
    if (!c.id) c.id = g_nextId++;
    c.name = p.size() > 0 ? SafeName(p[0]) : "CJ";
    c.color = p.size() > 1 ? ColorIndex(p[1].c_str()) : 0;

    char welcome[64];
    std::snprintf(welcome, sizeof(welcome), "WELCOME|%u\n", c.id);
    Send(c, welcome);
    SendInitialState(c);

    char chat[160];
    std::snprintf(chat, sizeof(chat), "CHAT|SERVER|%s joined the crew\n", c.name.c_str());
    Broadcast(chat);

    char name[96];
    std::snprintf(name, sizeof(name), "NAME|%u|%s|%u\n", c.id, c.name.c_str(), c.color);
    Broadcast(name, c.id);

    AnnounceTimeAuthority();
    if (g_clockValid) {
        char time[64];
        std::snprintf(time, sizeof(time), "TIME|%u|%u|%u\n", g_clockHour, g_clockMinute, g_clockDay);
        Send(c, time);
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "Player joined id=%u name=%s", c.id, c.name.c_str());
}

void HandleTime(Client& c, const Parts& p) {
    if (p.size() != 3) return;
    long long hour = -1, minute = -1, day = -1;
    if (!ParseInteger(p[0].c_str(), hour) || !ParseInteger(p[1].c_str(), minute) || !ParseInteger(p[2].c_str(), day)) return;
    if (c.id != TimeAuthorityId()) return;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || day < 1 || day > 31) return;
    g_clockHour = static_cast<uint8_t>(hour);
    g_clockMinute = static_cast<uint8_t>(minute);
    g_clockDay = static_cast<uint8_t>(day);
    g_clockValid = true;
    char time[64];
    std::snprintf(time, sizeof(time), "TIME|%u|%u|%u\n", g_clockHour, g_clockMinute, g_clockDay);
    Broadcast(time, c.id);
}





void HandleInput(Client& c, const Parts& p) {
    const size_t count = p.size();
    const uint8_t oldWeapon = c.weapon;
    const uint8_t oldColor = c.color;
    const uint16_t oldModel = c.model;
    const std::string oldSpecial(c.specialModelName);

    c.x = static_cast<float>(ClampD(FloatOr(p, 0, c.x), -3000, 3000));
    c.y = static_cast<float>(ClampD(FloatOr(p, 1, c.y), -3000, 3000));
    c.z = static_cast<float>(ClampD(FloatOr(p, 2, c.z), -3000, 3000));
    c.heading = FloatOr(p, 3, c.heading);
    c.model = ModelIndex(At(p, 11));
    c.move = MoveState(At(p, 12));

    c.specialModelName[0] = '\0';
    if (count > 17 && c.model >= coop_special::kFirstModelIndex && c.model <= coop_special::kLastModelIndex)
        AssignSpecialName(p[17], c.specialModelName);


    const unsigned claimedVehicle = UNum(At(p, 18), 0);
    const unsigned claimedSeat = UNum(At(p, 19), 0);
    const unsigned localKey = UNum(At(p, 20), 0);
    const unsigned incomingArea = UNum(At(p, 21), c.areaCode);
    if (count > 21 && incomingArea <= 255) c.areaCode = static_cast<uint8_t>(incomingArea);
    uint16_t resolved = 0;
    if (claimedVehicle > 0 && claimedVehicle <= 65535 && g_vehicles.count(static_cast<uint16_t>(claimedVehicle)))
        resolved = static_cast<uint16_t>(claimedVehicle);
    if (!resolved && localKey > 0) {
        auto it = g_vehicleKeys.find(VehicleKey(c.id, localKey));
        if (it != g_vehicleKeys.end()) resolved = it->second;
    }
    c.vehicleId = resolved;
    c.vehicleSeat = (resolved && claimedSeat <= 8) ? static_cast<uint8_t>(claimedSeat) : 0;



    const uint16_t ownerHealth = count > 14 ? VitalValue(p[14].c_str(), c.health) : c.health;
    const uint16_t ownerArmour = count > 15 ? VitalValue(p[15].c_str(), c.armour) : c.armour;
    const auto previous = c.healthReports.find(c.id);
    const bool ownerRespawned = c.vitalsInitialized && count > 15 &&
        previous != c.healthReports.end() && previous->second.health == 0 && ownerHealth > 0;
    if (!c.vitalsInitialized || ownerRespawned) {
        if (ownerRespawned) {
            c.healthReports.clear();
            c.healthReportResumeAtMs = MonotonicMilliseconds() + kHealthResumeDelayMs;
        }
        c.health = ownerHealth;
        c.armour = ownerArmour;
        c.vitalsInitialized = true;
    }
    c.healthReports[c.id] = {ownerHealth, ownerArmour};
    if (ownerRespawned || ReconcileVitals(c)) Broadcast(PlayerVitalsPacket(c));

    if (count > 13) c.weapon = WeaponIndex(p[13].c_str());
    if (count > 16) c.color = ColorIndex(p[16].c_str());


    if (c.weapon != oldWeapon || c.color != oldColor || c.model != oldModel ||
        std::string(c.specialModelName) != oldSpecial) {
        Broadcast(PlayerSnapshot(c), c.id);
    }
}


void HandleAnim2(Client& c, const Parts& p) {
    if (p.size() != 11) return;
    long long id, sequence, locomotion, action, flags, weaponState, animGroup;
    double phase, blend, aimYaw, aimPitch;
    if (!ParseInteger(p[0].c_str(), id) || !ParseInteger(p[1].c_str(), sequence) ||
        !ParseInteger(p[2].c_str(), locomotion) || !ParseInteger(p[3].c_str(), action) ||
        !ParseInteger(p[4].c_str(), flags) || !ParseInteger(p[5].c_str(), weaponState) ||
        !ParseInteger(p[6].c_str(), animGroup) || !ParseDouble(p[7].c_str(), phase) ||
        !ParseDouble(p[8].c_str(), blend) || !ParseDouble(p[9].c_str(), aimYaw) ||
        !ParseDouble(p[10].c_str(), aimPitch)) return;
    if (id != c.id || sequence < 0 || sequence > 0xFFFFFFFFLL ||
        locomotion < 1 || locomotion > 7 || action < 0 || action > 17 ||
        flags < 0 || flags > 0xFFFF || weaponState < 0 || weaponState > 255 ||
        animGroup < 0 || animGroup > 0xFFFF ||
        phase < 0 || phase > 1 || blend < 0 || blend > 1 ||
        std::fabs(aimYaw) > 10000 || std::fabs(aimPitch) > 10000) return;

    if (static_cast<uint32_t>(sequence) < c.animation.sequence) return;

    c.animation.sequence = static_cast<uint32_t>(sequence);
    c.animation.locomotion = static_cast<uint8_t>(locomotion);
    c.animation.action = static_cast<uint8_t>(action);
    c.animation.flags = static_cast<uint16_t>(flags);
    c.animation.weaponState = static_cast<uint8_t>(weaponState);
    c.animation.animGroup = static_cast<uint16_t>(animGroup);
    c.animation.phase = static_cast<float>(phase);
    c.animation.blend = static_cast<float>(blend);
    c.animation.aimYaw = static_cast<float>(aimYaw);
    c.animation.aimPitch = static_cast<float>(aimPitch);
    c.move = c.animation.locomotion;
    Broadcast(AnimationPacket(c), c.id);
}


void HandleAnim3(Client& c, const Parts& p) {
    if (p.size() < 2) return;
    long long id, count;
    if (!ParseInteger(p[0].c_str(), id) || !ParseInteger(p[1].c_str(), count)) return;
    if (id <= 0 || count < 0 || count > 64 || p.size() != 2 + static_cast<size_t>(count) * 8) return;
    if (id != c.id) return;

    std::vector<AnimationAssociation> out;
    out.reserve(static_cast<size_t>(count));
    for (long long i = 0; i < count; ++i) {
        const size_t o = 2 + static_cast<size_t>(i) * 8;
        long long group, animationId, flags;
        double v[5];
        if (!ParseInteger(p[o].c_str(), group) || group < 0 || group > 65535 ||
            !ParseInteger(p[o + 1].c_str(), animationId) || animationId < -32768 || animationId > 32767 ||
            !ParseInteger(p[o + 2].c_str(), flags) || flags < 0 || flags > 65535) return;
        for (size_t k = 0; k < 5; ++k) if (!ParseDouble(p[o + 3 + k].c_str(), v[k])) return;
        if (std::fabs(v[0]) > 100 || std::fabs(v[1]) > 100 || std::fabs(v[2]) > 100000 ||
            std::fabs(v[3]) > 100 || std::fabs(v[4]) > 100) return;
        AnimationAssociation a{};
        a.group = static_cast<uint16_t>(group);
        a.animationId = static_cast<int16_t>(animationId);
        a.flags = static_cast<uint16_t>(flags);
        a.blend = static_cast<float>(v[0]);
        a.blendDelta = static_cast<float>(v[1]);
        a.currentTime = static_cast<float>(v[2]);
        a.speed = static_cast<float>(v[3]);
        a.timeStep = static_cast<float>(v[4]);
        out.push_back(a);
    }
    c.associations = std::move(out);
    Broadcast(AssociationsSnapshot(c), c.id);
}


void HandleBones2(Client& c, const Parts& p) {
    if (p.size() < 2) return;
    long long id, count;
    if (!ParseInteger(p[0].c_str(), id) || !ParseInteger(p[1].c_str(), count)) return;
    if (id != c.id || count < 0 || count > 19 || p.size() != 2 + static_cast<size_t>(count) * 5) return;
    if (count == 0) {
        c.bones.clear();
        Broadcast("BONES2|" + std::to_string(c.id) + "|0\n", c.id);
        return;
    }
    std::vector<Bone> bones;
    bones.reserve(static_cast<size_t>(count));
    for (long long i = 0; i < count; ++i) {
        const size_t o = 2 + static_cast<size_t>(i) * 5;
        long long index;
        double q[4];
        if (!ParseInteger(p[o].c_str(), index) || index < 0) return;
        for (size_t k = 0; k < 4; ++k) if (!ParseDouble(p[o + 1 + k].c_str(), q[k])) return;
        Bone b{};
        b.index = static_cast<uint8_t>(std::min<long long>(18, index));
        b.qx = static_cast<float>(q[0]);
        b.qy = static_cast<float>(q[1]);
        b.qz = static_cast<float>(q[2]);
        b.qw = static_cast<float>(q[3]);
        bones.push_back(b);
    }
    c.bones = std::move(bones);
    const std::string packet = BonesSnapshot(c);
    if (!packet.empty()) Broadcast(packet, c.id);
}


void HandlePlayerHealth(Client& c, const Parts& p) {
    if (p.size() != 3) return;
    long long targetId, health, armour;
    if (!ParseInteger(p[0].c_str(), targetId) || !ParseInteger(p[1].c_str(), health) ||
        !ParseInteger(p[2].c_str(), armour)) return;
    if (targetId <= 0 || targetId > 65535 || health < 0 || health > 100 || armour < 0 || armour > 100) return;
    Client* target = FindClient(static_cast<uint16_t>(targetId));
    if (!target || !target->vitalsInitialized || target->id == c.id ||
        MonotonicMilliseconds() < target->healthReportResumeAtMs) return;
    target->healthReports[c.id] = {static_cast<uint16_t>(health), static_cast<uint16_t>(armour)};
    if (ReconcileVitals(*target)) Broadcast(PlayerVitalsPacket(*target));
}


void HandleVehicleState(Client& c, const Parts& p) {
    if (p.size() != 32) return;
    long long localKey, model;
    if (!ParseInteger(p[0].c_str(), localKey) || !ParseInteger(p[1].c_str(), model)) return;
    if (localKey <= 0 || localKey > 0xFFFFFFFFLL || model < 400 || model > 611) return;
    double v[30];
    for (size_t i = 0; i < 30; ++i) {
        if (!ParseDouble(p[i + 2].c_str(), v[i]) || std::fabs(v[i]) >= 1000000.0) return;
    }

    const uint32_t key = static_cast<uint32_t>(localKey);
    const uint64_t source = VehicleKey(c.id, key);
    Vehicle* vehicle = nullptr;
    auto mapped = g_vehicleKeys.find(source);
    if (mapped != g_vehicleKeys.end()) {
        const uint16_t existingId = mapped->second;
        auto it = g_vehicles.find(existingId);
        if (it == g_vehicles.end()) {
            g_vehicleKeys.erase(source);
        } else if (it->second.ownerId != c.id) {
            return;
        } else if (it->second.model != model) {
            RemoveVehicle(existingId, c.id);
        } else {
            vehicle = &it->second;
        }
    }
    if (!vehicle) {
        const uint16_t id = AllocateVehicleId();
        if (!id) return;
        Vehicle created{};
        created.id = id;
        created.ownerId = c.id;
        created.localKey = key;
        vehicle = &g_vehicles.emplace(id, created).first->second;
        g_vehicleKeys[source] = id;
        char ack[64];
        std::snprintf(ack, sizeof(ack), "VEHICLE_ID|%u|%u\n", key, id);
        Send(c, ack);
    }

    auto clampF = [](double x, double lo, double hi) { return static_cast<float>(ClampD(x, lo, hi)); };
    auto clampI = [](double x, double lo, double hi) { return std::trunc(ClampD(x, lo, hi)); };
    vehicle->model = static_cast<uint16_t>(model);
    vehicle->x = clampF(v[0], -3000, 3000);
    vehicle->y = clampF(v[1], -3000, 3000);
    vehicle->z = clampF(v[2], -1000, 10000);
    vehicle->heading = static_cast<float>(v[3]);
    vehicle->vx = clampF(v[4], -100, 100);
    vehicle->vy = clampF(v[5], -100, 100);
    vehicle->vz = clampF(v[6], -100, 100);
    vehicle->health = clampF(v[7], 0, 1000);
    vehicle->driverSeat = static_cast<uint16_t>(clampI(v[8], 0, 7));
    vehicle->primary = static_cast<uint8_t>(clampI(v[9], 0, 255));
    vehicle->secondary = static_cast<uint8_t>(clampI(v[10], 0, 255));
    vehicle->engine = v[11] != 0;
    vehicle->lights = v[12] != 0;
    vehicle->siren = v[13] != 0;
    vehicle->doorLock = static_cast<uint8_t>(clampI(v[14], 0, 7));
    vehicle->dirt = clampF(v[15], 0, 15);
    vehicle->tyres = static_cast<uint8_t>(clampI(v[16], 0, 255));

    vehicle->doors = static_cast<uint32_t>(clampI(v[17], 0, 0xFFFFFF));
    vehicle->lightStatus = static_cast<uint32_t>(clampI(v[18], 0, 4294967295.0));
    vehicle->panelStatus = static_cast<uint32_t>(clampI(v[19], 0, 4294967295.0));
    vehicle->trailerId = static_cast<uint16_t>(clampI(v[20], 0, 65535));
    vehicle->rightX = clampF(v[21], -2, 2);
    vehicle->rightY = clampF(v[22], -2, 2);
    vehicle->rightZ = clampF(v[23], -2, 2);
    vehicle->upX = clampF(v[24], -2, 2);
    vehicle->upY = clampF(v[25], -2, 2);
    vehicle->upZ = clampF(v[26], -2, 2);
    vehicle->atX = clampF(v[27], -2, 2);
    vehicle->atY = clampF(v[28], -2, 2);
    vehicle->atZ = clampF(v[29], -2, 2);
    Broadcast(VehicleSnapshot(*vehicle), c.id);
}


void HandleVehicleClaim(Client& c, const Parts& p) {
    if (p.size() != 3) return;
    long long id, seat, key;
    if (!ParseInteger(p[0].c_str(), id) || !ParseInteger(p[1].c_str(), seat) || !ParseInteger(p[2].c_str(), key)) return;
    if (id <= 0 || id > 65535 || seat != 0 || key <= 0 || key > 0xFFFFFFFFLL) return;
    auto it = g_vehicles.find(static_cast<uint16_t>(id));
    if (it == g_vehicles.end()) return;
    const uint64_t newKey = VehicleKey(c.id, static_cast<uint32_t>(key));
    auto alias = g_vehicleKeys.find(newKey);
    if (alias != g_vehicleKeys.end() && alias->second != id) return;



    for (auto npcIt = g_npcs.begin(); npcIt != g_npcs.end();) {
        if (npcIt->second.vehicleId == static_cast<uint16_t>(id) && npcIt->second.vehicleSeat == 0) {
            char removePacket[64];
            std::snprintf(removePacket, sizeof(removePacket), "NPC_REMOVE|%u\n", npcIt->second.id);
            Broadcast(removePacket);
            npcIt = g_npcs.erase(npcIt);
        } else ++npcIt;
    }
    it->second.ownerId = c.id;
    it->second.localKey = static_cast<uint32_t>(key);
    it->second.driverSeat = 0;
    g_vehicleKeys[newKey] = static_cast<uint16_t>(id);
    c.vehicleId = static_cast<uint16_t>(id);
    c.vehicleSeat = 0;
    char ack[64];
    std::snprintf(ack, sizeof(ack), "VEHICLE_ID|%lld|%lld\n", key, id);
    Send(c, ack);
    Broadcast(VehicleSnapshot(it->second), c.id);
}


void HandleVehicleEnter(Client& c, const Parts& p) {
    if (p.size() != 2) return;
    const unsigned id = UNum(p[0].c_str(), 0);
    const unsigned seat = UNum(p[1].c_str(), 255);
    if (id <= 65535 && g_vehicles.count(static_cast<uint16_t>(id)) && seat <= 8) {
        c.vehicleId = static_cast<uint16_t>(id);
        c.vehicleSeat = static_cast<uint8_t>(seat);
    }
}


void HandleVehicleRemoveKey(Client& c, const Parts& p) {
    if (p.size() != 1) return;
    const uint32_t key = UNum(p[0].c_str(), 0);
    if (!key) return;
    auto mapped = g_vehicleKeys.find(VehicleKey(c.id, key));
    if (mapped == g_vehicleKeys.end()) return;
    auto it = g_vehicles.find(mapped->second);
    if (it != g_vehicles.end() && it->second.ownerId == c.id) RemoveVehicle(it->first, c.id);
}


void HandleMissionMarker(Client& c, const Parts& p) {
    if (p.size() != 10) return;
    const unsigned slotValue = UNum(p[0].c_str(), 65536);
    if (slotValue > 174) return;
    MissionMarker m{};
    m.ownerId = c.id;
    m.sourceSlot = static_cast<uint16_t>(slotValue);
    m.id = MissionMarkerId(c.id, m.sourceSlot);
    m.type = static_cast<uint8_t>(std::min(8u, UNum(p[1].c_str(), 0)));
    m.display = static_cast<uint8_t>(UNum(p[2].c_str(), 0));
    m.sprite = static_cast<uint8_t>(std::min(63u, UNum(p[3].c_str(), 0)));
    m.scale = static_cast<uint16_t>(std::min(65535u, UNum(p[4].c_str(), 1)));
    m.color = UNum(p[5].c_str(), 0);
    m.x = Float(p[6].c_str(), 0);
    m.y = Float(p[7].c_str(), 0);
    m.z = Float(p[8].c_str(), 0);
    m.radius = Float(p[9].c_str(), 1);
    if (m.display != 1 && m.display != 3) return;
    if (!std::isfinite(m.x) || !std::isfinite(m.y) || !std::isfinite(m.z) || !std::isfinite(m.radius)) return;
    m.radius = std::max(0.1f, std::min(100.0f, m.radius));
    g_missionMarkers[MissionMarkerKey(c.id, m.sourceSlot)] = m;
    Broadcast(MissionMarkerSnapshot(m), c.id);
}

void HandleMissionMarkerRemove(Client& c, const Parts& p) {
    if (p.size() != 1) return;
    const unsigned slotValue = UNum(p[0].c_str(), 65536);
    if (slotValue > 174) return;
    auto it = g_missionMarkers.find(MissionMarkerKey(c.id, static_cast<uint16_t>(slotValue)));
    if (it == g_missionMarkers.end()) return;
    char b[64];
    std::snprintf(b, sizeof(b), "MISSION_MARKER_REMOVE|%u\n", it->second.id);
    Broadcast(b, c.id);
    g_missionMarkers.erase(it);
}




void HandleNpcState(Client& c, const Parts& p) {
    const size_t count = p.size();
    if (count < 10 || count > 14) return;

    long long ref, model, weaponId, moveState;
    if (!ParseInteger(p[0].c_str(), ref) || ref <= 0 || ref > 0xFFFFFFFFLL) return;
    if (!ParseInteger(p[1].c_str(), model) || model < 0 || model > 2000) return;
    double v[8];
    for (size_t i = 0; i < 8; ++i) if (!ParseDouble(p[i + 2].c_str(), v[i])) return;
    if (!ParseInteger(p[8].c_str(), weaponId) || weaponId < 0 || weaponId > 0x3A) return;
    if (!ParseInteger(p[9].c_str(), moveState) || moveState < 1 || moveState > 7) return;

    const uint16_t incomingHealth = static_cast<uint16_t>(ClampD(std::trunc(v[4]), 0, 100));
    const uint16_t incomingArmour = static_cast<uint16_t>(ClampD(std::trunc(v[5]), 0, 100));


    long long vehicleKey = 0, seat = 0, directId = 0;
    if (count >= 13) {
        if (!ParseInteger(p[10].c_str(), vehicleKey)) vehicleKey = 0;
        if (!ParseInteger(p[11].c_str(), seat)) seat = 0;
    }
    if (count >= 14) {
        if (!ParseInteger(p[13].c_str(), directId)) directId = 0;
    }
    uint16_t npcVehicleId = 0;
    if (directId > 0 && directId <= 65535 && g_vehicles.count(static_cast<uint16_t>(directId))) {
        npcVehicleId = static_cast<uint16_t>(directId);
    } else if (vehicleKey > 0) {
        auto it = g_vehicleKeys.find(VehicleKey(c.id, static_cast<uint32_t>(vehicleKey)));
        if (it != g_vehicleKeys.end()) npcVehicleId = it->second;
    }
    const uint8_t npcVehicleSeat = static_cast<uint8_t>(ClampD(seat, 0, 8));



    if (npcVehicleId && npcVehicleSeat == 0) {
        auto vehicleIt = g_vehicles.find(npcVehicleId);
        if (vehicleIt == g_vehicles.end() || vehicleIt->second.ownerId != c.id) {
            const uint64_t staleKey = NpcKey(c.id, static_cast<uint32_t>(ref));
            auto stale = g_npcs.find(staleKey);
            if (stale != g_npcs.end()) {
                char removePacket[64];
                std::snprintf(removePacket, sizeof(removePacket), "NPC_REMOVE|%u\n", stale->second.id);
                Broadcast(removePacket, c.id);
                g_npcs.erase(stale);
            }
            return;
        }
    }


    const bool specialSlot = model >= coop_special::kFirstModelIndex && model <= coop_special::kLastModelIndex;
    std::string specialRaw;
    if (count == 11) specialRaw = p[10];
    else if (count == 13 || count == 14) specialRaw = p[12];

    const uint64_t key = NpcKey(c.id, static_cast<uint32_t>(ref));
    auto it = g_npcs.find(key);
    if (it == g_npcs.end()) {
        Npc created{};
        created.id = g_nextNpcId++;
        if (!created.id || created.id > kMaxNpcId) return;
        created.ownerId = c.id;
        created.localRef = static_cast<uint32_t>(ref);
        created.health = incomingHealth;
        created.armour = incomingArmour;
        it = g_npcs.emplace(key, created).first;
    }
    Npc& npc = it->second;
    npc.model = static_cast<uint16_t>(model);
    npc.specialModelName[0] = '\0';
    if (specialSlot) AssignSpecialName(specialRaw, npc.specialModelName);
    npc.x = static_cast<float>(v[0]);
    npc.y = static_cast<float>(v[1]);
    npc.z = static_cast<float>(v[2]);
    npc.heading = static_cast<float>(v[3]);
    npc.weapon = static_cast<uint8_t>(weaponId);
    npc.move = static_cast<uint8_t>(moveState);
    npc.vehicleId = npcVehicleId;
    npc.vehicleSeat = npcVehicleSeat;
    npc.healthReports[c.id] = {incomingHealth, incomingArmour};
    const bool vitalsChanged = ReconcileVitals(npc);

    Broadcast(NpcSnapshot(npc), c.id);
    if (vitalsChanged) Broadcast(NpcVitals(npc));
}


void HandleNpcHealth(Client& c, const Parts& p) {
    if (p.size() != 3) return;
    long long id, health, armour;
    if (!ParseInteger(p[0].c_str(), id) || !ParseInteger(p[1].c_str(), health) || !ParseInteger(p[2].c_str(), armour)) return;
    if (id <= 0 || id > 0xFFFFFFFFLL || health < 0 || health > 100 || armour < 0 || armour > 100) return;
    for (auto& entry : g_npcs) {
        Npc& n = entry.second;
        if (n.id != static_cast<uint32_t>(id)) continue;
        if (n.ownerId == c.id) return;
        n.healthReports[c.id] = {static_cast<uint16_t>(health), static_cast<uint16_t>(armour)};
        if (ReconcileVitals(n)) {
            Broadcast(NpcSnapshot(n), n.ownerId);
            Broadcast(NpcVitals(n));
        }
        return;
    }
}

void HandleNpcRemove(Client& c, const Parts& p) {
    if (p.size() < 1) return;
    long long ref;
    if (!ParseInteger(p[0].c_str(), ref) || ref <= 0) return;
    auto it = g_npcs.find(NpcKey(c.id, static_cast<uint32_t>(ref)));
    if (it == g_npcs.end()) return;
    char b[96];
    std::snprintf(b, sizeof(b), "NPC_REMOVE|%u\n", it->second.id);
    Broadcast(b, c.id);
    g_npcs.erase(it);
}

std::vector<std::string> SplitLine(const std::string& line) {
    std::vector<std::string> out;
    size_t begin = 0;
    while (begin <= line.size()) {
        size_t end = line.find('|', begin);
        if (end == std::string::npos) end = line.size();
        out.emplace_back(line.substr(begin, end - begin));
        if (end == line.size()) break;
        begin = end + 1;
    }
    return out;
}

void HandleChat(Client& c, const Parts& p) {
    std::string message;
    for (size_t i = 0; i < p.size(); ++i) {
        if (i) message.push_back(' ');
        message += p[i];
    }
    for (char& ch : message) {
        if (ch == '|' || ch == '\r' || ch == '\n') ch = ' ';
    }
    const size_t first = message.find_first_not_of(" \t");
    if (first == std::string::npos) return;
    const size_t last = message.find_last_not_of(" \t");
    message = message.substr(first, last - first + 1);
    if (message.size() > 160) message.resize(160);
    if (message.empty()) return;
    Broadcast("CHAT|" + SafeName(c.name) + "|" + message + "\n");
}

void HandleLine(Client& c, const std::string& raw) {

    const size_t first = raw.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return;
    const size_t last = raw.find_last_not_of(" \t\r\n");
    const std::string line = raw.substr(first, last - first + 1);

    const Parts f = SplitLine(line);
    if (f.empty()) return;
    const std::string& type = f[0];
    const Parts p(f.begin() + 1, f.end());

    if (type == "JOIN") {
        if (!c.joined) HandleJoin(c, p);
        return;
    }
    if (!c.joined) return;

    if (type == "CHATMSG") HandleChat(c, p);
    else if (type == "TIME") HandleTime(c, p);
    else if (type == "INPUT") HandleInput(c, p);
    else if (type == "ANIM2") HandleAnim2(c, p);
    else if (type == "ANIM3") HandleAnim3(c, p);
    else if (type == "BONES2") HandleBones2(c, p);
    else if (type == "PLAYER_HEALTH") HandlePlayerHealth(c, p);
    else if (type == "VEHICLE_STATE") HandleVehicleState(c, p);
    else if (type == "VEHICLE_CLAIM") HandleVehicleClaim(c, p);
    else if (type == "VEHICLE_ENTER") HandleVehicleEnter(c, p);
    else if (type == "VEHICLE_REMOVE_KEY") HandleVehicleRemoveKey(c, p);
    else if (type == "MISSION_MARKER") HandleMissionMarker(c, p);
    else if (type == "MISSION_MARKER_REMOVE") HandleMissionMarkerRemove(c, p);
    else if (type == "NPC_STATE") HandleNpcState(c, p);
    else if (type == "NPC_HEALTH") HandleNpcHealth(c, p);
    else if (type == "NPC_REMOVE") HandleNpcRemove(c, p);
    else if (type == "SHOT") HandleShot(c, p);
}
}

EmbeddedServer::EmbeddedServer() = default;
EmbeddedServer::~EmbeddedServer() { Stop(); }

bool EmbeddedServer::Start(uint16_t port) {
    if (m_running.load()) return m_port == port;
    errno = 0;
    m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenSocket < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "socket() failed: %s", std::strerror(errno));
        return false;
    }
    int yes = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || listen(m_listenSocket, 16) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "bind/listen failed on %u: %s", port, std::strerror(errno));
        close(m_listenSocket);
        m_listenSocket = -1;
        return false;
    }
    m_port = port;
    m_running = true;
    m_thread = std::thread(&EmbeddedServer::Run, this);
    __android_log_print(ANDROID_LOG_INFO, kTag, "Native server started on port %u", port);
    return true;
}

void EmbeddedServer::Stop() {
    if (!m_running.exchange(false)) return;
    if (m_listenSocket >= 0) {
        shutdown(m_listenSocket, SHUT_RDWR);
        close(m_listenSocket);
        m_listenSocket = -1;
    }
    if (m_thread.joinable()) m_thread.join();
    CloseClients();
}

void EmbeddedServer::CloseClients() {
    std::scoped_lock lock(g_serverMutex);
    for (auto& c : g_clients) if (c.fd >= 0) close(c.fd);
    g_clients.clear();
    g_npcs.clear();
    g_missionMarkers.clear();
    g_vehicles.clear();
    g_vehicleKeys.clear();
    g_nextVehicleId = 1;
    g_clockValid = false;
    g_clockHour = 0;
    g_clockMinute = 0;
    g_clockDay = 1;
}

void EmbeddedServer::Run() {
    uint64_t lastPlayerBroadcast = 0;
    uint64_t lastVehicleBroadcast = 0;
    while (m_running.load()) {
        std::vector<pollfd> pfds;
        pfds.push_back({m_listenSocket, POLLIN, 0});



        std::vector<int> polledFds;
        {
            std::scoped_lock lock(g_serverMutex);
            polledFds.reserve(g_clients.size());
            for (const auto& c : g_clients) {
                pfds.push_back({c.fd, static_cast<short>(POLLIN | (c.tx.empty() ? 0 : POLLOUT)), 0});
                polledFds.push_back(c.fd);
            }
        }
        poll(pfds.data(), pfds.size(), 15);
        std::scoped_lock lock(g_serverMutex);



        for (size_t pi = 0; pi < polledFds.size(); ++pi) {
            const size_t pollIndex = pi + 1;
            const int polledFd = polledFds[pi];
            const short events = pfds[pollIndex].revents;
            if (!(events & (POLLERR | POLLHUP | POLLNVAL | POLLIN | POLLOUT))) continue;
            auto findIt = std::find_if(g_clients.begin(), g_clients.end(),
                [polledFd](const Client& cl) { return cl.fd == polledFd; });
            if (findIt == g_clients.end()) continue;
            const size_t idx = static_cast<size_t>(std::distance(g_clients.begin(), findIt));
            if (events & (POLLERR | POLLHUP | POLLNVAL)) { RemoveClient(idx); continue; }
            if (events & POLLOUT) FlushClient(g_clients[idx]);
            if (!(events & POLLIN)) continue;
            char b[4096];
            const ssize_t n = recv(g_clients[idx].fd, b, sizeof(b), 0);
            if (n <= 0) { RemoveClient(idx); continue; }
            g_clients[idx].rx.append(b, static_cast<size_t>(n));
            if (g_clients[idx].rx.size() > 65536) { RemoveClient(idx); continue; }
            size_t e;
            while ((e = g_clients[idx].rx.find('\n')) != std::string::npos) {
                const std::string line = g_clients[idx].rx.substr(0, e);
                g_clients[idx].rx.erase(0, e + 1);
                if (line.size() < kMaxLine) HandleLine(g_clients[idx], line);
            }
        }
        if (!pfds.empty() && (pfds[0].revents & POLLIN)) {
            int fd = accept(m_listenSocket, nullptr, nullptr);
            if (fd >= 0) {
                fcntl(fd, F_SETFL, O_NONBLOCK);
                g_clients.push_back(Client{fd});
            }
        }

        const uint64_t now = MonotonicMilliseconds();
        if (now - lastPlayerBroadcast >= kPlayerBroadcastMs) {
            lastPlayerBroadcast = now;
            for (const auto& p : g_clients) if (p.joined) Broadcast(PlayerSnapshot(p), p.id);
        }
        if (now - lastVehicleBroadcast >= kVehicleBroadcastMs) {
            lastVehicleBroadcast = now;
            for (const auto& entry : g_vehicles) Broadcast(VehicleSnapshot(entry.second), entry.second.ownerId);
        }
    }
}
