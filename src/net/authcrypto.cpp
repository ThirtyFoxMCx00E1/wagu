// 3.21.6: login system crypto (see authcrypto.h)
#include "authcrypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>
#include <time.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>

namespace
{
    std::mutex               g_M;
    bool                     g_bInit = false, g_bOk = false;
    mbedtls_entropy_context  g_Ent;
    mbedtls_ctr_drbg_context g_Drbg;

    bool InitLocked()
    {
        if(g_bInit) return g_bOk;
        g_bInit = true;
        mbedtls_entropy_init(&g_Ent);
        mbedtls_ctr_drbg_init(&g_Drbg);
        static const char pers[] = "wagu-auth-drbg";
        g_bOk = mbedtls_ctr_drbg_seed(&g_Drbg, mbedtls_entropy_func, &g_Ent, (const unsigned char*)pers, sizeof(pers) - 1) == 0;
        return g_bOk;
    }
    int RngLocked(void*, unsigned char* out, size_t n) { return mbedtls_ctr_drbg_random(&g_Drbg, out, n); }

    int HexVal(char c)
    {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    bool Unhex(const char* h, uint8_t* out, size_t n)
    {
        for(size_t i = 0; i < n; ++i)
        {
            int a = HexVal(h[2 * i]), b = a < 0 ? -1 : HexVal(h[2 * i + 1]);
            if(a < 0 || b < 0) return false;
            out[i] = (uint8_t)(a * 16 + b);
        }
        return true;
    }
    bool BotPoint(mbedtls_ecp_group& grp, mbedtls_ecp_point& Q)
    {
        static const char* kHex = MP_AUTH_BOT_PUBKEY;
        uint8_t raw[65];
        if(strlen(kHex) != 130 || !Unhex(kHex, raw, 65)) return false;
        if(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) return false;
        if(mbedtls_ecp_point_read_binary(&grp, &Q, raw, 65) != 0) return false;
        return mbedtls_ecp_check_pubkey(&grp, &Q) == 0;
    }
    bool Gcm(bool enc, const uint8_t key[32], const uint8_t iv[12], char dir, const uint8_t* in, size_t n, uint8_t* out, uint8_t tag[16])
    {
        mbedtls_gcm_context g; mbedtls_gcm_init(&g);
        bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0;
        const unsigned char aad[2] = { 'W', (unsigned char)dir };   // C = phone -> bot, S = bot -> phone
        if(ok)
        {
            if(enc) ok = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, iv, 12, aad, 2, in, out, 16, tag) == 0;
            else    ok = mbedtls_gcm_auth_decrypt(&g, n, iv, 12, aad, 2, tag, 16, in, out) == 0;
        }
        mbedtls_gcm_free(&g);
        return ok;
    }
}

void AuthCrypto::Hex(const uint8_t* d, size_t n, char* out)
{
    static const char* k = "0123456789abcdef";
    for(size_t i = 0; i < n; ++i) { out[2 * i] = k[d[i] >> 4]; out[2 * i + 1] = k[d[i] & 15]; }
    out[2 * n] = 0;
}

bool AuthCrypto::Random(void* out, size_t n)
{
    std::lock_guard<std::mutex> lk(g_M);
    if(!InitLocked()) return false;
    return mbedtls_ctr_drbg_random(&g_Drbg, (unsigned char*)out, n) == 0;
}

