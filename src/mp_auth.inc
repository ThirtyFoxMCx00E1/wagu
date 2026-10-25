// 3.21.6: login system screens (the logic is in net/authclient.cpp, the server side in net/server.cpp).
//
//  connect -> the server sends a challenge -> the < Spawn > buttons wait for this:
//    new phone:  CODE    "type /gen XXXXXXXX in #verification" + the Discord invite
//                SIGNUP  User / Password / Gender / Age  [Create]  -> "Account created successfully"
//    rejoin:     LOGIN   User / Password  [Login]
//    same game session (host change, reconnect): nothing to do, the ticket goes by itself
//  Brief Case War: the same system, its own look (briefcase icon, red + black, moving highlight).
//  The account (user, Discord) is only known by the MP bot and the server: never in the chat,
//  never in the player list. This phone keeps only the last user name (account.txt, no password).

static AuthClient s_Auth;
static bool       s_bAuthReq = false;        // this connection's server wants a login
static int        s_nAuthStyle = AUTH_STYLE_NORMAL;
static char       s_szAuthChal[33] = "";
static uint64_t   s_nAuthDoneAt = 0, s_nAuthTicketAt = 0, s_nAuthJoinAt = 0;
static std::string s_AuthTicket; static uint8_t s_AuthSig[64];
static bool       s_bAuthTicket = false;
static std::string s_AuthUser;               // the account this game session is logged in with
static bool       s_bStatsOpen = false;
static uint64_t   s_nStatsAt = 0;

struct AuthField { char buf[40]; int max; bool secret; bool numeric; const char* title; };
// .max is the VKeyboard buffer size (it allows max-1 characters, room for the '\0'): 20 chars = 21, 32 = 33, 2 digits = 3
static AuthField s_AfUser = { "", 21, false, false, "User" };
static AuthField s_AfPass = { "", 33, true,  false, "Password" };
static AuthField s_AfAge  = { "", 3,  false, true,  "Age" };
static const int AUTH_AGE_MIN = 10, AUTH_AGE_MAX = 24;   // allowed sign-up ages (keep the hint text below in step)
static int       s_nAfGender = 0;            // 0 Male, 1 Female, 2 Other
static AuthField* s_pAfEditing = nullptr;
static const char* kAuthGenders[3] = { "Male", "Female", "Other" };

