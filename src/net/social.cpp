// 3.63: MP SOCIAL (see social.h)
#include "social.h"
#include "online.h"
#include "authcrypto.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <algorithm>
#include <map>
#include <set>
#include <deque>
#include <memory>
#include <array>
#include <chrono>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>

#ifdef __ANDROID__
  #include <android/log.h>
  #define SLOG(...) __android_log_print(ANDROID_LOG_INFO, "MP-Social", __VA_ARGS__)
#else
  #define SLOG(...) do { printf("[MP Social] " __VA_ARGS__); printf("\n"); } while(0)
#endif

using namespace Social;

namespace
{
    const std::string ROOT = "wagu/soc1/";
    const char FS = '\x1f';   // envelope fields
    const char RS = '\x1e';   // fields inside a body
    const int  PARTY_MAX = 5;
    const int  CONV_KEEP = 150;
    const int  PRESENCE_EVERY_S = 60, PRESENCE_FRESH_S = 150;

    bool g_bInit = false;
    std::string g_Dir, g_Me, g_MyName, g_PubHex;
    mbedtls_ecp_group g_Grp; mbedtls_mpi g_D; mbedtls_ecp_point g_Q;
    bool g_bKey = false;

    std::vector<Person> g_Friends, g_In, g_Out;
    std::map<std::string, Person> g_People;          // id -> name + pub (directory + everyone we heard from)
    std::map<std::string, int64_t> g_Pres;           // id -> last presence time
    std::vector<std::unique_ptr<Conv>> g_Convs;
    std::vector<std::unique_ptr<Community>> g_Coms;
    std::map<std::string, std::map<std::string, std::string>> g_ComMem;   // cid -> id -> "j" / "f" / "jf"
    std::map<std::string, std::string> g_ComMemName;                     // id -> name (members)
    std::vector<std::unique_ptr<Notif>> g_Notifs;
    std::deque<std::string> g_SeenQ; std::set<std::string> g_Seen;      // mailbox message ids already handled
    std::set<std::string> g_PartyOk;                  // party gids you accepted (waiting for the leader's update)
    std::map<std::string, std::array<uint8_t, 32>> g_Keys;   // peer pub -> shared AES key

    struct PendingMail { std::string to, kind, body; int64_t until; };
    std::vector<PendingMail> g_Pending;               // waiting for the receiver's public key

    std::string g_Query; int64_t g_QueryAt = 0; bool g_bDirSub = false;
    std::string g_ViewCid;
    int  g_nGen = -1;
    int64_t g_LastPres = 0;
    bool g_bDirty = false; int64_t g_SaveAt = 0;
    std::string g_LastProfileName;

    int64_t Now() { return (int64_t)time(nullptr); }
    uint64_t NowMs() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    void Dirty() { g_bDirty = true; }

    int Rng(void*, unsigned char* o, size_t n) { return AuthCrypto::Random(o, n) ? 0 : -1; }

