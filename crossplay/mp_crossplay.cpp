// MP 4.12: open.mp cross-play - PC (SA-MP / open.mp client) + Mobile (MP for GTA SA Android) in one world.
//
// An open.mp server component (components/mp-crossplay.so / .dll). It runs the MP server of the
// mobile mod INSIDE omp-server, on its own UDP port (default 7780, MP protocol), and bridges both:
//   phone player (MP)        -> an open.mp NPC (NPCs component) that copies the phone's position,
//                               facing, health, armour, skin and weapon ~10 times a second
//   PC player (open.mp)      -> a "(PC)" player on the MP side (ghost slot), synced the same way
//   chat                     -> both ways ("[Mobile] name: text" on PC, "name (PC): text" on mobile)
// The gamemode sees the phone players as NPCs (IsPlayerNPC = 1). PC players join with the normal
// open.mp launcher; phone players join the "<name> [cross-play]" server from the MP lobby / LAN list
// or by IP:7780.
//
// Built against open.mp-sdk 08cc69a3 = the SDK of the open.mp 1.6.7.3218 server.
// Config: mp_crossplay.cfg next to omp-server (port=7780, name=..., region=Asia, online=1, max=100).
#include <sdk.hpp>
#include <Server/Components/NPCs/npcs.hpp>
#include <Server/Components/Vehicles/vehicles.hpp>