static void AuthAccountPath(char* out, size_t n) { snprintf(out, n, "%s/account.txt", s_szDataDir); }
// account.txt: user=NAME (the login screen fills it in). 3.22 also kept a login=TOKEN "remember me" token
// there so the game logged in by itself after a restart / mod update. Now the login screen always opens
// after a game restart (you type the password), so no token is kept; an old login= line is removed the next
// time the game connects. Set AUTH_AUTOLOGIN_AT_START to true to bring the 3.22 auto login back.
// (A reconnect inside the same game session still needs no screen: its session lives in memory.)
static const bool AUTH_AUTOLOGIN_AT_START = false;
static std::string s_szAuthSavedTok;
static std::string AuthAccountValue(const char* key)
{
    char p[512]; AuthAccountPath(p, sizeof(p));
    std::string u; const size_t kl = strlen(key);
    if(FILE* f = fopen(p, "r"))
    {
        char line[160];
        while(fgets(line, sizeof(line), f))
            if(!strncmp(line, key, kl) && line[kl] == '=') { u = line + kl + 1; while(!u.empty() && (u.back() == '\n' || u.back() == '\r' || u.back() == ' ')) u.pop_back(); }
        fclose(f);
    }
    return u;
}
static std::string AuthLastUser() { return AuthAccountValue("user"); }
static std::string AuthSavedToken()
{
    std::string t = AuthAccountValue("login");
    if(t.size() != 32) return "";
    for(char ch : t) if(!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return "";
    return t;
}
// 3.21.8: a random id for this phone (no account data in it). The bot remembers a /gen for it for
// 24 hours, so closing / restarting the game after verifying goes straight to Create your account.
static std::string AuthDeviceId()
{
    char p[512]; snprintf(p, sizeof(p), "%s/auth_device.txt", s_szDataDir);
    std::string id;
    if(FILE* f = fopen(p, "r"))
    {
        char line[96];
        while(fgets(line, sizeof(line), f))
            if(!strncmp(line, "dev=", 4)) { id = line + 4; while(!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back(); }
        fclose(f);
    }
    bool good = id.size() == 32;
    for(char ch : id) if(!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) good = false;
    if(good) return id;
    uint8_t r[16]; char h[33];
    if(!AuthCrypto::Random(r, sizeof(r))) return "";
    AuthCrypto::Hex(r, sizeof(r), h); id = h;
    if(FILE* f = fopen(p, "w")) { fprintf(f, "# MP login: random id of this phone (keeps a Discord /gen across a game restart)\ndev=%s\n", h); fclose(f); }
    return id;
}
static void AuthSaveUser(const std::string& u)
{
    char p[512]; AuthAccountPath(p, sizeof(p));
    const std::string& tok = s_Auth.Session();
    const bool keep = AUTH_AUTOLOGIN_AT_START && tok.size() == 32;
    if(FILE* f = fopen(p, "w"))
    {
        fprintf(f, "# MP login: the account on this phone (no password is kept). Keep this file when you update the mod:\n"
                   "# the game logs you in by itself. Delete it (or log out) to use another account.\nuser=%s\n", u.c_str());
        if(keep) fprintf(f, "login=%s\n", tok.c_str());
        fclose(f);
    }
    s_szAuthSavedTok = keep ? tok : "";
}

// left the server / lost it: the screen goes, the game session login stays (reconnect = no screen)
static void AuthReset()
{
    if(s_Auth.state != AuthClient::IDLE && s_Auth.state != AuthClient::DONE) s_Auth.Stop();
    else s_Auth.state = AuthClient::IDLE;
    s_bAuthReq = false; s_szAuthChal[0] = 0; s_bAuthTicket = false; s_nAuthDoneAt = 0;
    s_nAuthJoinAt = MP_NowMs();
    s_pAfEditing = nullptr;
    if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) VKeyboard::Close();
}

// PKT_AUTH from the server (network thread -> queued by the caller to the game frame)
static void AuthOnPacket(const uint8_t* d, int len)
{
    if(len < 2) return;
    if(d[1] == AUTH_CHALLENGE && len >= (int)sizeof(PktAuthChal))
    {
        PktAuthChal c; memcpy(&c, d, sizeof(c)); c.chal[32] = 0; c.server[31] = 0;
        if(s_bAuthReq && !strcmp(c.chal, s_szAuthChal)) return;   // the same one again (it is repeated)
        s_bAuthReq = true; s_nAuthStyle = c.style;
        MP_CopyStr(s_szAuthChal, c.chal, sizeof(s_szAuthChal));
        s_bAuthTicket = false; s_nAuthDoneAt = 0;
        const std::string last = AuthLastUser();
        if(s_Auth.device.empty()) s_Auth.device = AuthDeviceId();
        if(!s_Auth.LoggedIn())
        {   // a fresh game start: the login screen opens (3.22 auto login is off unless AUTH_AUTOLOGIN_AT_START)
            const std::string t = AuthSavedToken();
            if(!t.empty() && AUTH_AUTOLOGIN_AT_START) { s_Auth.RestoreSession(t); s_szAuthSavedTok = t; }
            else if(!t.empty() && !last.empty()) AuthSaveUser(last);   // drop the old login= token, keep the user name
        }
        s_Auth.Begin(c.chal, c.server, !last.empty(), last.c_str());
        if(!s_AfUser.buf[0] && !last.empty()) MP_CopyStr(s_AfUser.buf, last.c_str(), sizeof(s_AfUser.buf));
        SrvAuthPut(client.GetServerName(), client.GetPort(), true);   // a phone that takes it over keeps it on
    }
    else if(d[1] == AUTH_RESULT && len >= (int)sizeof(PktAuthResult))
    {
        PktAuthResult r; memcpy(&r, d, sizeof(r)); r.user[23] = 0; r.msg[95] = 0;
        if(!s_bAuthReq || s_Auth.state != AuthClient::WAIT_SERVER) return;
        s_bAuthTicket = false;
        s_Auth.ServerResult(r.ok != 0, r.msg);
        if(r.ok)
        {
            s_nAuthDoneAt = MP_NowMs();
            s_AuthUser = r.user[0] ? r.user : s_Auth.user;
            AuthSaveUser(s_AuthUser);
            s_AfPass.buf[0] = 0;
            if(s_Auth.createdNow) SysMsg(COL_GREEN, "Your account was created successfully. Welcome, %s.", s_AuthUser.c_str());
            else SysMsg(COL_GREEN, "Logged in as %s.", s_AuthUser.c_str());
        }
    }
}

// ---- 3.24: launcher profile (top left): your Discord profile picture + account name ----------
// MP/avatar.png + MP/profile.txt (user= / discord=) are written after a login: the bot sends the
// picture of the Discord account that made the /gen (64 px PNG). No account on this phone yet (never
// verified on Discord / never created one) = the blank default picture and "Not logged in".
static const char* kAvatarFile = "avatar.png";
static std::string s_ProfDiscord, s_ProfOwner;   // profile.txt: whose avatar.png it is
static bool        s_bProfRead = false;
static uint64_t    s_nAvatarAskAt = 0;
static void ProfilePath(char* out, size_t n) { snprintf(out, n, "%s/profile.txt", s_szDataDir); }
static void ProfileRead()
{
    if(s_bProfRead) return;
    s_bProfRead = true;
    char p[512]; ProfilePath(p, sizeof(p));
    if(FILE* f = fopen(p, "r"))
    {
        char line[160];
        while(fgets(line, sizeof(line), f))
        {
            std::string v = line; while(!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
            if(!v.compare(0, 5, "user=")) s_ProfOwner = v.substr(5);
            else if(!v.compare(0, 8, "discord=")) s_ProfDiscord = v.substr(8);
        }
        fclose(f);
    }
}
static void ProfileFrame(uint64_t now)
{
    ProfileRead();
    if(s_Auth.avatarReady)
    {
        s_Auth.avatarReady = false;
        char p[512]; snprintf(p, sizeof(p), "%s/%s", s_szDataDir, kAvatarFile);
        if(FILE* f = fopen(p, "wb")) { fwrite(s_Auth.avatarPng.data(), 1, s_Auth.avatarPng.size(), f); fclose(f); }
        s_ProfOwner = s_AuthUser.empty() ? s_Auth.user : s_AuthUser;
        if(!s_Auth.avatarDiscord.empty()) s_ProfDiscord = s_Auth.avatarDiscord;
        ProfilePath(p, sizeof(p));
        if(FILE* f = fopen(p, "w")) { fprintf(f, "# MP launcher profile (your Discord picture is avatar.png)\nuser=%s\ndiscord=%s\n", s_ProfOwner.c_str(), s_ProfDiscord.c_str()); fclose(f); }
        HUD::ReloadTexture(kAvatarFile);
        s_Auth.avatarPng.clear();
        logger->Info("Profile: Discord avatar saved for %s", s_ProfOwner.c_str());
    }
    // logged in during this game session: get the picture (again every 30 min: a changed Discord pfp)
    if(s_Auth.LoggedIn() && s_nAuthDoneAt && !s_Auth.avatarPending &&
       (!s_nAvatarAskAt || now - s_nAvatarAskAt > 30 * 60 * 1000ull) && now - s_nAuthDoneAt > 3000)
    { s_nAvatarAskAt = now; s_Auth.RequestAvatar(); }
}
// the account shown in the launcher ("" = none on this phone: blank picture)
static std::string ProfileUser()
{
    if(!s_AuthUser.empty()) return s_AuthUser;
    return AuthLastUser();
}
// draws the round picture: Discord avatar if this account has one, else the blank default
static void ProfileDrawAvatar(ImDrawList* dl, ImVec2 c, float r)
{
    ProfileRead();
    const std::string u = ProfileUser();
    ImVec2 uv1;
    void* tex = (!u.empty() && !strcasecmp(u.c_str(), s_ProfOwner.c_str())) ? HUD::Texture(kAvatarFile, &uv1) : nullptr;
    if(tex)
    {
        dl->AddImageRounded((ImTextureID)tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0), uv1, IM_COL32_WHITE, r);
        dl->AddCircle(c, r, IM_COL32(255, 255, 255, 90), 40, UI::S(2));
        return;
    }
    // blank default: grey disc + a plain head-and-shoulders silhouette
    dl->AddCircleFilled(c, r, IM_COL32(70, 72, 78, 255), 40);
    dl->PushClipRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), true);
    dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.18f), r * 0.36f, IM_COL32(150, 153, 160, 255), 28);
    dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.95f), r * 0.68f, IM_COL32(150, 153, 160, 255), 32);
    dl->PopClipRect();
    dl->AddCircle(c, r, IM_COL32(255, 255, 255, 60), 40, UI::S(2));
}