    std::string Hex(const uint8_t* d, size_t n) { std::string s(n * 2, '0'); AuthCrypto::Hex(d, n, &s[0]); s.resize(n * 2); return s; }
    int HexVal(char c) { if(c >= '0' && c <= '9') return c - '0'; if(c >= 'a' && c <= 'f') return c - 'a' + 10; if(c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
    bool Unhex(const std::string& h, std::string& out)
    {
        if(h.size() & 1) return false;
        out.resize(h.size() / 2);
        for(size_t i = 0; i < out.size(); ++i) { int a = HexVal(h[2 * i]), b = HexVal(h[2 * i + 1]); if(a < 0 || b < 0) return false; out[i] = (char)(a * 16 + b); }
        return true;
    }
    std::string RandHex(int bytes) { uint8_t b[16]; if(bytes > 16) bytes = 16; if(!AuthCrypto::Random(b, bytes)) for(int i = 0; i < bytes; ++i) b[i] = (uint8_t)rand(); return Hex(b, bytes); }

    // names / texts can't break the formats: no control characters (the separators are control characters)
    std::string Clean(const std::string& s, size_t maxLen)
    {
        std::string o; o.reserve(s.size());
        for(unsigned char c : s) { if(c < 0x20) c = ' '; o += (char)c; }
        if(o.size() > maxLen) { o.resize(maxLen); while(!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back(); if(!o.empty() && (unsigned char)o.back() >= 0xC0) o.pop_back(); }
        return o;
    }
    std::string CleanName(const std::string& s) { std::string o = Clean(s, 24); for(char& c : o) if(c == ':' || c == ',') c = '_'; return o; }
    std::vector<std::string> Split(const std::string& s, char d)
    {
        std::vector<std::string> v; size_t a = 0;
        for(;;) { size_t b = s.find(d, a); if(b == std::string::npos) { v.push_back(s.substr(a)); break; } v.push_back(s.substr(a, b - a)); a = b + 1; }
        return v;
    }

    std::string IdFromPub(const std::string& pubHex)
    {
        std::string raw; if(pubHex.size() != 130 || !Unhex(pubHex, raw)) return "";
        uint8_t h[32]; mbedtls_sha256((const unsigned char*)raw.data(), raw.size(), h, 0);
        uint32_t v = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        char b[12]; snprintf(b, sizeof(b), "%08u", v % 100000000u); return b;
    }

    // ---- key ----
    bool LoadOrMakeKey()
    {
        mbedtls_ecp_group_init(&g_Grp); mbedtls_mpi_init(&g_D); mbedtls_ecp_point_init(&g_Q);
        if(mbedtls_ecp_group_load(&g_Grp, MBEDTLS_ECP_DP_SECP256R1) != 0) return false;
        std::string path = g_Dir + "/key.txt";
        if(FILE* f = fopen(path.c_str(), "r"))
        {
            char d[80] = "", q[160] = "";
            if(fscanf(f, "%70s %140s", d, q) == 2)
            {
                std::string dr, qr;
                if(Unhex(d, dr) && dr.size() == 32 && Unhex(q, qr) && qr.size() == 65 &&
                   mbedtls_mpi_read_binary(&g_D, (const unsigned char*)dr.data(), 32) == 0 &&
                   mbedtls_ecp_point_read_binary(&g_Grp, &g_Q, (const unsigned char*)qr.data(), 65) == 0 &&
                   mbedtls_ecp_check_pubkey(&g_Grp, &g_Q) == 0 && mbedtls_ecp_check_privkey(&g_Grp, &g_D) == 0)
                { fclose(f); g_PubHex = q; return true; }
            }
            fclose(f);
        }
        if(mbedtls_ecp_gen_keypair(&g_Grp, &g_D, &g_Q, Rng, nullptr) != 0) return false;
        uint8_t d[32], q[65]; size_t ql = 0;
        if(mbedtls_mpi_write_binary(&g_D, d, 32) != 0) return false;
        if(mbedtls_ecp_point_write_binary(&g_Grp, &g_Q, MBEDTLS_ECP_PF_UNCOMPRESSED, &ql, q, sizeof(q)) != 0 || ql != 65) return false;
        g_PubHex = Hex(q, 65);
        if(FILE* f = fopen(path.c_str(), "w")) { fprintf(f, "%s %s\n# MP social key: your player ID. Keep it secret, don't share this file.\n", Hex(d, 32).c_str(), g_PubHex.c_str()); fclose(f); chmod(path.c_str(), 0600); }
        memset(d, 0, sizeof(d));
        return true;
    }
    std::string Sign(const std::string& data)
    {
        uint8_t h[32]; mbedtls_sha256((const unsigned char*)data.data(), data.size(), h, 0);
        mbedtls_mpi r, s; mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
        std::string out;
        uint8_t sig[64];
        if(mbedtls_ecdsa_sign(&g_Grp, &r, &s, &g_D, h, 32, Rng, nullptr) == 0 && mbedtls_mpi_write_binary(&r, sig, 32) == 0 && mbedtls_mpi_write_binary(&s, sig + 32, 32) == 0) out = Hex(sig, 64);
        mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
        return out;
    }
    bool ReadPub(const std::string& pubHex, mbedtls_ecp_point& Q)
    {
        std::string raw; if(pubHex.size() != 130 || !Unhex(pubHex, raw)) return false;
        return mbedtls_ecp_point_read_binary(&g_Grp, &Q, (const unsigned char*)raw.data(), 65) == 0 && mbedtls_ecp_check_pubkey(&g_Grp, &Q) == 0;
    }
    bool Verify(const std::string& data, const std::string& pubHex, const std::string& sigHex)
    {
        std::string sig; if(sigHex.size() != 128 || !Unhex(sigHex, sig)) return false;
        mbedtls_ecp_point Q; mbedtls_mpi r, s; mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
        uint8_t h[32]; mbedtls_sha256((const unsigned char*)data.data(), data.size(), h, 0);
        bool ok = ReadPub(pubHex, Q) && mbedtls_mpi_read_binary(&r, (const unsigned char*)sig.data(), 32) == 0 &&
                  mbedtls_mpi_read_binary(&s, (const unsigned char*)sig.data() + 32, 32) == 0 && mbedtls_ecdsa_verify(&g_Grp, h, 32, &Q, &r, &s) == 0;
        mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
        return ok;
    }
    bool SharedKey(const std::string& pubHex, uint8_t key[32])
    {
        auto it = g_Keys.find(pubHex);
        if(it != g_Keys.end()) { memcpy(key, it->second.data(), 32); return true; }
        mbedtls_ecp_point Q; mbedtls_mpi z; mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&z);
        uint8_t zb[32];
        bool ok = ReadPub(pubHex, Q) && mbedtls_ecdh_compute_shared(&g_Grp, &z, &Q, &g_D, Rng, nullptr) == 0 && mbedtls_mpi_write_binary(&z, zb, 32) == 0;
        if(ok)
        {
            mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
            mbedtls_sha256_update(&c, (const unsigned char*)"wagu-soc-v1", 11); mbedtls_sha256_update(&c, zb, 32);
            mbedtls_sha256_finish(&c, key); mbedtls_sha256_free(&c);
            std::array<uint8_t, 32> a; memcpy(a.data(), key, 32); g_Keys[pubHex] = a;
        }
        memset(zb, 0, sizeof(zb));
        mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&z);
        return ok;
    }
    bool Seal(const std::string& peerPub, const std::string& plain, std::string& outHex)
    {
        uint8_t key[32]; if(!SharedKey(peerPub, key)) return false;
        uint8_t iv[12]; if(!AuthCrypto::Random(iv, 12)) return false;
        std::string buf(12 + plain.size() + 16, '\0');
        memcpy(&buf[0], iv, 12);
        mbedtls_gcm_context g; mbedtls_gcm_init(&g);
        bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
                  mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, plain.size(), iv, 12, (const unsigned char*)"WS1", 3, (const unsigned char*)plain.data(), (unsigned char*)&buf[12], 16, (unsigned char*)&buf[12 + plain.size()]) == 0;
        mbedtls_gcm_free(&g);
        if(ok) outHex = Hex((const uint8_t*)buf.data(), buf.size());
        return ok;
    }
    bool Unseal(const std::string& peerPub, const std::string& hex, std::string& plain)
    {
        std::string buf; if(!Unhex(hex, buf) || buf.size() < 28) return false;
        uint8_t key[32]; if(!SharedKey(peerPub, key)) return false;
        size_t n = buf.size() - 28;
        plain.assign(n, '\0');
        mbedtls_gcm_context g; mbedtls_gcm_init(&g);
        uint8_t dummy;
        bool ok = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256) == 0 &&
                  mbedtls_gcm_auth_decrypt(&g, n, (const unsigned char*)buf.data(), 12, (const unsigned char*)"WS1", 3, (const unsigned char*)buf.data() + 12 + n, 16, (const unsigned char*)buf.data() + 12, n ? (unsigned char*)&plain[0] : &dummy) == 0;
        mbedtls_gcm_free(&g);
        if(!ok) plain.clear();
        return ok;
    }

    // ---- envelope: WS1 | kind | fromId | fromName | ts | to | body | pub | sig ----
    struct Env { std::string kind, from, name, to, body, pub; int64_t ts = 0; };
    std::string MakeEnv(const std::string& kind, const std::string& to, const std::string& body)
    {
        std::string s = "WS1"; s += FS; s += kind; s += FS; s += g_Me; s += FS; s += g_MyName; s += FS;
        s += std::to_string((long long)Now()); s += FS; s += to; s += FS; s += body; s += FS; s += g_PubHex; s += FS;
        return s + Sign(s);
    }
    bool ParseEnv(const std::string& p, Env& e)
    {
        if(p.size() < 200 || p.size() > 64 * 1024 || p.compare(0, 4, "WS1\x1f")) return false;
        size_t sigAt = p.rfind(FS); if(sigAt == std::string::npos) return false;
        std::vector<std::string> f = Split(p, FS);
        if(f.size() != 9) return false;
        e.kind = f[1]; e.from = f[2]; e.name = CleanName(f[3]); e.ts = atoll(f[4].c_str()); e.to = f[5]; e.body = f[6]; e.pub = f[7];
        if(IdFromPub(e.pub) != e.from) return false;              // the ID belongs to that key
        return Verify(p.substr(0, sigAt + 1), e.pub, f[8]);       // and that key signed it
    }

    // ---- lookups ----
    Person* FindIn(std::vector<Person>& v, const std::string& id) { for(auto& p : v) if(p.id == id) return &p; return nullptr; }
    void RemoveFrom(std::vector<Person>& v, const std::string& id) { v.erase(std::remove_if(v.begin(), v.end(), [&](const Person& p) { return p.id == id; }), v.end()); }
    std::string PubOf(const std::string& id)
    {
        if(Person* p = FindIn(g_Friends, id)) return p->pub;
        if(Person* p = FindIn(g_In, id)) return p->pub;
        if(Person* p = FindIn(g_Out, id)) if(!p->pub.empty()) return p->pub;
        auto it = g_People.find(id); return it != g_People.end() ? it->second.pub : "";
    }
    Conv* ConvByKey(const std::string& k) { for(auto& c : g_Convs) if(c->key == k) return c.get(); return nullptr; }
    Community* ComById(const std::string& cid) { for(auto& c : g_Coms) if(c->cid == cid) return c.get(); return nullptr; }
    bool IsMember(const Conv* c, const std::string& id) { for(auto& m : c->members) if(m.id == id) return true; return false; }

    Notif* AddNotif(int kind, const std::string& fromId, const std::string& fromName, const std::string& ref, const std::string& text)
    {
        // chats / community posts: one line per chat that counts up, not a new line per message
        if(kind == N_MSG || kind == N_COMMUNITY)
            for(auto& n : g_Notifs) if(n->kind == kind && n->ref == ref && !n->read) { n->fromId = fromId; n->fromName = fromName; n->text = text; n->ts = Now(); Dirty(); return n.get(); }
        std::unique_ptr<Notif> n(new Notif());
        n->kind = kind; n->fromId = fromId; n->fromName = fromName; n->ref = ref; n->text = Clean(text, 160); n->ts = Now();
        Notif* r = n.get();
        g_Notifs.push_back(std::move(n));
        if(g_Notifs.size() > 120) g_Notifs.erase(g_Notifs.begin());
        Dirty();
        return r;
    }
    void DoneNotifs(int kind, const std::string& fromId, const std::string& ref)
    {
        for(auto& n : g_Notifs) if(n->kind == kind && n->fromId == fromId && (ref.empty() || n->ref == ref)) { n->done = true; n->read = true; }
        Dirty();
    }

    void Pub(const std::string& topic, const std::string& payload, bool retain) { Online::SocLinkPub(ROOT + topic, payload, retain); }
    void Sub(const std::string& f) { Online::SocLinkSub(ROOT + f); }
    void Unsub(const std::string& f) { Online::SocLinkUnsub(ROOT + f); }

    // a private message into someone's mailbox (waits on the broker until they come online)
    bool Mail(const std::string& to, const std::string& kind, const std::string& body)
    {
        if(to.empty() || to == g_Me) return false;
        std::string pub = PubOf(to);
        if(pub.empty())
        {   // don't know their key yet: ask the directory, send when it comes
            Sub("u/" + to);
            if(g_Pending.size() < 200) g_Pending.push_back({ to, kind, body, Now() + 600 });
            return true;
        }
        std::string sealed;
        if(!Seal(pub, body, sealed)) return false;
        Pub("m/" + to + "/" + RandHex(8), MakeEnv(kind, to, sealed), true);
        return true;
    }

    void PublishProfile()
    {
        if(g_MyName.empty()) return;
        Pub("u/" + g_Me, MakeEnv("prof", "", g_MyName), true);
        g_LastProfileName = g_MyName;
    }
    void PublishPresence() { Pub("o/" + g_Me, std::to_string((long long)Now()), true); g_LastPres = Now(); }

    std::string MembersStr(const Conv* c)
    {
        std::string s;
        for(auto& m : c->members) { if(!s.empty()) s += ','; s += m.id; s += ':'; s += CleanName(m.name); }
        return s;
    }
    std::string GroupBody(const Conv* c)
    {
        std::string b = c->gid; b += RS; b += std::to_string(c->kind); b += RS; b += Clean(c->title, 40); b += RS; b += c->owner; b += RS; b += MembersStr(c);
        return b;
    }
    void GroupBroadcast(const Conv* c, const std::string& kind, const std::string& body, const std::string& except = "")
    {
        for(auto& m : c->members) if(m.id != g_Me && m.id != except) Mail(m.id, kind, body);
    }
    void AddMsg(Conv* c, const std::string& fromId, const std::string& fromName, const std::string& text, int64_t ts, bool mine)
    {
        Msg m; m.fromId = fromId; m.fromName = fromName; m.text = Clean(text, 400); m.ts = ts ? ts : Now(); m.mine = mine;
        if(m.ts > Now() + 60 || m.ts < Now() - 30 * 86400) m.ts = Now();   // wrong phone clocks
        c->msgs.push_back(m);
        if((int)c->msgs.size() > CONV_KEEP) c->msgs.erase(c->msgs.begin(), c->msgs.begin() + (c->msgs.size() - CONV_KEEP));
        c->last = Now();
        if(!mine) ++c->unread;
        Dirty();
    }
    Conv* MakeDm(const std::string& id, const std::string& name)
    {
        std::string k = "u" + id;
        if(Conv* c = ConvByKey(k)) { if(!name.empty()) c->title = name; return c; }
        std::unique_ptr<Conv> c(new Conv()); c->key = k; c->kind = CONV_DM; c->title = name.empty() ? id : name; c->last = Now();
        c->members.push_back({ g_Me, g_MyName }); c->members.push_back({ id, c->title });
        Conv* r = c.get(); g_Convs.push_back(std::move(c)); Dirty();
        return r;
    }
    void RemoveConv(const std::string& key) { g_Convs.erase(std::remove_if(g_Convs.begin(), g_Convs.end(), [&](const std::unique_ptr<Conv>& c) { return c->key == key; }), g_Convs.end()); Dirty(); }
    void FixOwner(Conv* c) { if(!IsMember(c, c->owner) && !c->members.empty()) c->owner = c->members.front().id; }

    void ComSubs(const Community* c, bool on)
    {
        if(on) { Sub("c/" + c->cid + "/m/+"); Sub("c/" + c->cid + "/p/+"); }
        else   { Unsub("c/" + c->cid + "/m/+"); Unsub("c/" + c->cid + "/p/+"); }
    }
    bool ComWatched(const Community* c) { return c->joined || c->followed || c->ownerId == g_Me || c->cid == g_ViewCid; }
    void ComCount(Community* c)
    {
        c->members = c->followers = 0; c->memberList.clear();
        for(auto& kv : g_ComMem[c->cid])
        {
            if(kv.second.find('j') != std::string::npos) { ++c->members; auto it = g_ComMemName.find(kv.first); c->memberList.push_back({ kv.first, it != g_ComMemName.end() ? it->second : kv.first }); }
            if(kv.second.find('f') != std::string::npos) ++c->followers;
        }
    }
    void PublishMembership(Community* c)
    {
        std::string f; if(c->joined) f += 'j'; if(c->followed) f += 'f';
        if(f.empty()) Pub("c/" + c->cid + "/m/" + g_Me, "", true);
        else Pub("c/" + c->cid + "/m/" + g_Me, MakeEnv("cm", c->cid, f), true);
        if(f.empty()) g_ComMem[c->cid].erase(g_Me); else { g_ComMem[c->cid][g_Me] = f; g_ComMemName[g_Me] = g_MyName; }
        ComCount(c);
    }

    // ---- save / load (tab separated, one record per line) ----
    std::string T(const std::string& s) { std::string o = s; for(char& c : o) if(c == '\t' || c == '\n' || c == '\r') c = ' '; return o; }
    void Save()
    {
        std::string path = g_Dir + "/state.txt", tmp = path + ".tmp";
        FILE* f = fopen(tmp.c_str(), "w"); if(!f) return;
        fprintf(f, "# MP social v1\n");
        for(auto& p : g_Friends) fprintf(f, "F\t%s\t%s\t%s\t%lld\n", p.id.c_str(), T(p.name).c_str(), p.pub.c_str(), (long long)p.since);
        for(auto& p : g_In)      fprintf(f, "I\t%s\t%s\t%s\t%lld\n", p.id.c_str(), T(p.name).c_str(), p.pub.c_str(), (long long)p.since);
        for(auto& p : g_Out)     fprintf(f, "O\t%s\t%s\t%s\t%lld\n", p.id.c_str(), T(p.name).c_str(), p.pub.c_str(), (long long)p.since);
        for(auto& c : g_Convs)
        {
            fprintf(f, "C\t%s\t%d\t%s\t%s\t%s\t%lld\t%d\t%s\n", c->key.c_str(), c->kind, c->gid.c_str(), T(c->title).c_str(), c->owner.c_str(), (long long)c->last, c->unread, T(MembersStr(c.get())).c_str());
            for(auto& m : c->msgs) fprintf(f, "M\t%s\t%lld\t%d\t%s\t%s\t%s\n", c->key.c_str(), (long long)m.ts, m.mine ? 1 : 0, m.fromId.c_str(), T(m.fromName).c_str(), T(m.text).c_str());
        }
        for(auto& c : g_Coms) if(c->joined || c->followed || c->ownerId == g_Me)
            fprintf(f, "K\t%s\t%d\t%d\t%lld\t%lld\t%s\t%s\t%s\t%s\n", c->cid.c_str(), c->joined ? 1 : 0, c->followed ? 1 : 0, (long long)c->seen, (long long)c->ts, c->ownerId.c_str(), T(c->ownerName).c_str(), T(c->name).c_str(), T(c->desc).c_str());
        for(auto& n : g_Notifs) fprintf(f, "N\t%d\t%s\t%s\t%s\t%lld\t%d\t%d\t%s\n", n->kind, n->fromId.c_str(), T(n->fromName).c_str(), T(n->ref).c_str(), (long long)n->ts, n->read ? 1 : 0, n->done ? 1 : 0, T(n->text).c_str());
        for(auto& g : g_PartyOk) fprintf(f, "P\t%s\n", g.c_str());
        for(auto& s : g_SeenQ) fprintf(f, "S\t%s\n", s.c_str());
        fclose(f);
        rename(tmp.c_str(), path.c_str());
        g_bDirty = false;
    }
    void Load()
    {
        std::string path = g_Dir + "/state.txt";
        FILE* f = fopen(path.c_str(), "r"); if(!f) return;
        char* line = nullptr; size_t cap = 0; ssize_t n;
        while((n = getline(&line, &cap, f)) > 0)
        {
            std::string l(line, n); while(!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if(l.empty() || l[0] == '#') continue;
            std::vector<std::string> v = Split(l, '\t');
            const std::string& t = v[0];
            if((t == "F" || t == "I" || t == "O") && v.size() >= 5)
            {
                Person p; p.id = v[1]; p.name = v[2]; p.pub = v[3]; p.since = atoll(v[4].c_str());
                (t == "F" ? g_Friends : t == "I" ? g_In : g_Out).push_back(p);
            }
            else if(t == "C" && v.size() >= 9)
            {
                std::unique_ptr<Conv> c(new Conv()); c->key = v[1]; c->kind = atoi(v[2].c_str()); c->gid = v[3]; c->title = v[4]; c->owner = v[5]; c->last = atoll(v[6].c_str()); c->unread = atoi(v[7].c_str());
                for(auto& m : Split(v[8], ',')) { size_t k = m.find(':'); if(k != std::string::npos) c->members.push_back({ m.substr(0, k), m.substr(k + 1) }); }
                g_Convs.push_back(std::move(c));
            }
            else if(t == "M" && v.size() >= 7)
            {
                if(Conv* c = ConvByKey(v[1])) { Msg m; m.ts = atoll(v[2].c_str()); m.mine = v[3] == "1"; m.fromId = v[4]; m.fromName = v[5]; m.text = v[6]; c->msgs.push_back(m); }
            }
            else if(t == "K" && v.size() >= 10)
            {
                std::unique_ptr<Community> c(new Community()); c->cid = v[1]; c->joined = v[2] == "1"; c->followed = v[3] == "1"; c->seen = atoll(v[4].c_str()); c->ts = atoll(v[5].c_str());
                c->ownerId = v[6]; c->ownerName = v[7]; c->name = v[8]; c->desc = v[9];
                g_Coms.push_back(std::move(c));
            }
            else if(t == "N" && v.size() >= 9)
            {
                std::unique_ptr<Notif> x(new Notif()); x->kind = atoi(v[1].c_str()); x->fromId = v[2]; x->fromName = v[3]; x->ref = v[4]; x->ts = atoll(v[5].c_str()); x->read = v[6] == "1"; x->done = v[7] == "1"; x->text = v[8];
                g_Notifs.push_back(std::move(x));
            }
            else if(t == "P" && v.size() >= 2) g_PartyOk.insert(v[1]);
            else if(t == "S" && v.size() >= 2) { g_SeenQ.push_back(v[1]); g_Seen.insert(v[1]); }
        }
        free(line);
        fclose(f);
    }

    // ---- incoming ----
    void OnMail(const std::string& mid, const Env& e)
    {
        if(e.to != g_Me) return;
        if(g_Seen.count(mid)) return;
        g_Seen.insert(mid); g_SeenQ.push_back(mid);
        while(g_SeenQ.size() > 300) { g_Seen.erase(g_SeenQ.front()); g_SeenQ.pop_front(); }
        Dirty();
        // a friend's key never changes: a different key with the same ID is someone else
        if(Person* fr = FindIn(g_Friends, e.from)) if(!fr->pub.empty() && fr->pub != e.pub) { SLOG("mail from %s with a different key: ignored", e.from.c_str()); return; }
        std::string body;
        if(!Unseal(e.pub, e.body, body)) return;
        g_People[e.from] = Person{ e.from, e.name, e.pub, Now() };
        const std::string& k = e.kind;
        if(k == "freq")
        {
            if(FindIn(g_Friends, e.from)) { Mail(e.from, "facc", ""); return; }
            if(FindIn(g_Out, e.from))
            {   // both asked: friends
                RemoveFrom(g_Out, e.from);
                g_Friends.push_back(Person{ e.from, e.name, e.pub, Now() });
                Mail(e.from, "facc", "");
                AddNotif(N_FRIEND_ACC, e.from, e.name, "", e.name + " is now your friend");
                Sub("o/" + e.from);
                return;
            }
            if(!FindIn(g_In, e.from)) g_In.push_back(Person{ e.from, e.name, e.pub, Now() });
            DoneNotifs(N_FRIEND_REQ, e.from, "");
            AddNotif(N_FRIEND_REQ, e.from, e.name, "", e.name + " #" + e.from + " sent you a friend request");
        }
        else if(k == "facc")
        {
            if(!FindIn(g_Out, e.from) && !FindIn(g_Friends, e.from)) return;   // you never asked
            RemoveFrom(g_Out, e.from);
            if(!FindIn(g_Friends, e.from)) { g_Friends.push_back(Person{ e.from, e.name, e.pub, Now() }); AddNotif(N_FRIEND_ACC, e.from, e.name, "", e.name + " accepted your friend request. You can chat now."); }
        }
        else if(k == "fdec")
        {
            if(!FindIn(g_Out, e.from)) return;
            RemoveFrom(g_Out, e.from);
            AddNotif(N_FRIEND_DEC, e.from, e.name, "", e.name + " declined your friend request");
        }
        else if(k == "fcan") { RemoveFrom(g_In, e.from); DoneNotifs(N_FRIEND_REQ, e.from, ""); }
        else if(k == "frem") { RemoveFrom(g_Friends, e.from); }
        else if(k == "dm")
        {
            Person* fr = FindIn(g_Friends, e.from); if(!fr) return;   // only friends can write to you
            fr->name = e.name;
            Conv* c = MakeDm(e.from, e.name);
            AddMsg(c, e.from, e.name, body, e.ts, false);
            AddNotif(N_MSG, e.from, e.name, c->key, e.name + ": " + Clean(body, 80));
        }
        else if(k == "ginv" || k == "gupd")
        {
            std::vector<std::string> f = Split(body, RS); if(f.size() < 5) return;
            const std::string gid = f[0]; int kind = atoi(f[1].c_str()); if(kind != CONV_GROUP && kind != CONV_PARTY) return;
            std::string key = "g" + gid;
            Conv* c = ConvByKey(key);
            bool allowed = (c && IsMember(c, e.from)) || (!c && kind == CONV_GROUP && FindIn(g_Friends, e.from)) || (!c && kind == CONV_PARTY && g_PartyOk.count(gid));
            if(!allowed) return;
            std::vector<Member> mem;
            for(auto& m : Split(f[4], ',')) { size_t p = m.find(':'); if(p != std::string::npos) mem.push_back({ m.substr(0, p), m.substr(p + 1) }); }
            bool meIn = false; for(auto& m : mem) if(m.id == g_Me) meIn = true;
            if(!meIn) { if(c) { RemoveConv(key); AddNotif(N_INFO, e.from, e.name, "", std::string(kind == CONV_PARTY ? "You left the party" : "You were removed from ") + (kind == CONV_PARTY ? "" : f[2])); } return; }
            bool fresh = !c;
            if(fresh)
            {
                if(kind == CONV_PARTY) { Conv* old = Party(); if(old && old->gid != gid) LeaveParty(); }
                std::unique_ptr<Conv> n(new Conv()); n->key = key; n->gid = gid; n->kind = kind; c = n.get(); g_Convs.push_back(std::move(n));
            }
            c->title = Clean(f[2], 40); c->owner = f[3]; c->members = mem; c->last = Now();
            for(auto& m : mem) if(m.id != g_Me && PubOf(m.id).empty()) Sub("u/" + m.id);
            g_PartyOk.erase(gid);
            if(fresh && kind == CONV_GROUP) AddNotif(N_GROUP, e.from, e.name, key, e.name + " added you to the group " + c->title);
            if(fresh && kind == CONV_PARTY) AddNotif(N_INFO, e.from, e.name, key, "You joined " + e.name + "'s party");
            Dirty();
        }
        else if(k == "gmsg")
        {
            std::vector<std::string> f = Split(body, RS); if(f.size() < 2) return;
            Conv* c = ConvByKey("g" + f[0]); if(!c || !IsMember(c, e.from)) return;
            AddMsg(c, e.from, e.name, f[1], e.ts, false);
            if(c->kind == CONV_GROUP) AddNotif(N_MSG, e.from, e.name, c->key, c->title + " - " + e.name + ": " + Clean(f[1], 70));
        }
        else if(k == "glv")
        {
            Conv* c = ConvByKey("g" + body); if(!c) return;
            c->members.erase(std::remove_if(c->members.begin(), c->members.end(), [&](const Member& m) { return m.id == e.from; }), c->members.end());
            FixOwner(c);
            AddMsg(c, "", "", e.name + (c->kind == CONV_PARTY ? " left the party" : " left the group"), e.ts, true);
            c->unread = 0;
        }
        else if(k == "pinv")
        {
            std::vector<std::string> f = Split(body, RS); if(f.size() < 1 || !FindIn(g_Friends, e.from)) return;
            DoneNotifs(N_PARTY_INV, e.from, f[0]);
            AddNotif(N_PARTY_INV, e.from, e.name, f[0], e.name + " invited you to their party");
        }
        else if(k == "pacc")
        {
            Conv* c = ConvByKey("g" + body);
            if(!c || c->kind != CONV_PARTY || c->owner != g_Me) return;
            if(!IsMember(c, e.from) && (int)c->members.size() < PARTY_MAX) c->members.push_back({ e.from, e.name });
            else if(!IsMember(c, e.from)) { AddNotif(N_INFO, e.from, e.name, "", "The party is full (5): " + e.name + " couldn't join"); return; }
            AddMsg(c, "", "", e.name + " joined the party", Now(), true); c->unread = 0;
            AddNotif(N_PARTY_ACC, e.from, e.name, c->key, e.name + " joined your party");
            GroupBroadcast(c, "gupd", GroupBody(c));
        }
        else if(k == "pdec")
        {
            AddNotif(N_INFO, e.from, e.name, "", e.name + " declined your party invite");
        }
    }

    void OnTopic(const std::string& topic, const std::string& payload)
    {
        if(topic.compare(0, ROOT.size(), ROOT)) return;
        std::string t = topic.substr(ROOT.size());
        std::vector<std::string> p = Split(t, '/');
        if(p.size() == 2 && p[0] == "o")
        {
            if(payload.empty()) g_Pres.erase(p[1]);
            else { int64_t ts = atoll(payload.c_str()); if(ts > 0) g_Pres[p[1]] = ts; }
            return;
        }
        if(payload.empty())
        {
            if(p.size() == 3 && p[0] == "c" && p[2] == "i") { if(Community* c = ComById(p[1])) if(c->ownerId != g_Me || !c->joined) c->deleted = true; }
            else if(p.size() == 4 && p[0] == "c" && p[2] == "m") { g_ComMem[p[1]].erase(p[3]); if(Community* c = ComById(p[1])) ComCount(c); }
            else if(p.size() == 4 && p[0] == "c" && p[2] == "p") { if(Community* c = ComById(p[1])) c->posts.erase(std::remove_if(c->posts.begin(), c->posts.end(), [&](const Post& x) { return x.id == p[3]; }), c->posts.end()); }
            return;
        }
        Env e;
        if(!ParseEnv(payload, e))
        {
            if(p.size() == 3 && p[0] == "m" && p[1] == g_Me) Pub(t, "", true);   // junk in our mailbox: clear it
            return;
        }
        if(p.size() == 2 && p[0] == "u" && e.kind == "prof" && e.from == p[1])
        {
            Person& pp = g_People[e.from]; pp.id = e.from; pp.name = CleanName(e.body); pp.pub = e.pub; pp.since = e.ts;
            if(Person* f = FindIn(g_Friends, e.from)) if(f->pub == e.pub) { if(f->name != pp.name) { f->name = pp.name; if(Conv* c = ConvByKey("u" + e.from)) c->title = pp.name; Dirty(); } }
            if(Person* o = FindIn(g_Out, e.from)) if(o->pub.empty()) { o->pub = e.pub; Dirty(); }
            // mails waiting for this key
            for(size_t i = 0; i < g_Pending.size(); )
            {
                if(g_Pending[i].to == e.from) { PendingMail pm = g_Pending[i]; g_Pending.erase(g_Pending.begin() + i); Mail(pm.to, pm.kind, pm.body); }
                else ++i;
            }
            return;
        }
        if(p.size() == 3 && p[0] == "m" && p[1] == g_Me)
        {
            OnMail(p[2], e);
            Pub(t, "", true);   // read: clear it from the broker
            return;
        }
        if(p.size() == 3 && p[0] == "c" && p[2] == "i" && e.kind == "com")
        {
            if(p[1].compare(0, e.from.size() + 1, e.from + "-")) return;   // only the maker of this ID
            std::vector<std::string> f = Split(e.body, RS);
            Community* c = ComById(p[1]);
            if(!c) { std::unique_ptr<Community> n(new Community()); n->cid = p[1]; c = n.get(); g_Coms.push_back(std::move(n)); }
            c->name = Clean(f[0], 40); c->desc = f.size() > 1 ? Clean(f[1], 300) : ""; c->ownerId = e.from; c->ownerName = e.name; c->ts = e.ts; c->deleted = false;
            if(ComWatched(c)) ComSubs(c, true);
            return;
        }
        if(p.size() == 4 && p[0] == "c" && p[2] == "m" && e.kind == "cm" && e.from == p[3] && e.to == p[1])
        {
            g_ComMem[p[1]][e.from] = e.body.substr(0, 2); g_ComMemName[e.from] = e.name;
            if(Community* c = ComById(p[1])) ComCount(c);
            return;
        }
        if(p.size() == 4 && p[0] == "c" && p[2] == "p" && e.kind == "cp" && e.to == p[1])
        {
            Community* c = ComById(p[1]); if(!c) return;
            for(auto& x : c->posts) if(x.id == p[3]) return;
            Post po; po.id = p[3]; po.fromId = e.from; po.fromName = e.name; po.text = Clean(e.body, 600); po.ts = e.ts;
            auto it = std::lower_bound(c->posts.begin(), c->posts.end(), po, [](const Post& a, const Post& b) { return a.ts > b.ts; });
            c->posts.insert(it, po);
            if(c->posts.size() > 200) c->posts.pop_back();
            if((c->joined || c->followed) && e.from != g_Me && e.ts > c->seen && c->cid != g_ViewCid)
                AddNotif(N_COMMUNITY, e.from, e.name, c->cid, c->name + " - " + e.name + ": " + Clean(e.body, 70));
            return;
        }
    }
}

