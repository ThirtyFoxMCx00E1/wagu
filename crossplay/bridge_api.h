// MP 4.12: the cross-play component talks to the MP server only through this header, so the
// open.mp SDK and the MP headers never meet in one file (both define TEAM_NONE, Vector3...).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <utility>

namespace xp
{
// = SyncData of src/net/protocol.h (checked by static_assert in bridge_api.cpp)
struct Sync
{
    float    pos[3];
    float    heading;      // degrees
    float    aim[3];
    float    speed;        // m/s
    uint8_t  health, armour, weapon, flags;
    uint16_t skin, carModel;
    float    carPos[3];
    float    carHeading;
};
enum : uint8_t { F_IN_CAR = 1 << 1, F_DEAD = 1 << 3, F_DRIVER = 1 << 4 };
struct Peer { int slot; std::string name; uint16_t skin; bool hasSync; Sync d; };

struct Config { int port = 7780; std::string name = "open.mp [cross-play]"; std::string region = "Asia"; bool online = true; int maxPlayers = 100; };

typedef void (*LogFn)(const char* msg);
bool  Start(const Config& c, LogFn log);     // false = the UDP port could not be opened
void  Stop();
void  Tick();                                // ~2 s: the internet lobby entry
const char* Version();
const char* RegionName(const std::string& r);

int   Add(const char* name, uint32_t rgba, uint16_t skin);
void  SyncPc(int slot, const Sync& d);
void  Chat(int slot, const char* text);
void  Remove(int slot);
void  Peers(std::vector<Peer>& out);
void  TakeChat(std::vector<std::pair<std::string, std::string>>& out);   // (name, text)
}
