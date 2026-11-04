// Saved server list (max MP_MAX_SERVERS = 100) with live info (players x/max, ping) via PKT_QUERY.
#pragma once
#include <stdint.h>
#include <netinet/in.h>
#include "protocol.h"

struct ServerEntry
{
    char     name[32];
    char     host[64];
    int      port;
    int      region;          // eRegion
    char     password[MP_PASSWORD_LEN + 1];   // 2.19: join password (empty = none)
    char     lang[MP_LANG_LEN];               // 2.25: language set when the server was created (saved)
    // 4.03: CS:GO style list (saved, set in + Add server): country, gamemode chip, tags, description, favourite
    char     cc[MP_CC_LEN];
    int      modeCol;
    char     mode[MP_MODE_LEN];
    char     tags[MP_TAGS_LEN];
    char     desc[MP_DESC_LEN];
    bool     fav;

    // live info (not saved)
    bool        resolved;
    sockaddr_in addr;
    bool        online;
    bool        locked;       // 2.19: server says it needs a password
    int         icon;         // 2.24: 0 = default icon, 1..250
    char        liveLang[MP_LANG_LEN];   // 2.25: language the running server reports
    char        liveCC[MP_CC_LEN], liveMode[MP_MODE_LEN], liveTags[MP_TAGS_LEN], liveDesc[MP_DESC_LEN]; int liveModeCol;   // 4.03
    int         players, maxPlayers, ping;
    uint64_t    querySent;
};

namespace ServerList
{
    void Load(const char* path);        // creates a default entry if the file doesn't exist
    void Save();
    int  Count();
    ServerEntry* Get(int i);
    int  CountInRegion(int region);
    bool Add(const char* name, const char* host, int port, int region, const char* password = "", const char* lang = ""); // false if full (100) or invalid
    void SetPassword(int i, const char* password);   // 2.19
    void SetDetails(int i, const char* cc, int modeCol, const char* mode, const char* tags, const char* desc);   // 4.03
    void SetFav(int i, bool fav);                                                                              // 4.03
    // Remember a server found on the LAN: updates the entry with the same host:port, or the
    // entry with the same name + port (the host phone got a new IP), otherwise adds it.
    void AddOrUpdate(const char* name, const char* host, int port, int region);
    int  FindByNamePort(const char* name, int port);   // -1 if not saved
    int  FindByHostPort(const char* host, int port);   // -1 if not saved
    void Remove(int i);
    void Refresh();                     // send a query to every server
    void Poll();                        // read replies; call every frame
    uint64_t LastRefreshMs();
}