// every game frame
static void AuthFrame(uint64_t now)
{
    if(client.GetState() == NET_DISCONNECTED) { if(s_bAuthReq) AuthReset(); }
    if(s_Auth.state != AuthClient::IDLE || s_Auth.statsPending || s_Auth.avatarPending) s_Auth.Tick(now);
    ProfileFrame(now);   // 3.24
    if(!s_szAuthSavedTok.empty() && !s_Auth.LoggedIn())
    {   // 3.22: the bot said the saved login ended (180 days unused / account gone): forget it, keep the name
        s_szAuthSavedTok.clear();
        const std::string last = AuthLastUser();
        if(!last.empty()) AuthSaveUser(last);
    }
    if(!s_bAuthReq) return;
    std::string p; uint8_t sig[64];
    if(s_Auth.TakeTicket(p, sig)) { s_AuthTicket = p; memcpy(s_AuthSig, sig, 64); s_bAuthTicket = true; s_nAuthTicketAt = 0; }
    if(s_bAuthTicket && s_Auth.state == AuthClient::WAIT_SERVER && now - s_nAuthTicketAt > 1500 && client.GetState() == NET_CONNECTED)
    {   // UDP: again until the server answers
        s_nAuthTicketAt = now;
        uint8_t buf[sizeof(PktAuthTicket) + MP_AUTH_TICKET_MAX];
        PktAuthTicket h; h.type = PKT_AUTH; h.op = AUTH_TICKET; h.plen = (uint16_t)std::min<size_t>(s_AuthTicket.size(), MP_AUTH_TICKET_MAX);
        memcpy(h.sig, s_AuthSig, 64);
        memcpy(buf, &h, sizeof(h)); memcpy(buf + sizeof(h), s_AuthTicket.data(), h.plen);
        client.Send(buf, (int)(sizeof(h) + h.plen));
    }
}