// =============================================================================================

void Social::Init(const char* dataDir)
{
    if(g_bInit) return;
    g_Dir = std::string(dataDir ? dataDir : ".") + "/social";
    mkdir(g_Dir.c_str(), 0700);
    g_bKey = LoadOrMakeKey();
    if(!g_bKey) { SLOG("no key: social off"); return; }
    g_Me = IdFromPub(g_PubHex);
    Load();
    g_bInit = true;
    for(auto& c : g_Coms) c->deleted = false;
    Online::SocLinkStart((ROOT + "o/" + g_Me).c_str());
    Sub("m/" + g_Me + "/#");   // the mailbox
    Sub("o/+");                // who is online
    Sub("c/+/i");              // communities
    for(auto& c : g_Coms) if(ComWatched(c.get())) ComSubs(c.get(), true);
    SLOG("social: you are #%s", g_Me.c_str());
}

void Social::Shutdown()
{
    if(!g_bInit) return;
    if(g_bDirty) Save();
    Online::SocLinkStop();
}

void Social::Tick()
{
    if(!g_bInit) return;
    int gen = Online::SocLinkGen();
    if(Online::SocLinkUp() && gen != g_nGen)
    {   // (re)connected
        g_nGen = gen;
        PublishProfile(); PublishPresence();
        for(auto& c : g_Coms) if(c->ownerId == g_Me && !c->deleted && !c->name.empty())
        { std::string b = c->name; b += RS; b += c->desc; Pub("c/" + c->cid + "/i", MakeEnv("com", "", b), true); }
    }
    if(Online::SocLinkUp())
    {
        if(Now() - g_LastPres >= PRESENCE_EVERY_S) PublishPresence();
        if(g_MyName != g_LastProfileName) PublishProfile();
    }
    // incoming: signatures cost a few ms each, so a time budget per frame (a big directory comes over many frames)
    uint64_t t0 = NowMs();
    std::string topic, payload;
    while(NowMs() - t0 < 6 && Online::SocLinkRecv(topic, payload)) OnTopic(topic, payload);
    // old waiting mails
    int64_t now = Now();
    g_Pending.erase(std::remove_if(g_Pending.begin(), g_Pending.end(), [&](const PendingMail& p) { return p.until < now; }), g_Pending.end());
    if(g_bDirty && now - g_SaveAt >= 2) { g_SaveAt = now; Save(); }
}