void AuthCrypto::RandomCode(char out[MP_AUTH_CODE_LEN + 1])
{
    static const char kAlpha[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // 32 symbols: 256 % 32 == 0, no bias
    uint8_t r[MP_AUTH_CODE_LEN];
    if(!Random(r, sizeof(r)))
    {   // should never happen; still never a fixed code
        uint64_t x = (uint64_t)(uintptr_t)out ^ (uint64_t)rand() << 20 ^ (uint64_t)time(nullptr);
        for(int i = 0; i < MP_AUTH_CODE_LEN; ++i) { x = x * 6364136223846793005ULL + 1442695040888963407ULL; r[i] = (uint8_t)(x >> 33); }
    }
    for(int i = 0; i < MP_AUTH_CODE_LEN; ++i) out[i] = kAlpha[r[i] & 31];
    out[MP_AUTH_CODE_LEN] = 0;
}

bool AuthCrypto::NewSession(Session& s)
{
    s.ok = false;
    std::lock_guard<std::mutex> lk(g_M);
    if(!InitLocked()) return false;
    mbedtls_ecp_group grp; mbedtls_ecp_point Qbot, Q; mbedtls_mpi d, z;
    mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&Qbot); mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&d); mbedtls_mpi_init(&z);
    bool ok = BotPoint(grp, Qbot)
           && mbedtls_ecdh_gen_public(&grp, &d, &Q, RngLocked, nullptr) == 0
           && mbedtls_ecdh_compute_shared(&grp, &z, &Qbot, &d, RngLocked, nullptr) == 0;
    uint8_t zb[32]; size_t ql = 0;
    if(ok) ok = mbedtls_mpi_write_binary(&z, zb, 32) == 0;
    if(ok) ok = mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &ql, s.pub, sizeof(s.pub)) == 0 && ql == 65;
    if(ok)
    {   // key = SHA-256("wagu-auth-v1" || z || our public key)
        mbedtls_sha256_context h; mbedtls_sha256_init(&h);
        mbedtls_sha256_starts(&h, 0);
        mbedtls_sha256_update(&h, (const unsigned char*)"wagu-auth-v1", 12);
        mbedtls_sha256_update(&h, zb, 32);
        mbedtls_sha256_update(&h, s.pub, 65);
        mbedtls_sha256_finish(&h, s.key);
        mbedtls_sha256_free(&h);
        uint8_t id[32]; mbedtls_sha256(s.pub, 65, id, 0);
        Hex(id, 8, s.sid);
        s.ok = true;
    }
    memset(zb, 0, sizeof(zb));
    mbedtls_ecp_group_free(&grp); mbedtls_ecp_point_free(&Qbot); mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&z);
    return s.ok;
}

bool AuthCrypto::Seal(const Session& s, const std::string& plain, std::string& frame)
{
    if(!s.ok || plain.size() > 4096) return false;
    uint8_t iv[12]; if(!Random(iv, sizeof(iv))) return false;
    frame.assign(1 + 65 + 12 + plain.size() + 16, '\0');
    uint8_t* f = (uint8_t*)&frame[0];
    f[0] = 1; memcpy(f + 1, s.pub, 65); memcpy(f + 66, iv, 12);
    return Gcm(true, s.key, iv, 'C', (const uint8_t*)plain.data(), plain.size(), f + 78, f + 78 + plain.size());
}

bool AuthCrypto::Open(const Session& s, const std::string& frame, std::string& plain)
{
    if(!s.ok || frame.size() < 1 + 12 + 16 || (uint8_t)frame[0] != 1) return false;
    const uint8_t* f = (const uint8_t*)frame.data();
    size_t n = frame.size() - 29;
    plain.assign(n, '\0');
    uint8_t tag[16]; memcpy(tag, f + 13 + n, 16);
    if(!Gcm(false, s.key, f + 1, 'S', f + 13, n, n ? (uint8_t*)&plain[0] : tag, tag)) { plain.clear(); return false; }
    return true;
}

bool AuthCrypto::VerifyTicket(const char* payload, size_t len, const uint8_t sig[64])
{
    mbedtls_ecp_group grp; mbedtls_ecp_point Q; mbedtls_mpi r, sv;
    mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&sv);
    uint8_t h[32]; mbedtls_sha256((const unsigned char*)payload, len, h, 0);
    bool ok = BotPoint(grp, Q)
           && mbedtls_mpi_read_binary(&r, sig, 32) == 0
           && mbedtls_mpi_read_binary(&sv, sig + 32, 32) == 0
           && mbedtls_ecdsa_verify(&grp, h, 32, &Q, &r, &sv) == 0;
    mbedtls_ecp_group_free(&grp); mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&r); mbedtls_mpi_free(&sv);
    return ok;
}

bool AuthCrypto::ParseTicket(const char* payload, size_t len, Ticket& t)
{
    memset(&t, 0, sizeof(t));
    if(len < 6 || len > 255 || memcmp(payload, "WAGU1|", 6)) return false;
    char buf[256]; memcpy(buf, payload, len); buf[len] = 0;
    char* parts[6] = { 0 }; int n = 0;
    for(char* p = buf; n < 6; )
    {
        parts[n++] = p;
        char* bar = strchr(p, '|');
        if(!bar) break;
        *bar = 0; p = bar + 1;
    }
    if(n != 6) return false;
    snprintf(t.user, sizeof(t.user), "%s", parts[1]);
    snprintf(t.discord, sizeof(t.discord), "%s", parts[2]);
    snprintf(t.chal, sizeof(t.chal), "%s", parts[3]);
    snprintf(t.server, sizeof(t.server), "%s", parts[4]);
    t.when = (uint32_t)strtoul(parts[5], nullptr, 10);
    return t.user[0] && strlen(t.chal) == 32;
}