// the < Spawn > buttons (and the Brief Case War teams) wait for the login
static bool AuthHoldsJoin(uint64_t now)
{
    if(!s_bAuthReq)
    {   // the challenge comes right after the welcome: a short wait on servers known to use it
        const char* sn = client.GetServerName();
        const bool expected = MP::IsDefaultServer(sn, client.GetPort()) || MP_IsBcwServerName(sn) || SrvAuthGet(sn, client.GetPort()) > 0;
        return expected && now - s_nAuthJoinAt < 3500;
    }
    if(s_Auth.state != AuthClient::DONE) return true;
    return s_Auth.createdNow && now - s_nAuthDoneAt < 2600;   // "Account created successfully" first
}
// 3.21.8: the login pop-up waits for "Connected successfully!" (the Connecting.... / resources feed done
// and the black join screen slid away); the login itself already runs in the background
// 3.22.1: on the default server the 100% progress save loads ~4 s after the join; the login pop-up waits
// for that load (it used to open first and the reload threw it away). Capped at 30 s so it never hangs.
static bool AuthJoinSettled()
{
    if(s_nFeedStep >= 0 || s_nFeedRes >= 0) return false;
    if(Progress::Pending() && MP_NowMs() - s_nAuthJoinAt < 30000) return false;
    return s_nJoinPhase == JP_NONE || s_nJoinPhase >= JP_WAIT;
}
static bool AuthScreenUp()
{
    if(!s_bAuthReq) return false;
    if(!AuthJoinSettled()) return false;
    if(s_Auth.state == AuthClient::IDLE) return false;
    if(s_Auth.state == AuthClient::DONE) return s_Auth.createdNow && MP_NowMs() - s_nAuthDoneAt < 2600;
    return true;
}
bool MP::AuthScreenShown() { return AuthScreenUp(); }

// ---- drawing ------------------------------------------------------------------------------------
struct AuthLook
{
    ImU32 panel, header, accent, accentHeld, text, dim, field, fieldBorder, btn, btnHeld, btnText, err, ok;
};
static AuthLook AuthStyleLook(bool bcw, bool mw3 = false)
{
    AuthLook l;
    if(mw3)
    {   // 3.23: Modern Warfare 3 - the same layout as Brief Case War, in green
        l.panel = IM_COL32(6, 10, 7, 236); l.header = IM_COL32(18, 110, 40, 245); l.accent = IM_COL32(60, 255, 106, 255);
        l.accentHeld = IM_COL32(170, 255, 190, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(190, 215, 195, 255);
        l.field = IM_COL32(255, 255, 255, 22); l.fieldBorder = IM_COL32(60, 255, 106, 200); l.btn = IM_COL32(30, 150, 58, 240);
        l.btnHeld = IM_COL32(255, 255, 255, 240); l.btnText = IM_COL32(255, 255, 255, 255);
    }
    else if(bcw)
    {
        l.panel = IM_COL32(8, 8, 10, 236); l.header = IM_COL32(150, 12, 18, 245); l.accent = IM_COL32(230, 30, 40, 255);
        l.accentHeld = IM_COL32(255, 80, 80, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(205, 190, 190, 255);
        l.field = IM_COL32(255, 255, 255, 22); l.fieldBorder = IM_COL32(230, 30, 40, 200); l.btn = IM_COL32(200, 20, 30, 240);
        l.btnHeld = IM_COL32(255, 255, 255, 240); l.btnText = IM_COL32(255, 255, 255, 255);
    }
    else
    {
        l.panel = IM_COL32(0, 0, 0, 215); l.header = IM_COL32(25, 25, 30, 240); l.accent = IM_COL32(169, 196, 228, 255);
        l.accentHeld = IM_COL32(220, 235, 255, 255); l.text = IM_COL32(255, 255, 255, 255); l.dim = IM_COL32(169, 196, 228, 255);
        l.field = IM_COL32(255, 255, 255, 30); l.fieldBorder = IM_COL32(169, 196, 228, 160); l.btn = IM_COL32(70, 70, 78, 230);
        l.btnHeld = IM_COL32(169, 196, 228, 140); l.btnText = IM_COL32(255, 255, 255, 255);
    }
    l.err = IM_COL32(255, 90, 90, 255); l.ok = IM_COL32(80, 220, 110, 255);
    return l;
}

// Brief Case War: 3.22.1 no red background at all - the same plain dim layer as the normal login screen
// (the red VFX, the red corner tint and the glowing red frame around the panel are gone).
static void AuthBcwBackdrop(ImDrawList* dl, ImVec2 ds, float /*t*/)
{
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 120));
}

static void OnAuthFieldText(const char* text, void* data)
{
    AuthField* f = (AuthField*)data;
    if(f) MP_CopyStr(f->buf, text ? text : "", sizeof(f->buf));
    s_pAfEditing = nullptr;
}

