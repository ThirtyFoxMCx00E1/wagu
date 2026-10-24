// 3.21.6: login system (Discord verification + accounts). Crypto shared by the phone and the server.
//
// Who knows what:
//   - The accounts (user, password hash, gender, age, Discord id) live ONLY in the MP Discord bot
//     (Railway, SQLite). Nothing about them is stored in the server or in a public file.
//   - Phone <-> bot messages go over a public MQTT broker but are end-to-end encrypted:
//     ECDH P-256 (a new key for every login screen) with the bot's public key below (pinned in
//     the mod, nobody else has the private key) -> SHA-256 -> AES-256-GCM. The broker, the host of
//     the server and anyone listening see random bytes only. The password never reaches a server.
//   - The bot answers a good login / sign up with a TICKET: "WAGU1|user|discord|challenge|server|time"
//     signed (ECDSA P-256) by the bot. The phone gives it to the game server, which checks the
//     signature with the same public key + that the challenge is the one it gave this player.
//     A ticket works once, on one server, for one connection.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>

// the bot's public key (uncompressed P-256 point, hex). Its private key is the Railway variable
// AUTH_PRIVATE_KEY of the Discord bot - it is in no repository.
#define MP_AUTH_BOT_PUBKEY "047bfb24d8637c172eb9353130450cca575553ef9cb1d8a9538451fa940eac0d12246f940a367ccc3f5231b61701d19c2a3d56a80eb49215ad71baf7405701e006"
#define MP_AUTH_DISCORD_URL "https://discord.gg/tbsDknk34y"
#define MP_AUTH_CODE_LEN    8
#define MP_AUTH_TOPIC_REQ   "wagu/auth1/req"
#define MP_AUTH_TOPIC_RES   "wagu/auth1/res/"   // + session id (16 hex)

namespace AuthCrypto
{
    bool Random(void* out, size_t n);          // CSPRNG (mbedTLS CTR-DRBG, seeded from the OS)
    // a NEW random code every call: 8 characters, no 0/O/1/I (easy to type), 32^8 = 1.1 trillion
    void RandomCode(char out[MP_AUTH_CODE_LEN + 1]);

    struct Session
    {
        bool    ok = false;
        uint8_t key[32];       // AES-256 key (ECDH shared secret -> SHA-256)
        uint8_t pub[65];       // our one-time public key (sent with every request)
        char    sid[17];       // session id = response topic
    };
    bool NewSession(Session& s);
    // phone -> bot: [1][65 our public key][12 nonce][ciphertext][16 tag]
    bool Seal(const Session& s, const std::string& plain, std::string& frame);
    // bot -> phone: [1][12 nonce][ciphertext][16 tag]
    bool Open(const Session& s, const std::string& frame, std::string& plain);

    // server side: is this ticket really from the bot? (sig = r || s, 64 bytes)
    bool VerifyTicket(const char* payload, size_t len, const uint8_t sig[64]);
    struct Ticket { char user[24]; char discord[24]; char chal[33]; char server[40]; uint32_t when; };
    bool ParseTicket(const char* payload, size_t len, Ticket& t);

    void Hex(const uint8_t* d, size_t n, char* out);   // out: 2n + 1
}
