#pragma once
// 2.37: the default server's administrator.
//  - The name SENJI is reserved on every server. Only a phone that has the admin key may use it;
//    anyone else using that name is refused by the server (REJECT_NAME_RESERVED) and the name
//    turns red in the virtual keyboard and can't be entered.
//  - On the default server the real SENJI shows as "[ADMIN] SENJI": RGB rank (red, blue, violet, yellow, green) + white/black flowing name
//    and can kick / ban / mute and switch cheats off for everyone.
//  - Joining first no longer makes you the host (administrator) of the default server.
//  - Proof = the file  /sdcard/Android/data/com.rockstargames.gtasa/files/MP/admin.txt  on your
//    phone, with the admin key in it (one line). Players without that file who use SENJI are
//    blocked. The key itself is never in the source - only its SHA-256 below. The 24/7 server
//    can use another hash: mp_server --admin-hash <sha256 hex of "wagu-admin-v1:" + key>.
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>
#include <mbedtls/sha256.h>

#define MP_ADMIN_NAME      "SENJI"
#define MP_ADMIN_TAG       "[ADMIN] "
#define MP_ADMIN_KEY_LEN   128   // long keys with symbols (2.37: 88 characters)
// 3.23.6: NEW admin key (administrator.txt). The 3.11.6 key and the older ones are no longer accepted
// by 3.23.6+ servers. Older servers only know the old keys: update them.
#define MP_ADMIN_KEY_HASH  "16b216bb1901f1a6053a28fb0dcab7516e61e027eb0f6ef48ce43b344bae762e"

// "SENJI", " senji ", "[ADMIN] SENJI", "S E N J I", "SENJ1"... -> true
inline bool MP_IsReservedName(const char* name)
{
    if(!name) return false;
    char t[64]; int n = 0;
    const char* p = name;
    if(!strncasecmp(p, "[ADMIN]", 7)) p += 7;
    for(; *p && n < 63; ++p)
    {
        unsigned char c = (unsigned char)*p;
        if(c == ' ' || c == '_' || c == '-' || c == '.' || c == '*' || c == '|') continue;   // S_E_N_J_I
        if(c == '1' || c == '!' || c == 'l' || c == 'L') c = 'I';                         // SENJ1 / SENJl
        if(c == '3') c = 'E';
        if(c == '5' || c == '$') c = 'S';
        t[n++] = (char)toupper(c);
    }
    t[n] = 0;
    return !strcmp(t, MP_ADMIN_NAME);
}

// a name that pretends to be staff ("[ADMIN] Bob") -> strip the tag
inline void MP_StripAdminTag(char* name)
{
    for(bool again = true; name && again; )
    {
        again = false;
        int n = !strncasecmp(name, "[ADMIN]", 7) ? 7 : !strncasecmp(name, "[MOD]", 5) ? 5 : 0;   // 3.35: [MOD] too
        if(!n) break;
        memmove(name, name + n, strlen(name + n) + 1);
        while(*name == ' ') memmove(name, name + 1, strlen(name));
        again = true;
    }
}

// 3.35: moderators. The server owner lists them in moderator.txt (next to mp_server, NOT admin.txt):
//   <name> <key>        one per line, # = comment, the key at least 8 characters
// The player puts the same key in files/MP/moderator.txt on the phone and joins with that name ->
// "[MOD] name" + kick / mute / cheats switch on that server. The phone sends "MOD:<key>" in the
// join packet's admin key field (only when the name is not the reserved admin name).
#define MP_MOD_TAG    "[MOD] "
#define MP_MOD_PREFIX "MOD:"
inline bool MP_ModKeyOk(const char* file, const char* name, const char* key)
{
    if(!file || !*file || !name || !*name || !key || strlen(key) < 8) return false;
    FILE* f = fopen(file, "r"); if(!f) return false;
    char line[400]; bool ok = false;
    while(!ok && fgets(line, sizeof(line), f))
    {
        char* p = line; while(*p == ' ' || *p == '\t') ++p;
        if(*p == '#' || !*p) continue;
        char n[64] = {0}, k[MP_ADMIN_KEY_LEN + 1] = {0};
        if(sscanf(p, "%63s %128s", n, k) != 2) continue;
        if(!strcasecmp(n, name) && !strcmp(k, key)) ok = true;
    }
    fclose(f);
    return ok;
}

// 3.35.2: the MP moderator key. A phone with files/MP/moderator.txt holding THIS key is "[MOD] name" on
// every MP server (any name, no per-server list needed). Only the SHA-256 of "wagu-mod-v1:" + key is here.
#define MP_MOD_KEY_HASH "2acf313da4bdca869ada4694bb940ad8ad5d746bbda9205f2db75c8cab76fc6c"
inline bool MP_ModMasterOk(const char* key)
{
    if(!key || strlen(key) < 8) return false;
    char buf[200]; snprintf(buf, sizeof(buf), "wagu-mod-v1:%s", key);
    unsigned char h[32]; char hex[65];
    mbedtls_sha256((const unsigned char*)buf, strlen(buf), h, 0);
    for(int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", h[i]);
    return !strcasecmp(hex, MP_MOD_KEY_HASH);
}

inline void MP_AdminKeyHash(const char* key, char out[65])
{
    char buf[200]; snprintf(buf, sizeof(buf), "wagu-admin-v1:%s", key ? key : "");
    unsigned char h[32];
    mbedtls_sha256((const unsigned char*)buf, strlen(buf), h, 0);
    for(int i = 0; i < 32; ++i) snprintf(out + i * 2, 3, "%02x", h[i]);
}

inline bool MP_AdminKeyOk(const char* key, const char* hashHex = nullptr)
{
    if(!key || !*key) return false;
    char h[65]; MP_AdminKeyHash(key, h);
    if(hashHex && *hashHex && !strcasecmp(h, hashHex)) return true;   // mp_server --admin-hash <hex>
    return !strcasecmp(h, MP_ADMIN_KEY_HASH);
}

// 3.21: the keys of 3.10 (admin.txt) and 2.37, used ONLY by the phone, ONLY as a retry when a server
// refuses the new key (= that server runs MP older than 3.11.6 and only knows these). Servers never
// accept them (MP_AdminKeyOk above), so a 3.21 server still takes the new key only. This changes
// nothing for security: an old server accepts an old key from any phone anyway.
#define MP_ADMIN_KEY_HASH_LEGACY1 "2a12f3acbdb4c820d18f784aff3261fabeddb7fffc35b8123970095def9a5d1b"   // 3.10 admin.txt
#define MP_ADMIN_KEY_HASH_LEGACY2 "e77bf27eaeb77088c97e17903677366c90dfaeb6fa4b5d78a07d722058ddf633"   // 2.37
#define MP_ADMIN_KEY_HASH_LEGACY3 "420b31a8e646f682dadd484b41fb46cd69810971edecb97165cfe675465d9ed0"   // 3.11.6 - 3.23.5
inline bool MP_AdminKeyLegacyOk(const char* key)
{
    if(!key || !*key) return false;
    char h[65]; MP_AdminKeyHash(key, h);
    return !strcasecmp(h, MP_ADMIN_KEY_HASH_LEGACY1) || !strcasecmp(h, MP_ADMIN_KEY_HASH_LEGACY2) || !strcasecmp(h, MP_ADMIN_KEY_HASH_LEGACY3);
}

inline bool MP_IsAdminTagged(const char* name) { return name && !strncmp(name, MP_ADMIN_TAG, strlen(MP_ADMIN_TAG)); }