bool Social::Up() { return g_bInit && Online::SocLinkUp(); }
const char* Social::Broker() { return Online::SocLinkBroker(); }
const std::string& Social::MyId() { return g_Me; }
const std::string& Social::MyName() { return g_MyName; }
void Social::SetName(const char* name)
{
    std::string n = CleanName(name ? name : "");
    if(n == g_MyName) return;
    g_MyName = n;
    for(auto& c : g_Convs) for(auto& m : c->members) if(m.id == g_Me) m.name = n;
}
bool Social::IsOnline(const std::string& id)
{
    if(id == g_Me) return Up();
    auto it = g_Pres.find(id);
    return it != g_Pres.end() && Now() - it->second < PRESENCE_FRESH_S;
}
std::string Social::NameOf(const std::string& id)
{
    if(id == g_Me) return g_MyName;
    if(Person* p = FindIn(g_Friends, id)) return p->name;
    auto it = g_People.find(id); return it != g_People.end() ? it->second.name : "";
}

// ---- friends ----
void Social::Search(const std::string& q0)
{
    std::string q = q0; while(!q.empty() && q[0] == ' ') q.erase(0, 1); while(!q.empty() && q.back() == ' ') q.pop_back();
    if(!q.empty() && q[0] == '#') q.erase(0, 1);
    g_Query = q; g_QueryAt = Now();
    bool digits = !q.empty() && q.size() <= 8 && std::all_of(q.begin(), q.end(), [](char c) { return c >= '0' && c <= '9'; });
    if(digits && q.size() == 8) Sub("u/" + q);
    if(!g_bDirSub && !q.empty()) { g_bDirSub = true; Sub("u/+"); }
}
const std::string& Social::SearchQuery() { return g_Query; }
std::vector<Person> Social::SearchResults()
{
    std::vector<Person> r;
    if(g_Query.empty()) return r;
    std::string ql = g_Query; for(char& c : ql) c = (char)tolower((unsigned char)c);
    for(auto& kv : g_People)
    {
        const Person& p = kv.second;
        if(p.id == g_Me || p.name.empty()) continue;
        std::string nl = p.name; for(char& c : nl) c = (char)tolower((unsigned char)c);
        bool hit = p.id == ql || (ql.size() >= 3 && p.id.find(ql) == 0) || nl.find(ql) != std::string::npos;
        if(hit) r.push_back(p);
        if(r.size() >= 60) break;
    }
    std::sort(r.begin(), r.end(), [&](const Person& a, const Person& b) {
        bool ea = a.id == ql || !strcasecmp(a.name.c_str(), g_Query.c_str()), eb = b.id == ql || !strcasecmp(b.name.c_str(), g_Query.c_str());
        if(ea != eb) return ea;
        bool oa = IsOnline(a.id), ob = IsOnline(b.id);
        if(oa != ob) return oa;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    return r;
}
bool Social::Searching() { return !g_Query.empty() && Now() - g_QueryAt < 4; }
const std::vector<Person>& Social::Friends() { return g_Friends; }
const std::vector<Person>& Social::Incoming() { return g_In; }
const std::vector<Person>& Social::Outgoing() { return g_Out; }
bool Social::IsFriend(const std::string& id) { return FindIn(g_Friends, id) != nullptr; }
bool Social::IsOutgoing(const std::string& id) { return FindIn(g_Out, id) != nullptr; }
bool Social::IsIncoming(const std::string& id) { return FindIn(g_In, id) != nullptr; }
bool Social::AddFriend(const std::string& id)
{
    if(!g_bInit || id == g_Me || IsFriend(id) || IsOutgoing(id)) return false;
    if(IsIncoming(id)) { Accept(id); return true; }
    std::string name = NameOf(id), pub = PubOf(id);
    g_Out.push_back(Person{ id, name.empty() ? id : name, pub, Now() });
    Mail(id, "freq", g_MyName);
    Dirty();
    return true;
}
void Social::Accept(const std::string& id)
{
    Person* p = FindIn(g_In, id); if(!p) return;
    Person f = *p; f.since = Now();
    RemoveFrom(g_In, id);
    if(!IsFriend(id)) g_Friends.push_back(f);
    Mail(id, "facc", "");
    DoneNotifs(N_FRIEND_REQ, id, "");
    Dirty();
}
void Social::Decline(const std::string& id)
{
    if(!FindIn(g_In, id)) return;
    RemoveFrom(g_In, id);
    Mail(id, "fdec", "");
    DoneNotifs(N_FRIEND_REQ, id, "");
    Dirty();
}
void Social::CancelRequest(const std::string& id)
{
    if(!FindIn(g_Out, id)) return;
    RemoveFrom(g_Out, id);
    Mail(id, "fcan", "");
    Dirty();
}
void Social::RemoveFriend(const std::string& id)
{
    if(!IsFriend(id)) return;
    Mail(id, "frem", "");
    RemoveFrom(g_Friends, id);
    RemoveConv("u" + id);
    Dirty();
}

// ---- chats ----
std::vector<Conv*> Social::Convs()
{
    std::vector<Conv*> v; for(auto& c : g_Convs) v.push_back(c.get());
    std::sort(v.begin(), v.end(), [](const Conv* a, const Conv* b) { return a->last > b->last; });
    return v;
}
Conv* Social::FindConv(const std::string& key) { return ConvByKey(key); }
Conv* Social::OpenDm(const std::string& id) { if(!IsFriend(id)) return nullptr; return MakeDm(id, NameOf(id)); }
void Social::MarkRead(const std::string& key)
{
    if(Conv* c = ConvByKey(key)) if(c->unread) { c->unread = 0; Dirty(); }
    for(auto& n : g_Notifs) if(n->kind == N_MSG && n->ref == key && !n->read) { n->read = true; Dirty(); }
}
bool Social::SendText(const std::string& key, const std::string& text0)
{
    Conv* c = ConvByKey(key); if(!c) return false;
    std::string text = Clean(text0, 400);
    while(!text.empty() && text.back() == ' ') text.pop_back();
    if(text.empty()) return false;
    if(c->kind == CONV_DM)
    {
        std::string id = key.substr(1);
        if(!IsFriend(id)) return false;
        Mail(id, "dm", text);
    }
    else { std::string b = c->gid; b += RS; b += text; GroupBroadcast(c, "gmsg", b); }
    AddMsg(c, g_Me, g_MyName, text, Now(), true);
    return true;
}
std::string Social::CreateGroup(const std::string& name, const std::vector<std::string>& ids)
{
    if(!g_bInit) return "";
    std::unique_ptr<Conv> c(new Conv());
    c->gid = g_Me + RandHex(4); c->key = "g" + c->gid; c->kind = CONV_GROUP; c->owner = g_Me; c->last = Now();
    c->members.push_back({ g_Me, g_MyName });
    std::string title;
    for(auto& id : ids) if(IsFriend(id) && !IsMember(c.get(), id)) { c->members.push_back({ id, NameOf(id) }); if(title.size() < 30) { if(!title.empty()) title += ", "; title += NameOf(id); } }
    c->title = Clean(name, 40);
    if(c->title.empty()) c->title = title.empty() ? "New group" : title;
    Conv* r = c.get(); g_Convs.push_back(std::move(c));
    GroupBroadcast(r, "ginv", GroupBody(r));
    AddMsg(r, "", "", "You made the group " + r->title, Now(), true);
    return r->key;
}
void Social::AddToGroup(const std::string& key, const std::string& id)
{
    Conv* c = ConvByKey(key); if(!c || c->kind != CONV_GROUP || !IsFriend(id) || IsMember(c, id)) return;
    c->members.push_back({ id, NameOf(id) });
    GroupBroadcast(c, "gupd", GroupBody(c), id);
    Mail(id, "ginv", GroupBody(c));
    AddMsg(c, "", "", "You added " + NameOf(id), Now(), true);
}
void Social::LeaveGroup(const std::string& key)
{
    Conv* c = ConvByKey(key); if(!c) return;
    if(c->kind != CONV_DM) GroupBroadcast(c, "glv", c->gid);
    RemoveConv(key);
}
int Social::UnreadChats() { int n = 0; for(auto& c : g_Convs) n += c->unread; return n; }

// ---- party ----
Conv* Social::Party() { for(auto& c : g_Convs) if(c->kind == CONV_PARTY) return c.get(); return nullptr; }
std::string Social::CreateParty()
{
    if(Conv* p = Party()) return p->key;
    std::unique_ptr<Conv> c(new Conv());
    c->gid = g_Me + RandHex(4); c->key = "g" + c->gid; c->kind = CONV_PARTY; c->owner = g_Me; c->last = Now();
    c->title = (g_MyName.empty() ? std::string("My") : g_MyName + "'s") + " party";
    c->members.push_back({ g_Me, g_MyName });
    Conv* r = c.get(); g_Convs.push_back(std::move(c));
    AddMsg(r, "", "", "Party made. Invite up to 4 friends.", Now(), true);
    return r->key;
}
bool Social::InviteToParty(const std::string& id)
{
    if(!IsFriend(id)) return false;
    Conv* p = Party(); if(!p) { CreateParty(); p = Party(); }
    if(!p || p->owner != g_Me || (int)p->members.size() >= PARTY_MAX || IsMember(p, id)) return false;
    std::string b = p->gid; b += RS; b += p->title;
    Mail(id, "pinv", b);
    AddMsg(p, "", "", "Invited " + NameOf(id), Now(), true);
    return true;
}
void Social::AcceptParty(const std::string& gid)
{
    std::string owner;
    for(auto& n : g_Notifs) if(n->kind == N_PARTY_INV && n->ref == gid && !n->done) { owner = n->fromId; n->done = n->read = true; }
    if(owner.empty()) return;
    if(Conv* old = Party()) if(old->gid != gid) LeaveParty();
    g_PartyOk.insert(gid);
    Mail(owner, "pacc", gid);
    Dirty();
}
void Social::DeclineParty(const std::string& gid)
{
    for(auto& n : g_Notifs) if(n->kind == N_PARTY_INV && n->ref == gid && !n->done) { n->done = n->read = true; Mail(n->fromId, "pdec", gid); }
    Dirty();
}
void Social::LeaveParty() { if(Conv* p = Party()) LeaveGroup(p->key); }

// ---- communities ----
std::vector<Community*> Social::Communities()
{
    std::vector<Community*> v; for(auto& c : g_Coms) if(!c->deleted && !c->name.empty()) v.push_back(c.get());
    std::sort(v.begin(), v.end(), [](const Community* a, const Community* b) { return a->ts > b->ts; });
    return v;
}
Community* Social::FindCommunity(const std::string& cid) { return ComById(cid); }
std::string Social::CreateCommunity(const std::string& name, const std::string& desc)
{
    if(!g_bInit) return "";
    std::string n = Clean(name, 40); if(n.empty()) return "";
    std::unique_ptr<Community> c(new Community());
    c->cid = g_Me + "-" + RandHex(3); c->name = n; c->desc = Clean(desc, 300); c->ownerId = g_Me; c->ownerName = g_MyName; c->ts = Now();
    c->joined = c->followed = true; c->seen = Now();
    Community* r = c.get(); g_Coms.push_back(std::move(c));
    std::string b = r->name; b += RS; b += r->desc;
    Pub("c/" + r->cid + "/i", MakeEnv("com", "", b), true);
    ComSubs(r, true);
    PublishMembership(r);
    Dirty();
    return r->cid;
}
void Social::JoinCommunity(const std::string& cid, bool join)
{
    Community* c = ComById(cid); if(!c || c->joined == join) return;
    c->joined = join; if(join) c->followed = true;
    PublishMembership(c);
    if(!ComWatched(c)) ComSubs(c, false); else ComSubs(c, true);
    Dirty();
}
void Social::FollowCommunity(const std::string& cid, bool follow)
{
    Community* c = ComById(cid); if(!c || c->followed == follow) return;
    c->followed = follow;
    PublishMembership(c);
    if(!ComWatched(c)) ComSubs(c, false); else ComSubs(c, true);
    Dirty();
}
bool Social::PostCommunity(const std::string& cid, const std::string& text)
{
    Community* c = ComById(cid); if(!c || !(c->joined || c->ownerId == g_Me)) return false;
    std::string t = Clean(text, 600); if(t.empty()) return false;
    std::string pid = std::to_string((long long)Now()) + RandHex(3);
    Pub("c/" + cid + "/p/" + pid, MakeEnv("cp", cid, t), true);
    Post po; po.id = pid; po.fromId = g_Me; po.fromName = g_MyName; po.text = t; po.ts = Now();
    c->posts.insert(c->posts.begin(), po);
    c->seen = Now(); Dirty();
    return true;
}
void Social::DeleteCommunity(const std::string& cid)
{
    Community* c = ComById(cid); if(!c || c->ownerId != g_Me) return;
    for(auto& p : c->posts) Pub("c/" + cid + "/p/" + p.id, "", true);
    Pub("c/" + cid + "/m/" + g_Me, "", true);
    Pub("c/" + cid + "/i", "", true);
    ComSubs(c, false);
    c->deleted = true; c->joined = c->followed = false;
    Dirty();
}
void Social::ViewCommunity(const std::string& cid)
{
    if(cid == g_ViewCid) { if(Community* c = ComById(cid)) if(!c->posts.empty() && c->posts.front().ts > c->seen) { c->seen = c->posts.front().ts; Dirty(); } return; }
    std::string old = g_ViewCid; g_ViewCid = cid;
    if(Community* o = ComById(old)) if(!ComWatched(o)) ComSubs(o, false);
    if(Community* c = ComById(cid))
    {
        ComSubs(c, true);
        for(auto& n : g_Notifs) if(n->kind == N_COMMUNITY && n->ref == cid) n->read = true;
        Dirty();
    }
}

// ---- inbox ----
std::vector<Notif*> Social::Inbox()
{
    std::vector<Notif*> v; for(auto& n : g_Notifs) v.push_back(n.get());
    std::sort(v.begin(), v.end(), [](const Notif* a, const Notif* b) { return a->ts > b->ts; });
    return v;
}
int Social::UnreadInbox() { int n = 0; for(auto& x : g_Notifs) if(!x->read) ++n; return n; }
void Social::MarkInboxRead()
{
    bool ch = false;
    for(auto& x : g_Notifs) if(!x->read && x->kind != N_FRIEND_REQ && x->kind != N_PARTY_INV) { x->read = true; ch = true; }
    if(ch) Dirty();
}
void Social::ClearInbox()
{
    g_Notifs.erase(std::remove_if(g_Notifs.begin(), g_Notifs.end(), [](const std::unique_ptr<Notif>& n) {
        return !((n->kind == N_FRIEND_REQ || n->kind == N_PARTY_INV) && !n->done); }), g_Notifs.end());
    Dirty();
}
