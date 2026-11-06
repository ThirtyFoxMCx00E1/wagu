// MP 4.12: MP side of the open.mp cross-play component (see bridge_api.h).
#include "bridge_api.h"
#include "net/server.h"
#include "net/online.h"
#include <cstring>
#include <strings.h>

static_assert(sizeof(xp::Sync) == sizeof(SyncData), "xp::Sync must match SyncData");
static_assert(xp::F_IN_CAR == SYNC_IN_CAR && xp::F_DEAD == SYNC_DEAD && xp::F_DRIVER == SYNC_DRIVER, "sync flags");

namespace xp
{
static MPServer* s_Srv = nullptr;
static bool s_Online = false;

const char* Version() { return MP_VERSION_STR; }
static int RegionOf(const std::string& r) { for(int i = 0; i < REGION_COUNT; ++i) if(!strcasecmp(r.c_str(), g_MPRegionNames[i])) return i; return REGION_ASIA; }
const char* RegionName(const std::string& r) { return Online::RegionLabel(RegionOf(r)); }

bool Start(const Config& c, LogFn log)
{
    if(s_Srv) return true;
    s_Srv = new MPServer();
    s_Srv->BridgeEnable(true);
    s_Srv->SetPersistent(true);
    int region = RegionOf(c.region);
    s_Online = c.online;
    if(s_Online) { Online::SetRegionPrefs(region, false); Online::Start("mp_broker.txt"); Online::SetHostDedicated(true); }
    if(!s_Srv->Start((uint16_t)c.port, c.name.c_str(), log, c.maxPlayers, region)) { delete s_Srv; s_Srv = nullptr; return false; }
    return true;
}

void Stop()
{
    if(!s_Srv) return;
    if(s_Online) Online::SetHosted(false, "", 0, 0, 0, 0);
    s_Srv->Stop(); delete s_Srv; s_Srv = nullptr;
}

void Tick()
{
    if(s_Srv && s_Online)
    {
        Online::SetRegionPrefs(s_Srv->GetRegion(), false);
        Online::SetHosted(true, s_Srv->GetName(), s_Srv->GetPort(), s_Srv->GetPlayerCount(), s_Srv->GetMaxPlayers(), s_Srv->GetRegion());
    }
}

int  Add(const char* name, uint32_t rgba, uint16_t skin) { return s_Srv ? s_Srv->BridgeAdd(name, rgba, skin) : -1; }
void SyncPc(int slot, const Sync& d) { if(s_Srv) { SyncData sd; memcpy(&sd, &d, sizeof(sd)); s_Srv->BridgeSync(slot, sd); } }
void Chat(int slot, const char* text) { if(s_Srv) s_Srv->BridgeChat(slot, text); }
void Remove(int slot) { if(s_Srv) s_Srv->BridgeRemove(slot); }

void Peers(std::vector<Peer>& out)
{
    out.clear(); if(!s_Srv) return;
    std::vector<MPServer::BridgePeer> v; s_Srv->BridgePeers(v);
    for(auto& b : v) { Peer p; p.slot = b.slot; p.name = b.name; p.skin = b.skin; p.hasSync = b.hasSync; memcpy(&p.d, &b.d, sizeof(p.d)); out.push_back(p); }
}

void TakeChat(std::vector<std::pair<std::string, std::string>>& out)
{
    out.clear(); if(!s_Srv) return;
    std::vector<std::pair<int, std::string>> raw; s_Srv->BridgeTakeChat(raw);
    if(raw.empty()) return;
    std::vector<Peer> peers; Peers(peers);
    for(auto& r : raw)
    {
        std::string who = "Mobile";
        for(auto& p : peers) if(p.slot == r.first) who = p.name;
        out.emplace_back(who, r.second);
    }
}
}
