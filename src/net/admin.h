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
#define MP_ADMIN_KEY_HASH  "e77bf27eaeb77088c97e17903677366c90dfaeb6fa4b5d78a07d722058ddf633"

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
    while(name && !strncasecmp(name, "[ADMIN]", 7))
    {
        memmove(name, name + 7, strlen(name + 7) + 1);
        while(*name == ' ') memmove(name, name + 1, strlen(name));
    }
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
    return !strcasecmp(h, hashHex && *hashHex ? hashHex : MP_ADMIN_KEY_HASH);
}

inline bool MP_IsAdminTagged(const char* name) { return name && !strncmp(name, MP_ADMIN_TAG, strlen(MP_ADMIN_TAG)); }