struct AuthPanel
{
    ImDrawList* dl; ImFont* f; AuthLook L; ImVec2 p0; float w, pad, fs, y; int ids;
    void Text(const char* t, ImU32 c, float size = 0, bool centre = false)
    {
        float s = size > 0 ? size : fs;
        ImVec2 ts = f->CalcTextSizeA(s, FLT_MAX, w - pad * 2, t);
        float x = centre ? p0.x + (w - ts.x) * 0.5f : p0.x + pad;
        dl->AddText(f, s, ImVec2(x, y), c, t, nullptr, w - pad * 2);
        y += ts.y + UI::S(8);
    }
    bool Button(const char* lab, float bx, float bw, float bh, bool primary)
    {
        ImVec2 a(bx, y), b(bx + bw, y + bh);
        ImGui::SetCursorScreenPos(a); ImGui::PushID(ids++);
        bool hit = ImGui::InvisibleButton("##ab", ImVec2(bw, bh)); bool held = ImGui::IsItemActive();
        ImGui::PopID();
        dl->AddRectFilled(a, b, held ? L.btnHeld : primary ? L.btn : IM_COL32(60, 60, 66, 220), UI::S(5));
        if(primary) dl->AddRect(a, b, L.accent, UI::S(5), 0, UI::S(2));
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, lab);
        dl->AddText(f, fs, ImVec2(a.x + (bw - ts.x) * 0.5f, a.y + (bh - ts.y) * 0.5f), held && primary ? IM_COL32(0, 0, 0, 255) : L.btnText, lab);
        return hit;
    }
    void Field(AuthField& fd)
    {
        const float rowH = UI::S(62), lw = UI::S(170);
        ImVec2 a(p0.x + pad + lw, y), b(p0.x + w - pad, y + rowH);
        dl->AddText(f, fs, ImVec2(p0.x + pad, y + (rowH - fs) * 0.5f), L.dim, fd.title);
        ImGui::SetCursorScreenPos(a); ImGui::PushID(ids++);
        if(ImGui::InvisibleButton("##af", ImVec2(b.x - a.x, rowH)))
        {
            s_pAfEditing = &fd;
            VKeyboard::Open(fd.title, fd.buf, fd.max, OnAuthFieldText, &fd, fd.numeric);
        }
        ImGui::PopID();
        const bool editing = s_pAfEditing == &fd && VKeyboard::IsOpen() && !VKeyboard::IsChat();
        dl->AddRectFilled(a, b, L.field, UI::S(5));
        dl->AddRect(a, b, editing ? L.accentHeld : L.fieldBorder, UI::S(5), 0, editing ? UI::S(3) : UI::S(1.5f));
        std::string shown = editing ? VKeyboard::GetText() : fd.buf;
        if(fd.secret) shown.assign(shown.size(), '*');
        if(shown.empty() && !editing) dl->AddText(f, fs, ImVec2(a.x + UI::S(14), a.y + (rowH - fs) * 0.5f), IM_COL32(255, 255, 255, 90), "tap to type");
        else dl->AddText(f, fs, ImVec2(a.x + UI::S(14), a.y + (rowH - fs) * 0.5f), L.text, shown.c_str());
        y += rowH + UI::S(12);
    }
};

static void AuthTakeField(AuthField& fd)   // a button pressed while the keyboard is still open
{
    if(s_pAfEditing == &fd && VKeyboard::IsOpen() && !VKeyboard::IsChat()) { MP_CopyStr(fd.buf, VKeyboard::GetText(), sizeof(fd.buf)); VKeyboard::Close(); s_pAfEditing = nullptr; }
}