#include "bridge_api.h"   // the MP server (bridge_api.cpp) - no MP headers here

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace
{
typedef xp::Config Cfg;

static std::string Trim(std::string s)
{
    while(!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0; while(i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

static Cfg LoadCfg()
{
    Cfg c;
    FILE* f = fopen("mp_crossplay.cfg", "r");
    if(!f)
    {   // first run: write the defaults so the owner can edit them
        f = fopen("mp_crossplay.cfg", "w");
        if(f)
        {
            fprintf(f, "# MP cross-play (PC + Mobile) - restart omp-server after a change\n"
                       "# UDP port of the mobile side (open it in the firewall, like 7777)\nport=7780\n"
                       "# name in the MP lobby on the phones\nname=open.mp [cross-play]\n"
                       "# Asia | Europe | NA | Oceania (the MP region lobby it is listed in)\nregion=Asia\n"
                       "# 0 = LAN / direct IP only (not in the internet lobby)\nonline=1\n"
                       "# mobile players max (they each take an open.mp player slot as an NPC)\nmax=100\n");
            fclose(f);
        }
        return c;
    }
    char line[256];
    while(fgets(line, sizeof(line), f))
    {
        std::string l = Trim(line);
        if(l.empty() || l[0] == '#') continue;
        size_t eq = l.find('='); if(eq == std::string::npos) continue;
        std::string k = Trim(l.substr(0, eq)), v = Trim(l.substr(eq + 1));
        if(k == "port") c.port = atoi(v.c_str());
        else if(k == "name" && !v.empty()) c.name = v;
        else if(k == "online") c.online = v != "0";
        else if(k == "max") c.maxPlayers = atoi(v.c_str());
        else if(k == "region" && !v.empty()) c.region = v;
    }
    fclose(f);
    if(c.port <= 0 || c.port > 65535) c.port = 7780;
    return c;
}

static ICore* g_Core = nullptr;
static void ServerLog(const char* msg) { if(g_Core) g_Core->printLn("[mp-crossplay] %s", msg); }

static uint8_t Clamp255(float v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
static float FacingOf(const GTAQuat& q) { float z = q.ToEuler().z; while(z < 0) z += 360.f; while(z >= 360.f) z -= 360.f; return z; }
}

class MPCrossplay final : public IComponent, public CoreEventHandler, public PlayerConnectEventHandler, public PlayerTextEventHandler
{
    PROVIDE_UID(0x4D50435250534131);   // "MPCRPSA1"

    ICore* core = nullptr;
    INPCComponent* npcs = nullptr;
    Cfg cfg;
    bool running = false;
    std::map<int, int> mobileNpc;   // MP slot -> open.mp NPC pool id
    std::map<int, int> pcGhost;     // open.mp player id -> MP ghost slot
    std::chrono::steady_clock::time_point lastSync{}, lastList{};
    std::map<int, std::chrono::steady_clock::time_point> npcRetry;   // MP slot -> next try after a failed create
    bool warnedFull = false;

public:
    MPCrossplay() : cfg(LoadCfg()) { }   // before provideConfiguration (max_bots)

private:
    StringView componentName() const override { return "MP cross-play (PC + Mobile)"; }
    SemanticVersion componentVersion() const override { return SemanticVersion(4, 12, 0, 0); }

    void onLoad(ICore* c) override
    {
        core = c; g_Core = c;
        core->getEventDispatcher().addEventHandler(this);
        core->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
        core->getPlayers().getPlayerTextDispatcher().addEventHandler(this);
    }

    // the phone players are NPCs: open.mp makes none unless "max_bots" in config.json allows them
    void provideConfiguration(ILogger& logger, IEarlyConfig& config, bool defaults) override
    {
        if(defaults) return;
        int* mb = config.getInt("max_bots");
        int want = cfg.maxPlayers > 0 ? cfg.maxPlayers : 100;
        const int was = mb ? *mb : 0;
        if(was < want)
        {
            config.setInt("max_bots", want);
            logger.printLn("[mp-crossplay] max_bots %d -> %d (one NPC per Android player)", was, want);
        }
    }

    void onInit(IComponentList* components) override
    {
        npcs = components->queryComponent<INPCComponent>();
        if(!npcs) core->printLn("[mp-crossplay] the NPCs component is missing (components/NPCs): phone players stay invisible on PC");
    }

    void onReady() override
    {
        if(!xp::Start(cfg, ServerLog))
        {
            core->printLn("[mp-crossplay] could not open UDP port %d (in use?) - cross-play is OFF", cfg.port);
            return;
        }
        running = true;
        core->printLn("[mp-crossplay] MP %s mobile side on UDP %d (\"%s\", %s%s) - PC + Mobile cross-play is ON",
            xp::Version(), cfg.port, cfg.name.c_str(), xp::RegionName(cfg.region), cfg.online ? ", internet lobby" : ", LAN / IP only");
    }

    // ---- PC -> Mobile -----------------------------------------------------------------------
    void onPlayerConnect(IPlayer& p) override
    {
        if(!running || p.isBot()) return;
        std::string nm(p.getName().data(), p.getName().size());
        Colour c = p.getColour();
        int slot = xp::Add(nm.c_str(), c.RGBA(), (uint16_t)p.getSkin());
        if(slot >= 0) pcGhost[p.getID()] = slot;
        p.sendClientMessage(Colour(0x2B, 0xD9, 0x4F), "This server has MP cross-play: Android players (MP mod) play here with you.");
    }

    void onPlayerDisconnect(IPlayer& p, PeerDisconnectReason) override
    {
        auto it = pcGhost.find(p.getID());
        if(it != pcGhost.end()) { xp::Remove(it->second); pcGhost.erase(it); }
    }

    bool onPlayerText(IPlayer& p, StringView msg) override
    {
        auto it = pcGhost.find(p.getID());
        if(it != pcGhost.end()) xp::Chat(it->second, std::string(msg.data(), msg.size()).c_str());
        return true;
    }

    void SyncPcPlayers()
    {
        for(IPlayer* p : core->getPlayers().players())
        {
            if(p->isBot()) continue;
            auto it = pcGhost.find(p->getID());
            if(it == pcGhost.end()) continue;
            xp::Sync d; memset(&d, 0, sizeof(d));
            Vector3 pos = p->getPosition();
            d.pos[0] = pos.x; d.pos[1] = pos.y; d.pos[2] = pos.z;
            d.heading = FacingOf(p->getRotation());
            Vector3 v = p->getVelocity();
            d.speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) * 50.f;   // SA-MP velocity units -> m/s (approx.)
            d.health = Clamp255(p->getHealth());
            d.armour = Clamp255(p->getArmour());
            d.weapon = (uint8_t)p->getArmedWeapon();
            d.skin = (uint16_t)p->getSkin();
            const PlayerState st = p->getState();
            if(st == PlayerState_Wasted) d.flags |= xp::F_DEAD;
            if(st == PlayerState_Driver || st == PlayerState_Passenger)
            {
                IPlayerVehicleData* vd = queryExtension<IPlayerVehicleData>(p);
                IVehicle* veh = vd ? vd->getVehicle() : nullptr;
                if(veh)
                {
                    d.flags |= xp::F_IN_CAR; if(st == PlayerState_Driver) d.flags |= xp::F_DRIVER;
                    d.carModel = (uint16_t)veh->getModel();
                    Vector3 cp = veh->getPosition();
                    d.carPos[0] = cp.x; d.carPos[1] = cp.y; d.carPos[2] = cp.z;
                    d.carHeading = veh->getZAngle();
                }
            }
            const PlayerAimData& aim = p->getAimData();
            d.aim[0] = pos.x + aim.camFrontVector.x * 20.f; d.aim[1] = pos.y + aim.camFrontVector.y * 20.f; d.aim[2] = pos.z + aim.camFrontVector.z * 20.f;
            xp::SyncPc(it->second, d);
        }
    }

    // ---- Mobile -> PC -----------------------------------------------------------------------
    void SyncMobilePlayers()
    {
        std::vector<xp::Peer> peers;
        xp::Peers(peers);
        std::map<int, bool> seen;
        for(auto& b : peers)
        {
            seen[b.slot] = true;
            INPC* npc = nullptr;
            auto it = mobileNpc.find(b.slot);
            if(it != mobileNpc.end()) npc = npcs ? npcs->get(it->second) : nullptr;
            if(!npc && npcs)
            {
                auto rt = npcRetry.find(b.slot);
                if(rt != npcRetry.end() && std::chrono::steady_clock::now() < rt->second) continue;
                char nm[32];
                int o = 0;   // SA-MP names: A-Z a-z 0-9 [ ] ( ) $ @ . _ = , 3..24 chars
                for(size_t i = 0; i < b.name.size() && o < 20; ++i)
                {
                    char ch = b.name[i];
                    bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || strchr("[]()$@._=", ch);
                    nm[o++] = ok ? ch : '_';
                }
                nm[o] = 0;
                char full[32]; snprintf(full, sizeof(full), "M_%s", o >= 1 ? nm : "Mobile");
                npc = npcs->create(full);
                if(!npc) { snprintf(full, sizeof(full), "M_%s_%d", o >= 1 ? nm : "Mobile", b.slot); npc = npcs->create(full); }
                if(!npc)
                {
                    npcRetry[b.slot] = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    if(!warnedFull) { warnedFull = true; core->printLn("[mp-crossplay] no free NPC slot for %s: raise \"max_players\" / \"max_bots\" in config.json", b.name.c_str()); }
                    continue;
                }
                npcRetry.erase(b.slot);
                mobileNpc[b.slot] = npc->getID();
                npc->spawn();
                core->printLn("[mp-crossplay] %s joined from Android (MP) as NPC %s", b.name.c_str(), full);
            }
            if(!npc || !b.hasSync) continue;
            const xp::Sync& d = b.d;
            bool inCar = (d.flags & xp::F_IN_CAR) != 0;
            Vector3 pos = inCar ? Vector3(d.carPos[0], d.carPos[1], d.carPos[2]) : Vector3(d.pos[0], d.pos[1], d.pos[2]);
            npc->setPosition(pos, false);
            npc->setRotation(GTAQuat(0.f, 0.f, inCar ? d.carHeading : d.heading), false);
            if(npc->getPlayer() && npc->getPlayer()->getSkin() != d.skin) npc->setSkin(d.skin);
            npc->setHealth((d.flags & xp::F_DEAD) ? 0.f : (float)d.health);
            npc->setArmour((float)d.armour);
            npc->setWeapon(d.weapon);
        }
        for(auto it = mobileNpc.begin(); it != mobileNpc.end();)
        {
            if(!seen.count(it->first))
            {
                if(npcs) if(INPC* npc = npcs->get(it->second)) npcs->destroy(*npc);
                npcRetry.erase(it->first);
                it = mobileNpc.erase(it);
            }
            else ++it;
        }
    }

    void onTick(Microseconds, TimePoint) override
    {
        if(!running) return;
        auto now = std::chrono::steady_clock::now();
        if(now - lastSync >= std::chrono::milliseconds(80))
        {
            lastSync = now;
            SyncPcPlayers();
            SyncMobilePlayers();
            std::vector<std::pair<std::string, std::string>> chat;
            xp::TakeChat(chat);
            for(auto& c : chat)
            {
                char line[200]; snprintf(line, sizeof(line), "[Mobile] %s: %s", c.first.c_str(), c.second.c_str());
                core->getPlayers().sendClientMessageToAll(Colour(0x7F, 0xD8, 0xFF), line);
            }
        }
        if(cfg.online && now - lastList >= std::chrono::seconds(2))
        {
            lastList = now;
            xp::Tick();
        }
    }

    void onFree(IComponent* c) override { if(c == npcs) npcs = nullptr; }

    void free() override
    {
        if(running) { xp::Stop(); running = false; }
        if(core)
        {
            core->getEventDispatcher().removeEventHandler(this);
            core->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
            core->getPlayers().getPlayerTextDispatcher().removeEventHandler(this);
        }
        delete this;
    }

    void reset() override
    {   // GMX: the gamemode restarts, the NPCs are gone - the next tick makes them again
        mobileNpc.clear();
    }
};

COMPONENT_ENTRY_POINT()
{
    return new MPCrossplay();
}
