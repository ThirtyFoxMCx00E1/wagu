// 3.21.6: login system - phone side (see authclient.h)
#include "authclient.h"
#include "online.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define AUTH_BUSY_MS     20000   // no answer from the bot
#define AUTH_CODE_MS     (10 * 60 * 1000)

static std::map<std::string, std::string> ParseKv(const std::string& t)
{
    std::map<std::string, std::string> m;
    size_t p = 0;
    while(p < t.size())
    {
        size_t e = t.find('\n', p); if(e == std::string::npos) e = t.size();
        size_t q = t.find('=', p);
        if(q != std::string::npos && q < e) m[t.substr(p, q - p)] = t.substr(q + 1, e - q - 1);
        p = e + 1;
    }
    return m;
}
static std::string Clean(const std::string& v)
{
    std::string o; o.reserve(v.size());
    for(char c : v) if(c != '\n' && c != '\r' && c != '|') o += c;
    return o.substr(0, 200);
}
static bool Unhex64(const std::string& h, uint8_t out[64])
{
    if(h.size() != 128) return false;
    for(int i = 0; i < 64; ++i)
    {
        char b[3] = { h[2 * i], h[2 * i + 1], 0 }; char* e = nullptr;
        long v = strtol(b, &e, 16); if(!e || *e) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

void AuthClient::Set(State s) { state = s; stateAt = 0; }

void AuthClient::Send(const std::map<std::string, std::string>& kv)
{
    std::string plain;
    for(auto& it : kv) { plain += it.first; plain += '='; plain += Clean(it.second); plain += '\n'; }
    std::string f;
    if(AuthCrypto::Seal(m_S, plain, f)) Online::AuthLinkSend(f);
}

void AuthClient::Begin(const char chalHex[33], const char* serverName, bool accountOnPhone, const char* lastUser)
{
    m_Chal = chalHex ? chalHex : ""; m_Server = serverName ? serverName : "";
    err.clear(); info.clear(); createdNow = false; m_bTicket = false;
    m_bAccountHint = accountOnPhone;
    if(lastUser && *lastUser) prefillUser = lastUser;
    if(!m_S.ok) AuthCrypto::NewSession(m_S);
    if(!m_S.ok) { err = "Login system error (crypto). Restart the game."; Set(FAILED); return; }
    Online::AuthLinkStart(m_S.sid);
    m_bLinked = false; m_nLastHello = 0;
    m_AfterLink = !m_Sess.empty() ? TICKET : (accountOnPhone ? LOGIN : CODE);
    Set(LINKING);
}

void AuthClient::Stop()
{
    Online::AuthLinkStop();
    m_bLinked = false; m_bTicket = false; code[0] = 0; m_VToken.clear();
    Set(IDLE);
}

void AuthClient::NewCode()
{
    AuthCrypto::RandomCode(code);    // a fresh random code every time
    codeAt = 0;
    char rid[12]; snprintf(rid, sizeof(rid), "%d", ++m_nRid); m_GenRid = rid;
    Send({ { "op", "gen" }, { "code", code }, { "rid", rid }, { "dev", device } });
}

void AuthClient::Signup(const char* u, const char* p, const char* g, int age)
{
    if(state != SIGNUP) return;
    err.clear();
    char a[8]; snprintf(a, sizeof(a), "%d", age);
    char rid[12]; snprintf(rid, sizeof(rid), "%d", ++m_nRid);
    Send({ { "op", "signup" }, { "vtoken", m_VToken }, { "user", u }, { "pass", p }, { "gender", g }, { "age", a },
           { "chal", m_Chal }, { "srv", m_Server }, { "rid", rid }, { "dev", device } });
    m_AfterLink = SIGNUP; Set(BUSY); m_nBusySince = 0;
}

void AuthClient::Login(const char* u, const char* p)
{
    if(state != LOGIN) return;
    err.clear();
    char rid[12]; snprintf(rid, sizeof(rid), "%d", ++m_nRid);
    Send({ { "op", "login" }, { "user", u }, { "pass", p }, { "chal", m_Chal }, { "srv", m_Server }, { "rid", rid }, { "dev", device } });
    prefillUser = u;
    m_AfterLink = LOGIN; Set(BUSY); m_nBusySince = 0;
}

void AuthClient::WantNewAccount() { if(state == LOGIN) { err.clear(); info.clear(); Set(CODE); NewCode(); } }
void AuthClient::WantLogin()      { if(state == CODE || state == SIGNUP) { err.clear(); info.clear(); Set(LOGIN); } }

void AuthClient::RequestStats()
{
    if(m_Sess.empty()) { stats.clear(); stats["err"] = "Not logged in on this server."; return; }
    if(!m_bLinked) Online::AuthLinkStart(m_S.sid);
    statsPending = true; stats.clear();
    Send({ { "op", "stats" }, { "sess", m_Sess }, { "rid", "s" } });
}

void AuthClient::RequestAvatar()
{
    if(m_Sess.empty() || avatarPending) return;
    if(!m_bLinked) Online::AuthLinkStart(m_S.sid);
    avatarPending = true; avatarAt = 0;
    Send({ { "op", "avatar" }, { "sess", m_Sess }, { "rid", "a" } });
}

static int B64Val(char c)
{
    if(c >= 'A' && c <= 'Z') return c - 'A';
    if(c >= 'a' && c <= 'z') return c - 'a' + 26;
    if(c >= '0' && c <= '9') return c - '0' + 52;
    if(c == '+') return 62;
    if(c == '/') return 63;
    return -1;
}
static std::string B64Decode(const std::string& in)
{
    std::string out; out.reserve(in.size() * 3 / 4);
    int acc = 0, bits = 0;
    for(char c : in)
    {
        int v = B64Val(c); if(v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if(bits >= 8) { bits -= 8; out.push_back((char)((acc >> bits) & 0xFF)); }
    }
    return out;
}

bool AuthClient::TakeTicket(std::string& payload, uint8_t sig[64])
{
    if(!m_bTicket) return false;
    m_bTicket = false;
    payload = m_Ticket; memcpy(sig, m_Sig, 64);
    return true;
}

void AuthClient::ServerResult(bool ok, const char* msg)
{
    if(ok) { Set(DONE); return; }
    err = msg && *msg ? msg : "The server refused the login.";
    Set(LOGIN);
}

void AuthClient::OnAnswer(std::map<std::string, std::string>& a)
{
    const std::string& op = a["op"];
    bool ok = a["ok"] == "1";
    if(op == "hello") { if(ok && !m_bLinked) { m_bLinked = true; err.clear(); State n = m_AfterLink; Set(n);
                          if(n == CODE) NewCode();
                          else if(n == TICKET) Send({ { "op", "ticket" }, { "sess", m_Sess }, { "chal", m_Chal }, { "srv", m_Server }, { "rid", "t" }, { "dev", device } }); }
                        return; }
    if(op == "gen")
    {
        if(a["rid"] != m_GenRid) return;          // answer to an older code
        if(ok) { codeAt = 1; return; }            // the bot knows the code now
        if(a["err"] == "dup") { NewCode(); return; }   // taken by somebody else right now: another random one
        err = a["err"];
        return;
    }
    if(op == "verified")
    {   // 3.21.8: also right after linking (verified, then the game was restarted) and on the Login screen
        if(state == LINKING) { m_bLinked = true; err.clear(); }
        else if(state != CODE && state != LOGIN) return;
        if(a["vtoken"].empty() || a["vtoken"] == m_VToken) return;   // the same push again
        m_VToken = a["vtoken"]; discordName = a["dname"]; err.clear();
        if(a["has"] == "1") { prefillUser = a["user"]; info = "Discord verified (" + discordName + "). You already have the account " + a["user"] + ": log in."; Set(LOGIN); }
        else { info = "Discord verified: " + discordName; Set(SIGNUP); }
        return;
    }
    if(op == "signup" || op == "login" || op == "ticket")
    {
        if(state != BUSY && state != TICKET) return;
        if(ok && Unhex64(a["sig"], m_Sig) && !a["ticket"].empty())
        {
            if(!a["sess"].empty()) m_Sess = a["sess"];
            user = a["user"]; m_Ticket = a["ticket"]; m_bTicket = true;
            createdNow = op == "signup";
            Set(WAIT_SERVER);
            return;
        }
        err = a["err"].empty() ? std::string("Login failed.") : a["err"];
        if(a["relogin"] == "1") m_Sess.clear();
        Set(op == "ticket" ? LOGIN : m_AfterLink);
        return;
    }
    if(op == "stats")
    {
        statsPending = false;
        stats = a;
        if(!ok && a["relogin"] == "1") m_Sess.clear();
    }
    if(op == "avatar")   // 3.24: {ok, n, p0..p(n-1) = base64 PNG, dname} - wait=1: the bot is fetching it
    {
        if(ok && a["wait"] == "1") { avatarAt = 0; return; }   // the real answer comes as a push
        avatarPending = false;
        if(!ok) { if(a["relogin"] == "1") m_Sess.clear(); return; }
        int n = atoi(a["n"].c_str());
        if(n <= 0 || n > 400) return;
        std::string b64;
        for(int i = 0; i < n; ++i) { char k[8]; snprintf(k, sizeof(k), "p%d", i); b64 += a[k]; }
        std::string png = B64Decode(b64);
        if(png.size() < 8 || memcmp(png.data(), "\x89PNG", 4)) return;
        avatarPng.swap(png); avatarDiscord = a["dname"]; avatarReady = true;
    }
}

void AuthClient::Tick(uint64_t now)
{
    if(state == IDLE && !statsPending && !avatarPending) return;
    if(avatarPending) { if(!avatarAt) avatarAt = now; else if(now - avatarAt > 20000) avatarPending = false; }   // 3.24
    std::string f, plain;
    while(Online::AuthLinkRecv(f))
        if(AuthCrypto::Open(m_S, f, plain)) { auto kv = ParseKv(plain); OnAnswer(kv); }
    if(!stateAt) stateAt = now;
    switch(state)
    {
        case LINKING:
            if(Online::AuthLinkUp() && now - m_nLastHello > 2500) { m_nLastHello = now; Send({ { "op", "hello" }, { "rid", "h" }, { "dev", device } }); }
            // 3.21.7: the broker answers but the bot doesn't -> the bot is down, not the phone's internet
            if(Online::AuthLinkUp() && now - stateAt > 12000) err = "The MP login bot is offline right now (not your internet). It comes back by itself - live status: thirtyfoxmcx00e1.github.io/wagu/railway.html";
            else if(now - stateAt > 25000) err = "Can't reach the login server. Check your internet.";
            break;
        case CODE:
            if(!codeAt || codeAt == 1) { if(codeAt == 1) codeAt = now; }
            else if(now - codeAt > AUTH_CODE_MS) NewCode();      // expired: a new one
            if(codeAt == 0 && now - stateAt > 6000 && code[0]) { stateAt = now; NewCode(); }   // no answer yet: send again
            break;
        case BUSY: case TICKET:
            if(now - stateAt > AUTH_BUSY_MS) { err = "No answer from the login server. Try again."; Set(state == TICKET ? LOGIN : m_AfterLink); }
            break;
        default: break;
    }
}