static void RenderAuth()
{
    if(!AuthScreenUp() || !Game::IsReady()) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const bool autoLogin = s_Auth.LoggedIn() && (s_Auth.state == AuthClient::LINKING || s_Auth.state == AuthClient::TICKET ||
                           (s_Auth.state == AuthClient::WAIT_SERVER && !s_Auth.createdNow));
    if((s_Auth.state == AuthClient::LINKING || autoLogin) && s_Auth.err.empty())
    {   // 3.21.7: the login screen opens only once the login server is connected; until then a small line
        // 3.22: a saved login (account.txt) never opens the pop-up at all: "Logging in as NAME..."
        static const char* dots[4] = { "", ".", "..", "..." };
        std::string who = s_Auth.prefillUser; if(who.empty()) who = "your account";
        char base[96];
        if(autoLogin) snprintf(base, sizeof(base), "Logging in as %s", who.c_str());
        else snprintf(base, sizeof(base), "Connecting to the login server");
        char b[112]; snprintf(b, sizeof(b), "%s%s", base, dots[(MP_NowMs() / 400) % 4]);
        char full[112]; snprintf(full, sizeof(full), "%s...", base);
        ImDrawList* fg = ImGui::GetForegroundDrawList(); ImFont* f = UI::Font(); const float fs = UI::S(28);
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, full);
        ImVec2 a0((ds.x - ts.x) * 0.5f - UI::S(18), UI::S(14)), a1((ds.x + ts.x) * 0.5f + UI::S(18), UI::S(14) + ts.y + UI::S(16));
        fg->AddRectFilled(a0, a1, IM_COL32(0, 0, 0, 170), UI::S(8));
        fg->AddText(f, fs, ImVec2(a0.x + UI::S(18), a0.y + UI::S(8)), IM_COL32(255, 255, 255, 230), b);
        return;
    }
    const bool mw3 = s_nAuthStyle == AUTH_STYLE_MW3 || MP::IsOnMw3();   // 3.23
    const bool bcw = s_nAuthStyle == AUTH_STYLE_BCW || mw3;
    const float t = (float)(MP_NowMs() % 3600000ull) / 1000.0f;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if(bcw) AuthBcwBackdrop(bg, ds, t);
    else bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 120));

    AuthPanel P; P.f = UI::Font(); P.L = AuthStyleLook(bcw, mw3); P.pad = UI::S(28); P.fs = UI::S(30); P.ids = 0;
    P.w = fminf(fmaxf(UI::S(760), ds.x * 0.46f), ds.x * 0.94f);
    const AuthClient::State st = s_Auth.state;
    float h = UI::S(st == AuthClient::SIGNUP ? 700 : st == AuthClient::LOGIN ? 560 : st == AuthClient::CODE ? 640 : 380);
    if(bcw) h += UI::S(90);
    h = fminf(h, ds.y * 0.96f);
    P.p0 = ImVec2((ds.x - P.w) * 0.5f, (ds.y - h) * 0.5f);
    if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) P.p0.y = fmaxf(UI::S(6), fminf(P.p0.y, ds.y - VKeyboard::Height() - h - UI::S(6)));

    ImGui::SetNextWindowPos(P.p0); ImGui::SetNextWindowSize(ImVec2(P.w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##MPLogin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        P.dl = ImGui::GetWindowDrawList();
        ImDrawList* dl = P.dl; const AuthLook& L = P.L;
        const ImVec2 p1(P.p0.x + P.w, P.p0.y + h);
        dl->AddRectFilled(P.p0, p1, L.panel, UI::S(8));
        const float capH = UI::S(64);
        dl->AddRectFilled(P.p0, ImVec2(p1.x, P.p0.y + capH), L.header, UI::S(8), ImDrawFlags_RoundCornersTop);
        if(bcw)
        {   // sweeping shine over the header
            float sx = P.p0.x + fmodf(t * 0.5f, 1.0f) * (P.w + UI::S(200)) - UI::S(100);
            dl->PushClipRect(P.p0, ImVec2(p1.x, P.p0.y + capH), true);
            dl->AddQuadFilled(ImVec2(sx, P.p0.y), ImVec2(sx + UI::S(60), P.p0.y), ImVec2(sx + UI::S(20), P.p0.y + capH), ImVec2(sx - UI::S(40), P.p0.y + capH), IM_COL32(255, 255, 255, 60));
            dl->PopClipRect();
        }
        const char* title = st == AuthClient::SIGNUP ? "Create your account" : st == AuthClient::LOGIN ? "Login" :
                            st == AuthClient::CODE ? "Code authentication" : st == AuthClient::DONE ? "Welcome" : "Login";
        char cap[96]; snprintf(cap, sizeof(cap), "%s%s", mw3 ? "MODERN WARFARE 3  |  " : (bcw ? "BRIEF CASE WAR  |  " : "MP  |  "), title);
        dl->AddText(P.f, UI::S(32), ImVec2(P.p0.x + P.pad, P.p0.y + (capH - UI::S(32)) * 0.5f), IM_COL32_WHITE, cap);
        P.y = P.p0.y + capH + UI::S(18);
        if(bcw)
        {
            ImVec2 uv(1, 1); const float is = UI::S(84);
            float bob = sinf(t * 2.2f) * UI::S(4);
            ImVec2 c(P.p0.x + P.w * 0.5f, P.y + is * 0.5f + bob);
            dl->AddCircleFilled(c, is * 0.62f, mw3 ? IM_COL32(60, 255, 106, (int)(70 + 50 * sinf(t * 3.0f))) : IM_COL32(230, 30, 40, (int)(70 + 50 * sinf(t * 3.0f))), 40);
            if(void* tex = HUD::Texture(mw3 ? "mw3_guns.png" : "bcw_briefcase.png", &uv))
                dl->AddImage((ImTextureID)tex, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f), ImVec2(c.x + is * 0.5f, c.y + is * 0.5f), ImVec2(0, 0), uv);
            else
            {   // no texture: a drawn briefcase
                dl->AddRectFilled(ImVec2(c.x - is * 0.45f, c.y - is * 0.25f), ImVec2(c.x + is * 0.45f, c.y + is * 0.35f), IM_COL32(20, 20, 20, 255), UI::S(6));
                dl->AddRect(ImVec2(c.x - is * 0.15f, c.y - is * 0.40f), ImVec2(c.x + is * 0.15f, c.y - is * 0.25f), IM_COL32(230, 30, 40, 255), UI::S(3), 0, UI::S(4));
                dl->AddRect(ImVec2(c.x - is * 0.45f, c.y - is * 0.25f), ImVec2(c.x + is * 0.45f, c.y + is * 0.35f), IM_COL32(230, 30, 40, 255), UI::S(6), 0, UI::S(3));
            }
            P.y += is + UI::S(10);
        }

        const float bh = UI::S(70), gap = UI::S(20);
        const float fullW = P.w - P.pad * 2, halfW = (fullW - gap) * 0.5f;
        switch(st)
        {
        case AuthClient::LINKING: case AuthClient::BUSY: case AuthClient::TICKET: case AuthClient::WAIT_SERVER: case AuthClient::IDLE:
        {
            static const char* dots[4] = { "", ".", "..", "..." };
            char b[96]; snprintf(b, sizeof(b), "%s%s", st == AuthClient::LINKING ? "Connecting to the login server" : "Logging in", dots[(MP_NowMs() / 400) % 4]);
            P.y += UI::S(30); P.Text(b, L.text, UI::S(34), true);
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, 0, true);
            break;
        }
        case AuthClient::CODE:
        {
            P.Text("This server uses accounts. Verify with Discord first:", L.dim);
            P.Text("1.  Join the Discord server:", L.text);
            P.Text(MP_AUTH_DISCORD_URL, L.accent, UI::S(32), true);
            P.Text("2.  In the #verification channel type:", L.text);
            char cmd[48]; snprintf(cmd, sizeof(cmd), "/gen %s", s_Auth.code[0] ? s_Auth.code : "........");
            {   // the code, big, in a box
                const float cs = UI::S(56); ImVec2 ts = P.f->CalcTextSizeA(cs, FLT_MAX, 0, cmd);
                ImVec2 a(P.p0.x + (P.w - ts.x) * 0.5f - UI::S(24), P.y), b(P.p0.x + (P.w + ts.x) * 0.5f + UI::S(24), P.y + ts.y + UI::S(20));
                dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, bcw ? 18 : 26), UI::S(6));
                dl->AddRect(a, b, L.accent, UI::S(6), 0, UI::S(2.5f));
                dl->AddText(P.f, cs, ImVec2(a.x + UI::S(24), a.y + UI::S(10)), IM_COL32_WHITE, cmd);
                P.y = b.y + UI::S(10);
            }
            int left = s_Auth.codeAt ? 600 - (int)((MP_NowMs() - s_Auth.codeAt) / 1000) : 600; if(left < 0) left = 0;
            char w[96]; snprintf(w, sizeof(w), "Waiting for /gen...  (code works %d:%02d more)", left / 60, left % 60);
            P.Text(w, L.dim, UI::S(26), true);
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, UI::S(26), true);
            P.y = P.p0.y + h - P.pad - bh * 2 - gap;
            if(P.Button("Open Discord", P.p0.x + P.pad, halfW, bh, true)) Voice::OpenUrl(MP_AUTH_DISCORD_URL);
            if(P.Button("New code", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) s_Auth.NewCode();
            P.y += bh + gap;
            if(P.Button("I have an account: Login", P.p0.x + P.pad, halfW, bh, false)) s_Auth.WantLogin();
            if(P.Button("Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); LeaveServer("Left the server."); return; }
            break;
        }
        case AuthClient::SIGNUP:
        {
            char v[128]; snprintf(v, sizeof(v), "Discord verified: %s", s_Auth.discordName.empty() ? "ok" : s_Auth.discordName.c_str());
            P.Text(v, L.ok, UI::S(26));
            P.Field(s_AfUser); P.Field(s_AfPass);
            {   // gender: 3 buttons
                const float rowH = UI::S(62), lw = UI::S(170);
                P.dl->AddText(P.f, P.fs, ImVec2(P.p0.x + P.pad, P.y + (rowH - P.fs) * 0.5f), L.dim, "Gender");
                float gx = P.p0.x + P.pad + lw, gw = (P.w - P.pad * 2 - lw - UI::S(12) * 2) / 3;
                for(int i = 0; i < 3; ++i)
                {
                    ImVec2 a(gx + i * (gw + UI::S(12)), P.y), b(a.x + gw, P.y + rowH);
                    ImGui::SetCursorScreenPos(a); ImGui::PushID(500 + i);
                    if(ImGui::InvisibleButton("##g", ImVec2(gw, rowH))) s_nAfGender = i;
                    ImGui::PopID();
                    const bool on = s_nAfGender == i;
                    dl->AddRectFilled(a, b, on ? L.accent : L.field, UI::S(5));
                    dl->AddRect(a, b, L.fieldBorder, UI::S(5));
                    ImVec2 ts = P.f->CalcTextSizeA(P.fs, FLT_MAX, 0, kAuthGenders[i]);
                    dl->AddText(P.f, P.fs, ImVec2(a.x + (gw - ts.x) * 0.5f, a.y + (rowH - ts.y) * 0.5f), on ? IM_COL32(0, 0, 0, 255) : L.text, kAuthGenders[i]);
                }
                P.y += rowH + UI::S(12);
            }
            P.Field(s_AfAge);
            P.Text("User 3-20 letters / numbers / _   Password 6-32   Age 10-24", L.dim, UI::S(24));
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, UI::S(26));
            P.y = P.p0.y + h - P.pad - bh;
            if(P.Button("Create", P.p0.x + P.pad, halfW, bh, true))
            {
                AuthTakeField(s_AfUser); AuthTakeField(s_AfPass); AuthTakeField(s_AfAge);
                const int age = atoi(s_AfAge.buf);
                if(age < AUTH_AGE_MIN || age > AUTH_AGE_MAX) s_Auth.err = "Age must be 10-24.";
                else s_Auth.Signup(s_AfUser.buf, s_AfPass.buf, kAuthGenders[s_nAfGender], age);
            }
            if(P.Button("Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); LeaveServer("Left the server."); return; }
            break;
        }
        case AuthClient::LOGIN:
        {
            P.Text("Welcome back. Log in to play.", L.dim);
            if(!s_Auth.info.empty()) P.Text(s_Auth.info.c_str(), L.ok, UI::S(26));
            if(!s_Auth.prefillUser.empty() && !s_AfUser.buf[0]) MP_CopyStr(s_AfUser.buf, s_Auth.prefillUser.c_str(), sizeof(s_AfUser.buf));
            P.Field(s_AfUser); P.Field(s_AfPass);
            if(!s_Auth.err.empty()) P.Text(s_Auth.err.c_str(), L.err, UI::S(26));
            P.y = P.p0.y + h - P.pad - bh * 2 - gap;
            if(P.Button("Login", P.p0.x + P.pad, fullW, bh, true))
            {
                AuthTakeField(s_AfUser); AuthTakeField(s_AfPass);
                s_Auth.Login(s_AfUser.buf, s_AfPass.buf);
            }
            P.y += bh + gap;
            if(P.Button("New account (Discord code)", P.p0.x + P.pad, halfW, bh, false)) s_Auth.WantNewAccount();
            if(P.Button("Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); LeaveServer("Left the server."); return; }
            break;
        }
        case AuthClient::DONE:
        {
            P.y += UI::S(40);
            P.Text("Your account was created successfully", L.ok, UI::S(38), true);
            char u[64]; snprintf(u, sizeof(u), "Welcome, %s", s_AuthUser.c_str());
            P.Text(u, L.text, UI::S(32), true);
            break;
        }
        case AuthClient::FAILED:
        {
            P.y += UI::S(20);
            P.Text(s_Auth.err.empty() ? "Login failed." : s_Auth.err.c_str(), L.err, UI::S(30), true);
            P.y = P.p0.y + h - P.pad - bh;
            if(P.Button("Try again", P.p0.x + P.pad, halfW, bh, true))
            { const std::string last = AuthLastUser(); s_Auth.Begin(s_szAuthChal, client.GetServerName(), !last.empty(), last.c_str()); }
            if(P.Button("Leave server", P.p0.x + P.pad + halfW + gap, halfW, bh, false)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); LeaveServer("Left the server."); return; }
            break;
        }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ---- /stats  /char ----------------------------------------------------------------------------
static bool AuthCommand(const char* cmd)
{
    if(strcmp(cmd, "stats") && strcmp(cmd, "char")) return false;
    if(!s_Auth.LoggedIn())
    {
        SysMsg(COL_YELLOW, s_bAuthReq ? "Log in first." : "No account here: this server has no login system (the default server and Brief Case War have it).");
        return true;
    }
    s_Auth.RequestStats();
    s_bStatsOpen = true; s_nStatsAt = MP_NowMs();
    return true;
}

static void RenderStats()
{
    if(!s_bStatsOpen) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const bool mw3 = MP::IsOnMw3();   // 3.23
    const bool bcw = MP::IsOnBcw() || mw3;
    AuthPanel P; P.f = UI::Font(); P.L = AuthStyleLook(bcw, mw3); P.pad = UI::S(26); P.fs = UI::S(28); P.ids = 900;
    P.w = fminf(UI::S(700), ds.x * 0.9f);
    const float h = UI::S(600);
    P.p0 = ImVec2((ds.x - P.w) * 0.5f, (ds.y - h) * 0.5f);
    ImGui::SetNextWindowPos(P.p0); ImGui::SetNextWindowSize(ImVec2(P.w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    bool close = false;
    if(ImGui::Begin("##MPStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        P.dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(P.p0.x + P.w, P.p0.y + h);
        P.dl->AddRectFilled(P.p0, p1, P.L.panel, UI::S(8));
        P.dl->AddRectFilled(P.p0, ImVec2(p1.x, P.p0.y + UI::S(60)), P.L.header, UI::S(8), ImDrawFlags_RoundCornersTop);
        P.dl->AddText(P.f, UI::S(30), ImVec2(P.p0.x + P.pad, P.p0.y + UI::S(15)), IM_COL32_WHITE, "My account  (only you see this)");
        P.y = P.p0.y + UI::S(80);
        auto& S = s_Auth.stats;
        auto row = [&](const char* k, const std::string& v)
        {
            P.dl->AddText(P.f, P.fs, ImVec2(P.p0.x + P.pad, P.y), P.L.dim, k);
            P.dl->AddText(P.f, P.fs, ImVec2(P.p0.x + P.pad + UI::S(230), P.y), P.L.text, v.c_str());
            P.y += P.fs + UI::S(14);
        };
        auto when = [](const std::string& unix) -> std::string
        {
            time_t tt = (time_t)atoll(unix.c_str()); if(tt <= 0) return "-";
            struct tm tmv; localtime_r(&tt, &tmv); char b[40]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M", &tmv); return b;
        };
        if(s_Auth.statsPending && S.empty()) P.Text("Loading...", P.L.text, 0, true);
        else if(S["ok"] != "1") P.Text(S.count("err") ? S["err"].c_str() : "Could not load your account.", P.L.err, 0, true);
        else
        {
            row("User", S["user"]);
            row("Discord", S["discord"]);
            row("Gender", S["gender"]);
            row("Age", S["age"]);
            row("Created", when(S["created"]));
            row("Logins", S["logins"]);
            row("Last login", when(S["last"]));
            char sc[32]; snprintf(sc, sizeof(sc), "%d", s_nMyScore); row("Score here", sc);
            row("Server", client.GetServerName());
        }
        P.y = P.p0.y + h - P.pad - UI::S(66);
        if(P.Button("Close", P.p0.x + P.pad, P.w - P.pad * 2, UI::S(66), true)) close = true;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if(close || client.GetState() == NET_DISCONNECTED) s_bStatsOpen = false;
}
