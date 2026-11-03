// =============================================================================================
// 3.63 MP SOCIAL in the launcher (Valorant style): Friends (search by ID / player name, add +, requests with
// accept / decline, online / offline), Chats (private chats with friends + group chats), Party (up to 5, party chat),
// Communities (create, join, follow, posts - like Facebook groups / pages) and the Inbox (bell in the title bar).
// The network part is src/net/social.cpp.
// =============================================================================================
#include "net/social.h"
#include <set>

static char        s_szSocSearch[48] = "";
static std::string s_SocConv;            // open chat (conv key)
static std::string s_SocCom;             // open community
static bool        s_bSocNewGroup = false, s_bSocAddMember = false, s_bSocNewCom = false;
static char        s_szSocGroupName[41] = "";
static std::set<std::string> s_SocPick;  // friends picked for a new group
static char        s_szSocComName[41] = "", s_szSocComDesc[301] = "";
static std::string s_SocArm;             // "remove friend" / "delete" needs a 2nd tap (id of what is armed)
static uint64_t    s_nSocArmAt = 0;
static int         s_nSocPageFrame = -100;
static uint64_t    s_nSocInboxSeenAt = 0;
static int         s_nSocLastUnread = 0;
static std::string s_SocMsgKey, s_SocPostCid;
static bool        s_bSocInit = false;

static void OnSocSearch(const char* t, void*) { MP_CopyStr(s_szSocSearch, t ? t : "", sizeof(s_szSocSearch)); Social::Search(s_szSocSearch); }
static void OnSocMsg(const char* t, void*) { if(t && *t && !s_SocMsgKey.empty()) Social::SendText(s_SocMsgKey, t); }
static void OnSocPost(const char* t, void*) { if(t && *t && !s_SocPostCid.empty()) Social::PostCommunity(s_SocPostCid, t); }
static void OnSocGroupName(const char* t, void*) { MP_CopyStr(s_szSocGroupName, t ? t : "", sizeof(s_szSocGroupName)); }
static void OnSocComName(const char* t, void*) { MP_CopyStr(s_szSocComName, t ? t : "", sizeof(s_szSocComName)); }
static void OnSocComDesc(const char* t, void*) { MP_CopyStr(s_szSocComDesc, t ? t : "", sizeof(s_szSocComDesc)); }

// every game frame (mp_cmds.inc): network, your name, chat line for new notifications
static void SocialFrame()
{
    if(!s_bSocInit) { s_bSocInit = true; Social::Init(MP::DataDir()); }
    Social::SetName(settings.name[0] ? settings.name : ProfileUser().c_str());
    Social::Tick();
    if(ImGui::GetFrameCount() - s_nSocPageFrame > 3) Social::ViewCommunity("");
    int un = Social::UnreadInbox();
    if(un > s_nSocLastUnread)
    {
        auto inbox = Social::Inbox();
        if(!inbox.empty() && !inbox.front()->read) SysMsg(COL_SYSTEM, "[Social] %s  (MP Launcher > Inbox)", inbox.front()->text.c_str());
    }
    s_nSocLastUnread = un;
    if(!s_SocArm.empty() && MP_NowMs() - s_nSocArmAt > 4000) s_SocArm.clear();
}

// ---- small drawing helpers ----
static ImU32 SocColor(const std::string& id)
{
    static const ImU32 k[] = { IM_COL32(255, 70, 85, 255), IM_COL32(64, 230, 190, 255), IM_COL32(245, 185, 66, 255), IM_COL32(120, 150, 255, 255),
                               IM_COL32(200, 110, 255, 255), IM_COL32(90, 200, 250, 255), IM_COL32(255, 140, 90, 255), IM_COL32(140, 220, 110, 255) };
    uint32_t h = 2166136261u; for(char c : id) h = (h ^ (uint8_t)c) * 16777619u;
    return k[h % 8];
}
// chamfered square picture with the first letter (online = teal dot, offline = grey dot, -1 = no dot)
static void SocAvatar(ImDrawList* dl, ImVec2 c, float r, const std::string& name, const std::string& id, int online)
{
    ImU32 col = SocColor(id);
    VChamfer(dl, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), r * 0.35f, VAlpha(col, 0.22f), VAlpha(col, 0.9f), UI::S(1.5f));
    char l[8] = "?";
    if(!name.empty()) { unsigned char ch = (unsigned char)name[0]; if(ch < 0x80) { l[0] = (char)toupper(ch); l[1] = 0; } else { size_t n = 1; while(n < name.size() && n < 4 && ((unsigned char)name[n] & 0xC0) == 0x80) ++n; memcpy(l, name.data(), n); l[n] = 0; } }
    float fs = r * 1.1f; ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, l);
    dl->AddText(UI::Font(), fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), kVText, l);
    if(online >= 0)
    {
        ImVec2 d(c.x + r * 0.82f, c.y + r * 0.82f);
        dl->AddCircleFilled(d, UI::S(7), online ? kVTeal : IM_COL32(110, 118, 126, 255), 16);
        dl->AddCircle(d, UI::S(7), kVDark, 16, UI::S(2.5f));
    }
}
static void SocBadge(ImDrawList* dl, ImVec2 c, int n, ImU32 col = 0)
{
    if(n <= 0) return;
    char b[8]; snprintf(b, sizeof(b), n > 99 ? "99+" : "%d", n);
    float fs = ImGui::GetFontSize() * 0.68f; ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, b);
    float r = ImMax(UI::S(13), ts.x * 0.5f + UI::S(7));
    dl->AddRectFilled(ImVec2(c.x - r, c.y - UI::S(13)), ImVec2(c.x + r, c.y + UI::S(13)), col ? col : kVRed, UI::S(13));
    dl->AddText(UI::Font(), fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, b);
}
static void SocText(ImDrawList* dl, ImVec2 p, ImU32 col, const char* t, float scale = 1.0f, float wrap = 0)
{
    dl->AddText(UI::Font(), ImGui::GetFontSize() * scale, p, col, t, nullptr, wrap);
}
static float SocTextW(const char* t, float scale = 1.0f) { return UI::Font()->CalcTextSizeA(ImGui::GetFontSize() * scale, FLT_MAX, 0, t).x; }
static const char* SocAgo(int64_t ts, char* b, size_t n)
{
    int64_t d = (int64_t)time(nullptr) - ts; if(d < 0) d = 0;
    if(d < 60) snprintf(b, n, "now");
    else if(d < 3600) snprintf(b, n, "%dm", (int)(d / 60));
    else if(d < 86400) snprintf(b, n, "%dh", (int)(d / 3600));
    else snprintf(b, n, "%dd", (int)(d / 86400));
    return b;
}
// Valorant button: 0 = outline, 1 = teal, 2 = red, 3 = red outline, 4 = chip (not a button)
static bool VBtn(const char* id, const char* label, float w, float h, int style, bool enabled = true, int icon = -1)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + w, p.y + h);
    bool hit = false;
    if(style != 4) { if(!enabled) ImGui::BeginDisabled(); hit = ImGui::InvisibleButton("##vb", ImVec2(w, h)); if(!enabled) ImGui::EndDisabled(); }
    else ImGui::Dummy(ImVec2(w, h));
    bool held = enabled && style != 4 && ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill = 0, border = 0, tc = kVText;
    switch(style)
    {
    case 1: fill = held ? VAlpha(kVTeal, 0.75f) : kVTeal; tc = kVDark; break;
    case 2: fill = held ? VAlpha(kVRed, 0.75f) : kVRed; tc = IM_COL32_WHITE; break;
    case 3: fill = held ? kVRedDim : IM_COL32(255, 70, 85, 18); border = VAlpha(kVRed, 0.8f); tc = IM_COL32(255, 130, 140, 255); break;
    case 4: fill = IM_COL32(236, 232, 225, 14); border = IM_COL32(236, 232, 225, 40); tc = kVDim; break;
    default: fill = held ? IM_COL32(236, 232, 225, 40) : IM_COL32(236, 232, 225, 10); border = IM_COL32(236, 232, 225, 70); break;
    }
    if(!enabled) { fill = VAlpha(fill, 0.35f); border = VAlpha(border, 0.35f); tc = VAlpha(tc, 0.45f); }
    VChamfer(dl, p, q, ImMin(UI::S(10), h * 0.3f), fill, border, UI::S(1.5f));
    float iw = icon >= 0 ? h * 0.42f : 0, tw = label && *label ? SocTextW(label, 0.9f) : 0;
    float gap = (iw > 0 && tw > 0) ? UI::S(10) : 0, x = p.x + (w - iw - gap - tw) * 0.5f;
    if(icon >= 0) LIcon(dl, icon, ImVec2(x + iw * 0.5f, p.y + h * 0.5f), iw * 0.75f, tc);
    if(tw > 0) SocText(dl, ImVec2(x + iw + gap, p.y + (h - ImGui::GetFontSize() * 0.9f) * 0.5f), tc, label, 0.9f);
    ImGui::PopID();
    if(hit && enabled) MpUiClick();
    return hit && enabled;
}
static void SocTitle(const char* t, const char* sub)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float W = ImGui::GetContentRegionAvail().x;
    SocText(dl, p, kVText, t, 1.45f);
    float tw = SocTextW(t, 1.45f);
    if(sub && *sub) SocText(dl, ImVec2(p.x + tw + UI::S(20), p.y + ImGui::GetFontSize() * 0.55f), kVDim, sub, 0.85f);
    float y = p.y + ImGui::GetFontSize() * 1.6f;
    dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + W, y), kVLine, UI::S(1));
    dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + UI::S(60), y), kVRed, UI::S(3));
    ImGui::Dummy(ImVec2(W, ImGui::GetFontSize() * 1.6f + UI::S(14)));
}
static bool SocArmed(const std::string& key) { return s_SocArm == key; }
static void SocArm(const std::string& key) { s_SocArm = key; s_nSocArmAt = MP_NowMs(); }

// ---- title bar + sidebar ----
static void SocTitleButtons(float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if(LToolButton("socfr", IC_USERPLUS, nullptr, h, true, s_nLTab == LT_FRIENDS)) s_nLTab = LT_FRIENDS;
    SocBadge(dl, ImVec2(p.x + h * 0.72f, p.y + h * 0.26f), (int)Social::Incoming().size());
    ImGui::SameLine(0, 0);
    ImVec2 b = ImGui::GetCursorScreenPos();
    if(LToolButton("socbell", IC_BELL, nullptr, h, true, s_nLTab == LT_INBOX)) { s_nLTab = LT_INBOX; s_nSocInboxSeenAt = MP_NowMs(); }
    SocBadge(dl, ImVec2(b.x + h * 0.72f, b.y + h * 0.26f), Social::UnreadInbox());
}
static void SocSideItems(float iw, float ih)
{
    LGroupLabel("SOCIAL");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    struct It { const char* id; int icon; const char* label; int tab; int badge; };
    Social::Conv* party = Social::Party();
    It items[] = {
        { "sfr", IC_USERPLUS, "Friends",     LT_FRIENDS,   (int)Social::Incoming().size() },
        { "sch", IC_CHAT,     "Chats",       LT_CHATS,     Social::UnreadChats() },
        { "spa", IC_PARTY,    "Party",       LT_PARTY,     0 },
        { "sco", IC_FLAG,     "Communities", LT_COMMUNITY, 0 },
        { "sin", IC_BELL,     "Inbox",       LT_INBOX,     Social::UnreadInbox() },
    };
    for(auto& it : items)
    {
        if(LSideItem(it.id, it.icon, it.label, s_nLTab == it.tab, iw, ih)) { s_nLTab = it.tab; if(it.tab == LT_INBOX) s_nSocInboxSeenAt = MP_NowMs(); }
        ImVec2 mx = ImGui::GetItemRectMax(), mn = ImGui::GetItemRectMin();
        float cy = (mn.y + mx.y) * 0.5f;
        if(it.badge > 0) SocBadge(dl, ImVec2(mx.x - UI::S(30), cy), it.badge);
        else if(it.tab == LT_PARTY && party)
        { char b[8]; snprintf(b, sizeof(b), "%d/5", (int)party->members.size()); float tw = SocTextW(b, 0.75f); SocText(dl, ImVec2(mx.x - UI::S(18) - tw, cy - ImGui::GetFontSize() * 0.38f), kVTeal, b, 0.75f); }
    }
}

// ---- Friends ----
static void SocPersonRow(const Social::Person& p, float W, int mode)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = UI::S(88), bh = UI::S(58), gap = UI::S(10);
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(W, rowH));
    ImVec2 next = ImGui::GetCursorScreenPos();
    const bool online = Social::IsOnline(p.id);
    VChamfer(dl, a, ImVec2(a.x + W, a.y + rowH - UI::S(6)), UI::S(12), kVPanel, IM_COL32(236, 232, 225, 22));
    float cy = a.y + (rowH - UI::S(6)) * 0.5f;
    SocAvatar(dl, ImVec2(a.x + UI::S(44), cy), UI::S(26), p.name, p.id, online ? 1 : 0);
    float tx = a.x + UI::S(88), fs = ImGui::GetFontSize();
    SocText(dl, ImVec2(tx, cy - fs * 0.95f), kVText, p.name.empty() ? "?" : p.name.c_str());
    char idb[16]; snprintf(idb, sizeof(idb), "#%s", p.id.c_str());
    SocText(dl, ImVec2(tx + SocTextW(p.name.empty() ? "?" : p.name.c_str()) + UI::S(10), cy - fs * 0.85f), kVDim, idb, 0.85f);
    SocText(dl, ImVec2(tx, cy + fs * 0.12f), online ? kVTeal : kVDim, online ? "Online" : "Offline", 0.8f);
    // buttons from the right
    float x = a.x + W - UI::S(14);
    float by = cy - bh * 0.5f;
    auto place = [&](float bw) { x -= bw; ImGui::SetCursorScreenPos(ImVec2(x, by)); x -= gap; };
    ImGui::PushID(p.id.c_str()); ImGui::PushID(mode);
    if(mode == 0)
    {
        if(Social::IsFriend(p.id)) { place(UI::S(150)); VBtn("fr", "FRIENDS", UI::S(150), bh, 4, true, IC_CHECK); }
        else if(Social::IsOutgoing(p.id)) { place(UI::S(130)); if(VBtn("cn", "Cancel", UI::S(130), bh, 3)) Social::CancelRequest(p.id); place(UI::S(150)); VBtn("pd", "PENDING", UI::S(150), bh, 4); }
        else if(Social::IsIncoming(p.id)) { place(UI::S(130)); if(VBtn("dc", "Decline", UI::S(130), bh, 3)) Social::Decline(p.id); place(UI::S(130)); if(VBtn("ac", "Accept", UI::S(130), bh, 1)) Social::Accept(p.id); }
        else { place(UI::S(130)); if(VBtn("add", "ADD", UI::S(130), bh, 1, Social::Up(), IC_PLUS)) Social::AddFriend(p.id); }
    }
    else if(mode == 1)
    {
        place(UI::S(130)); if(VBtn("dc", "Decline", UI::S(130), bh, 3)) Social::Decline(p.id);
        place(UI::S(130)); if(VBtn("ac", "Accept", UI::S(130), bh, 1, true, IC_CHECK)) Social::Accept(p.id);
    }
    else if(mode == 2)
    {
        place(UI::S(130)); if(VBtn("cn", "Cancel", UI::S(130), bh, 3)) Social::CancelRequest(p.id);
        place(UI::S(190)); VBtn("wt", "WAITING...", UI::S(190), bh, 4);
    }
    else
    {
        std::string arm = "rm" + p.id;
        if(SocArmed(arm)) { place(UI::S(150)); if(VBtn("rm", "Remove?", UI::S(150), bh, 2)) { Social::RemoveFriend(p.id); s_SocArm.clear(); } }
        else { place(bh); if(VBtn("rm", "", bh, bh, 3, true, IC_TRASH)) SocArm(arm); }
        Social::Conv* party = Social::Party();
        bool inParty = false; if(party) for(auto& m : party->members) if(m.id == p.id) inParty = true;
        bool canInv = !inParty && (!party || (party->owner == Social::MyId() && party->members.size() < 5));
        place(UI::S(150)); if(VBtn("pi", inParty ? "IN PARTY" : "Party", UI::S(150), bh, inParty ? 4 : 0, canInv, IC_PARTY)) { Social::InviteToParty(p.id); }
        place(UI::S(130)); if(VBtn("ch", "Chat", UI::S(130), bh, 1, true, IC_CHAT)) { if(Social::OpenDm(p.id)) { s_SocConv = "u" + p.id; s_nLTab = LT_CHATS; s_bSocNewGroup = s_bSocAddMember = false; } }
    }
    ImGui::PopID(); ImGui::PopID();
    ImGui::SetCursorScreenPos(next);
}
static void SocSection(const char* t, int n = -1)
{
    char b[64]; if(n >= 0) snprintf(b, sizeof(b), "%s  (%d)", t, n); else snprintf(b, sizeof(b), "%s", t);
    ImGui::Dummy(ImVec2(1, UI::S(4)));
    ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos();
    SocText(dl, p, kVDim, b, 0.82f);
    ImGui::Dummy(ImVec2(1, ImGui::GetFontSize() * 0.82f + UI::S(10)));
}
static void SocPageFriends(float W)
{
    char sub[96];
    snprintf(sub, sizeof(sub), "You: %s  #%s   %s", Social::MyName().empty() ? "?" : Social::MyName().c_str(), Social::MyId().c_str(), Social::Up() ? "- online" : "- connecting...");
    SocTitle("FRIENDS", sub);
    const float h = UI::S(70);
    if(LInputBox("socsearch", s_szSocSearch, "Search player ID or name", W - UI::S(220), h, IC_SEARCH)) VKeyboard::Open("Search player (ID or name)", s_szSocSearch, 40, OnSocSearch);
    ImGui::SameLine(0, UI::S(12));
    if(VBtn("socgo", "SEARCH", UI::S(208), h, 2, Social::Up(), IC_SEARCH)) Social::Search(s_szSocSearch);
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    if(!Social::SearchQuery().empty())
    {
        auto res = Social::SearchResults();
        SocSection("SEARCH RESULTS", (int)res.size());
        if(res.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kVDim));
            ImGui::TextWrapped(Social::Searching() ? "Searching..." : "Nobody found. Type the 8 digit ID (like #12345678) or part of the player name. Players show up after they open MP 3.63 once.");
            ImGui::PopStyleColor();
        }
        for(auto& p : res) SocPersonRow(p, W, 0);
    }
    if(!Social::Incoming().empty()) { SocSection("FRIEND REQUESTS", (int)Social::Incoming().size()); for(auto p : Social::Incoming()) SocPersonRow(p, W, 1); }
    if(!Social::Outgoing().empty()) { SocSection("SENT REQUESTS", (int)Social::Outgoing().size()); for(auto p : Social::Outgoing()) SocPersonRow(p, W, 2); }
    std::vector<Social::Person> fr = Social::Friends();
    std::sort(fr.begin(), fr.end(), [](const Social::Person& a, const Social::Person& b) { bool oa = Social::IsOnline(a.id), ob = Social::IsOnline(b.id); if(oa != ob) return oa; return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    int on = 0; for(auto& f : fr) if(Social::IsOnline(f.id)) ++on;
    char t[48]; snprintf(t, sizeof(t), "FRIENDS  %d ONLINE", on);
    SocSection(t, (int)fr.size());
    if(fr.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kVDim));
        ImGui::TextWrapped("No friends yet. Search a player above and tap ADD (+). When they accept, you can chat, make group chats and invite them to your party.");
        ImGui::PopStyleColor();
    }
    for(auto& f : fr) SocPersonRow(f, W, 3);
}

// ---- chat pane (used by Chats and Party) ----
static void SocChatPane(Social::Conv* c, float W, float H, bool header)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    float headH = 0;
    if(header)
    {
        headH = UI::S(84);
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, headH)); ImVec2 next = ImGui::GetCursorScreenPos();
        VChamfer(dl, a, ImVec2(a.x + W, a.y + headH - UI::S(8)), UI::S(12), kVPanel, IM_COL32(236, 232, 225, 24));
        float cy = a.y + (headH - UI::S(8)) * 0.5f;
        std::string oid = c->kind == Social::CONV_DM ? c->key.substr(1) : c->gid;
        int online = c->kind == Social::CONV_DM ? (Social::IsOnline(oid) ? 1 : 0) : -1;
        SocAvatar(dl, ImVec2(a.x + UI::S(42), cy), UI::S(24), c->title, oid, online);
        SocText(dl, ImVec2(a.x + UI::S(82), cy - fs * 0.95f), kVText, c->title.c_str());
        char sub[96];
        if(c->kind == Social::CONV_DM) snprintf(sub, sizeof(sub), "#%s  -  %s", oid.c_str(), online ? "Online" : "Offline");
        else { int on = 0; for(auto& m : c->members) if(Social::IsOnline(m.id)) ++on; snprintf(sub, sizeof(sub), "%s  -  %d members, %d online", c->kind == Social::CONV_PARTY ? "Party" : "Group chat", (int)c->members.size(), on); }
        SocText(dl, ImVec2(a.x + UI::S(82), cy + fs * 0.1f), online == 1 ? kVTeal : kVDim, sub, 0.8f);
        float bh = UI::S(54), x = a.x + W - UI::S(12);
        if(c->kind == Social::CONV_GROUP)
        {
            std::string arm = "lv" + c->key;
            x -= UI::S(130); ImGui::SetCursorScreenPos(ImVec2(x, cy - bh * 0.5f));
            if(VBtn("glv", SocArmed(arm) ? "Sure?" : "Leave", UI::S(130), bh, SocArmed(arm) ? 2 : 3)) { if(SocArmed(arm)) { Social::LeaveGroup(c->key); s_SocConv.clear(); s_SocArm.clear(); ImGui::SetCursorScreenPos(next); return; } SocArm(arm); }
            x -= UI::S(130) + UI::S(10); ImGui::SetCursorScreenPos(ImVec2(x, cy - bh * 0.5f));
            if(VBtn("gadd", "Add", UI::S(120), bh, 0, true, IC_USERPLUS)) { s_bSocAddMember = true; s_bSocNewGroup = false; }
        }
        else if(c->kind == Social::CONV_DM)
        {
            Social::Conv* party = Social::Party();
            bool canInv = !party || (party->owner == Social::MyId() && party->members.size() < 5);
            x -= UI::S(150); ImGui::SetCursorScreenPos(ImVec2(x, cy - bh * 0.5f));
            if(VBtn("pinv", "Party", UI::S(150), bh, 0, canInv, IC_PARTY)) Social::InviteToParty(oid);
        }
        ImGui::SetCursorScreenPos(next);
    }
    const float inH = UI::S(66);
    float msgH = H - headH - inH - UI::S(12);
    ImGui::BeginChild("##socmsgs", ImVec2(W, msgH), false);
    {
        ImDrawList* cdl = ImGui::GetWindowDrawList();
        float cw = ImGui::GetContentRegionAvail().x;
        if(c->msgs.empty())
        {
            ImGui::Dummy(ImVec2(1, UI::S(20)));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kVDim));
            ImGui::TextWrapped("No messages yet. Say hi. Messages are end-to-end encrypted and wait for your friends while they are offline.");
            ImGui::PopStyleColor();
        }
        for(size_t i = 0; i < c->msgs.size(); ++i)
        {
            const Social::Msg& m = c->msgs[i];
            if(m.fromId.empty())
            {   // system line
                float tw = SocTextW(m.text.c_str(), 0.75f);
                ImVec2 p = ImGui::GetCursorScreenPos();
                SocText(cdl, ImVec2(p.x + (cw - tw) * 0.5f, p.y + UI::S(4)), kVDim, m.text.c_str(), 0.75f);
                ImGui::Dummy(ImVec2(cw, fs * 0.75f + UI::S(12)));
                continue;
            }
            bool showName = !m.mine && c->kind != Social::CONV_DM && (i == 0 || c->msgs[i - 1].fromId != m.fromId);
            float maxW = cw * 0.68f, sc = 0.92f;
            ImVec2 ts = UI::Font()->CalcTextSizeA(fs * sc, FLT_MAX, maxW - UI::S(28), m.text.c_str());
            char ago[16]; SocAgo(m.ts, ago, sizeof(ago));
            float bw = ImMax(ts.x, SocTextW(ago, 0.65f)) + UI::S(28), bh = ts.y + fs * 0.65f + UI::S(22);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float nameH = showName ? fs * 0.75f + UI::S(4) : 0;
            if(showName) SocText(cdl, ImVec2(p.x + UI::S(4), p.y), SocColor(m.fromId), m.fromName.c_str(), 0.75f);
            float bx = m.mine ? p.x + cw - bw - UI::S(4) : p.x + UI::S(4), by = p.y + nameH;
            ImU32 fill = m.mine ? IM_COL32(64, 230, 190, 46) : IM_COL32(15, 25, 35, 190), brd = m.mine ? VAlpha(kVTeal, 0.55f) : IM_COL32(236, 232, 225, 30);
            VChamfer(cdl, ImVec2(bx, by), ImVec2(bx + bw, by + bh), UI::S(9), fill, brd, UI::S(1.2f));
            SocText(cdl, ImVec2(bx + UI::S(14), by + UI::S(9)), kVText, m.text.c_str(), sc, maxW - UI::S(28));
            float aw = SocTextW(ago, 0.65f);
            SocText(cdl, ImVec2(bx + bw - aw - UI::S(12), by + bh - fs * 0.65f - UI::S(6)), kVDim, ago, 0.65f);
            ImGui::Dummy(ImVec2(cw, nameH + bh + UI::S(8)));
        }
        static std::string lastKey; static size_t lastN = 0;
        if(lastKey != c->key || lastN != c->msgs.size()) { lastKey = c->key; lastN = c->msgs.size(); ImGui::SetScrollHereY(1.0f); }
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(1, UI::S(4)));
    bool canSend = c->kind != Social::CONV_DM || Social::IsFriend(c->key.substr(1));
    if(LInputBox("socmsg", "", canSend ? "Type a message..." : "You are not friends any more", W - inH - UI::S(10), inH, IC_CHAT) && canSend)
    { s_SocMsgKey = c->key; VKeyboard::Open("Message", "", 200, OnSocMsg); }
    ImGui::SameLine(0, UI::S(10));
    if(VBtn("socsend", "", inH, inH, 2, canSend, IC_SEND)) { s_SocMsgKey = c->key; VKeyboard::Open("Message", "", 200, OnSocMsg); }
    Social::MarkRead(c->key);
}

// ---- Chats ----
static void SocFriendPicker(float W, bool forNew, Social::Conv* group)
{
    for(auto& f : Social::Friends())
    {
        if(group) { bool in = false; for(auto& m : group->members) if(m.id == f.id) in = true; if(in) continue; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetCursorScreenPos(); const float rh = UI::S(76);
        ImGui::PushID(f.id.c_str());
        bool hit = ImGui::InvisibleButton("##pk", ImVec2(W, rh - UI::S(6)));
        ImGui::PopID();
        bool on = s_SocPick.count(f.id) > 0;
        VChamfer(dl, a, ImVec2(a.x + W, a.y + rh - UI::S(6)), UI::S(10), on ? kVTealDim : kVPanel, on ? VAlpha(kVTeal, 0.7f) : IM_COL32(236, 232, 225, 22));
        float cy = a.y + (rh - UI::S(6)) * 0.5f;
        SocAvatar(dl, ImVec2(a.x + UI::S(38), cy), UI::S(22), f.name, f.id, Social::IsOnline(f.id) ? 1 : 0);
        SocText(dl, ImVec2(a.x + UI::S(76), cy - ImGui::GetFontSize() * 0.5f), kVText, f.name.c_str());
        // check box on the right
        ImVec2 cb(a.x + W - UI::S(46), cy - UI::S(16));
        if(forNew) { VChamfer(dl, cb, ImVec2(cb.x + UI::S(32), cb.y + UI::S(32)), UI::S(6), on ? kVTeal : 0, VAlpha(kVText, 0.5f)); if(on) LIcon(dl, IC_CHECK, ImVec2(cb.x + UI::S(16), cb.y + UI::S(16)), UI::S(12), kVDark); }
        else LIcon(dl, IC_PLUS, ImVec2(cb.x + UI::S(16), cb.y + UI::S(16)), UI::S(14), kVTeal);
        if(hit) { MpUiClick(); if(forNew) { if(on) s_SocPick.erase(f.id); else s_SocPick.insert(f.id); } else if(group) Social::AddToGroup(group->key, f.id); }
    }
}
static void SocPageChats(float W, float H)
{
    float L = ImMax(UI::S(330), W * 0.34f), R = W - L - UI::S(14);
    ImGui::BeginChild("##socconvs", ImVec2(L, H), false);
    {
        float lw = ImGui::GetContentRegionAvail().x;
        SocTitle("CHATS", nullptr);
        if(VBtn("newg", "NEW GROUP CHAT", lw, UI::S(62), s_bSocNewGroup ? 1 : 0, !Social::Friends().empty(), IC_PLUS)) { s_bSocNewGroup = true; s_bSocAddMember = false; s_SocPick.clear(); s_szSocGroupName[0] = 0; }
        ImGui::Dummy(ImVec2(1, UI::S(8)));
        auto convs = Social::Convs();
        if(convs.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kVDim));
            ImGui::TextWrapped("No chats yet. Open Friends and tap Chat on a friend.");
            ImGui::PopStyleColor();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float fs = ImGui::GetFontSize();
        for(auto* c : convs)
        {
            ImVec2 a = ImGui::GetCursorScreenPos(); const float rh = UI::S(92);
            ImGui::PushID(c->key.c_str());
            bool hit = ImGui::InvisibleButton("##cv", ImVec2(lw, rh - UI::S(6)));
            bool held = ImGui::IsItemActive();
            ImGui::PopID();
            bool on = c->key == s_SocConv && !s_bSocNewGroup;
            ImVec2 b(a.x + lw, a.y + rh - UI::S(6));
            if(on) { dl->AddRectFilled(a, b, kVTealDim); dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), kVTeal); }
            else if(held) dl->AddRectFilled(a, b, IM_COL32(236, 232, 225, 20));
            dl->AddLine(ImVec2(a.x, b.y + UI::S(3)), ImVec2(b.x, b.y + UI::S(3)), IM_COL32(236, 232, 225, 14));
            float cy = (a.y + b.y) * 0.5f;
            std::string oid = c->kind == Social::CONV_DM ? c->key.substr(1) : c->gid;
            SocAvatar(dl, ImVec2(a.x + UI::S(40), cy), UI::S(24), c->title, oid, c->kind == Social::CONV_DM ? (Social::IsOnline(oid) ? 1 : 0) : -1);
            float tx = a.x + UI::S(80), maxW = lw - UI::S(80) - UI::S(60);
            dl->PushClipRect(ImVec2(tx, a.y), ImVec2(tx + maxW, b.y), true);
            std::string title = c->title; if(c->kind == Social::CONV_PARTY) title = "[PARTY] " + title; else if(c->kind == Social::CONV_GROUP) title = "[GROUP] " + title;
            SocText(dl, ImVec2(tx, cy - fs * 0.95f), c->kind == Social::CONV_PARTY ? kVTeal : kVText, title.c_str());
            std::string last = c->msgs.empty() ? "" : ((c->msgs.back().mine && !c->msgs.back().fromId.empty()) ? "You: " : "") + c->msgs.back().text;
            SocText(dl, ImVec2(tx, cy + fs * 0.1f), c->unread ? kVText : kVDim, last.c_str(), 0.8f);
            dl->PopClipRect();
            if(c->unread) SocBadge(dl, ImVec2(b.x - UI::S(26), cy), c->unread);
            else if(c->last) { char ago[16]; SocAgo(c->last, ago, sizeof(ago)); SocText(dl, ImVec2(b.x - UI::S(14) - SocTextW(ago, 0.7f), cy - fs * 0.35f), kVDim, ago, 0.7f); }
            if(hit) { MpUiClick(); s_SocConv = c->key; s_bSocNewGroup = s_bSocAddMember = false; }
        }
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, UI::S(14));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(p.x - UI::S(7), p.y), ImVec2(p.x - UI::S(7), p.y + H), kVLine, UI::S(1.5f));
    }
    ImGui::BeginChild("##socchatR", ImVec2(R, H), false);
    {
        Social::Conv* c = s_SocConv.empty() ? nullptr : Social::FindConv(s_SocConv);
        if(s_bSocNewGroup)
        {
            SocTitle("NEW GROUP CHAT", "pick friends, then Create");
            if(LInputBox("gname", s_szSocGroupName, "Group name (optional)", R, UI::S(68), IC_CHAT)) VKeyboard::Open("Group name", s_szSocGroupName, 40, OnSocGroupName);
            ImGui::Dummy(ImVec2(1, UI::S(8)));
            if(VBtn("gcreate", "CREATE GROUP", R * 0.5f - UI::S(6), UI::S(64), 1, !s_SocPick.empty(), IC_USERS))
            {
                std::vector<std::string> ids(s_SocPick.begin(), s_SocPick.end());
                s_SocConv = Social::CreateGroup(s_szSocGroupName, ids); s_bSocNewGroup = false;
            }
            ImGui::SameLine(0, UI::S(12));
            if(VBtn("gcancel", "Cancel", R * 0.5f - UI::S(6), UI::S(64), 0)) s_bSocNewGroup = false;
            SocSection("FRIENDS", (int)Social::Friends().size());
            SocFriendPicker(R, true, nullptr);
            LDragScroll();
        }
        else if(c && s_bSocAddMember && c->kind == Social::CONV_GROUP)
        {
            SocTitle("ADD TO GROUP", c->title.c_str());
            if(VBtn("gdone", "Done", UI::S(160), UI::S(60), 1)) s_bSocAddMember = false;
            SocSection("TAP A FRIEND TO ADD");
            SocFriendPicker(R, false, c);
            LDragScroll();
        }
        else if(c) SocChatPane(c, R, H, true);
        else
        {
            ImGui::Dummy(ImVec2(1, H * 0.35f));
            const char* t = "Pick a chat on the left, or tap Chat on a friend.";
            ImGui::SetCursorPosX((R - SocTextW(t)) * 0.5f);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kVDim), "%s", t);
        }
    }
    ImGui::EndChild();
}

// ---- Party ----
static void SocPageParty(float W, float H)
{
    Social::Conv* party = Social::Party();
    char sub[64]; snprintf(sub, sizeof(sub), party ? "%d / 5" : "up to 5 friends, party chat", party ? (int)party->members.size() : 0);
    SocTitle("PARTY", sub);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // invites waiting for you
    for(auto* n : Social::Inbox()) if(n->kind == Social::N_PARTY_INV && !n->done)
    {
        ImVec2 a = ImGui::GetCursorScreenPos(); const float rh = UI::S(80);
        ImGui::Dummy(ImVec2(W, rh)); ImVec2 next = ImGui::GetCursorScreenPos();
        VChamfer(dl, a, ImVec2(a.x + W, a.y + rh - UI::S(8)), UI::S(12), kVTealDim, VAlpha(kVTeal, 0.6f));
        float cy = a.y + (rh - UI::S(8)) * 0.5f;
        SocText(dl, ImVec2(a.x + UI::S(20), cy - ImGui::GetFontSize() * 0.5f), kVText, n->text.c_str());
        ImGui::PushID(n);
        ImGui::SetCursorScreenPos(ImVec2(a.x + W - UI::S(140) - UI::S(12), cy - UI::S(27))); if(VBtn("pd", "Decline", UI::S(140), UI::S(54), 3)) Social::DeclineParty(n->ref);
        ImGui::SetCursorScreenPos(ImVec2(a.x + W - UI::S(290) - UI::S(12), cy - UI::S(27))); if(VBtn("pa", "Join", UI::S(140), UI::S(54), 1)) Social::AcceptParty(n->ref);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(next);
    }
    if(!party)
    {
        ImGui::Dummy(ImVec2(1, UI::S(20)));
        if(VBtn("pmake", "CREATE PARTY", UI::S(360), UI::S(76), 2, true, IC_PARTY)) Social::CreateParty();
        ImGui::Dummy(ImVec2(1, UI::S(10)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kVDim));
        ImGui::TextWrapped("Make a party and invite up to 4 friends. Everyone in the party gets the party chat. Invites need an accept (they show in the Inbox).");
        ImGui::PopStyleColor();
        return;
    }
    // 5 member cards
    const float cardH = UI::S(150), gap = UI::S(12), cw = (W - gap * 4) / 5;
    ImVec2 a0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(W, cardH + UI::S(10))); ImVec2 next = ImGui::GetCursorScreenPos();
    for(int i = 0; i < 5; ++i)
    {
        ImVec2 a(a0.x + i * (cw + gap), a0.y), b(a.x + cw, a.y + cardH);
        if(i < (int)party->members.size())
        {
            const Social::Member& m = party->members[i];
            bool lead = m.id == party->owner;
            VChamfer(dl, a, b, UI::S(14), kVPanel, lead ? VAlpha(kVTeal, 0.8f) : IM_COL32(236, 232, 225, 50));
            SocAvatar(dl, ImVec2((a.x + b.x) * 0.5f, a.y + UI::S(50)), UI::S(28), m.name, m.id, Social::IsOnline(m.id) ? 1 : 0);
            dl->PushClipRect(a, b, true);
            float tw = SocTextW(m.name.c_str(), 0.85f);
            SocText(dl, ImVec2((a.x + b.x - tw) * 0.5f, a.y + UI::S(90)), kVText, m.name.c_str(), 0.85f);
            const char* tag = lead ? "LEADER" : (m.id == Social::MyId() ? "YOU" : "");
            if(*tag) { float t2 = SocTextW(tag, 0.65f); SocText(dl, ImVec2((a.x + b.x - t2) * 0.5f, a.y + UI::S(120)), lead ? kVTeal : kVDim, tag, 0.65f); }
            dl->PopClipRect();
        }
        else
        {
            VChamfer(dl, a, b, UI::S(14), IM_COL32(236, 232, 225, 6), IM_COL32(236, 232, 225, 26));
            LIcon(dl, IC_PLUS, ImVec2((a.x + b.x) * 0.5f, a.y + cardH * 0.42f), UI::S(18), kVDim);
            float tw = SocTextW("EMPTY", 0.7f); SocText(dl, ImVec2((a.x + b.x - tw) * 0.5f, a.y + cardH * 0.66f), kVDim, "EMPTY", 0.7f);
        }
    }
    ImGui::SetCursorScreenPos(next);
    if(VBtn("pleave", "Leave party", UI::S(220), UI::S(56), 3)) { Social::LeaveParty(); return; }
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    float restH = H - (ImGui::GetCursorScreenPos().y - a0.y) - ImGui::GetFontSize() * 2.2f;
    if(restH < UI::S(200)) restH = UI::S(200);
    float L = ImMax(UI::S(300), W * 0.34f), R = W - L - UI::S(14);
    ImGui::BeginChild("##pinvl", ImVec2(L, restH), false);
    {
        SocSection(party->owner == Social::MyId() ? "INVITE FRIENDS" : "ONLY THE LEADER INVITES");
        float lw = ImGui::GetContentRegionAvail().x;
        for(auto& f : Social::Friends())
        {
            bool in = false; for(auto& m : party->members) if(m.id == f.id) in = true;
            if(in) continue;
            ImVec2 a = ImGui::GetCursorScreenPos(); const float rh = UI::S(76);
            ImGui::Dummy(ImVec2(lw, rh)); ImVec2 nx = ImGui::GetCursorScreenPos();
            VChamfer(dl, a, ImVec2(a.x + lw, a.y + rh - UI::S(6)), UI::S(10), kVPanel, IM_COL32(236, 232, 225, 20));
            float cy = a.y + (rh - UI::S(6)) * 0.5f;
            bool on = Social::IsOnline(f.id);
            SocAvatar(dl, ImVec2(a.x + UI::S(36), cy), UI::S(22), f.name, f.id, on ? 1 : 0);
            dl->PushClipRect(a, ImVec2(a.x + lw - UI::S(130), a.y + rh), true);
            SocText(dl, ImVec2(a.x + UI::S(72), cy - ImGui::GetFontSize() * 0.5f), on ? kVText : kVDim, f.name.c_str(), 0.9f);
            dl->PopClipRect();
            ImGui::PushID(f.id.c_str());
            ImGui::SetCursorScreenPos(ImVec2(a.x + lw - UI::S(120), cy - UI::S(25)));
            if(VBtn("inv", "Invite", UI::S(112), UI::S(50), 1, party->owner == Social::MyId() && party->members.size() < 5)) Social::InviteToParty(f.id);
            ImGui::PopID();
            ImGui::SetCursorScreenPos(nx);
        }
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, UI::S(14));
    ImGui::BeginChild("##pchat", ImVec2(R, restH), false);
    SocSection("PARTY CHAT");
    SocChatPane(party, R, restH - ImGui::GetFontSize() * 1.4f, false);
    ImGui::EndChild();
}

// ---- Communities ----
static void SocPageCommunity(float W, float H)
{
    float L = ImMax(UI::S(330), W * 0.32f), R = W - L - UI::S(14);
    auto all = Social::Communities();
    ImGui::BeginChild("##comlist", ImVec2(L, H), false);
    {
        float lw = ImGui::GetContentRegionAvail().x;
        SocTitle("COMMUNITIES", nullptr);
        if(VBtn("cnew", "CREATE COMMUNITY", lw, UI::S(62), s_bSocNewCom ? 1 : 2, Social::Up(), IC_PLUS)) { s_bSocNewCom = true; s_szSocComName[0] = s_szSocComDesc[0] = 0; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float fs = ImGui::GetFontSize();
        for(int pass = 0; pass < 2; ++pass)
        {
            int n = 0; for(auto* c : all) { bool mine = c->joined || c->followed || c->ownerId == Social::MyId(); if(mine == (pass == 0)) ++n; }
            SocSection(pass == 0 ? "YOUR COMMUNITIES" : "DISCOVER", n);
            if(!n) { ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kVDim), "%s", pass == 0 ? "Join or follow one below." : (Social::Up() ? "Nothing new yet." : "Connecting...")); continue; }
            for(auto* c : all)
            {
                bool mine = c->joined || c->followed || c->ownerId == Social::MyId();
                if(mine != (pass == 0)) continue;
                ImVec2 a = ImGui::GetCursorScreenPos(); const float rh = UI::S(86);
                ImGui::PushID(c->cid.c_str());
                bool hit = ImGui::InvisibleButton("##cm", ImVec2(lw, rh - UI::S(6)));
                ImGui::PopID();
                bool on = c->cid == s_SocCom && !s_bSocNewCom;
                ImVec2 b(a.x + lw, a.y + rh - UI::S(6));
                if(on) { dl->AddRectFilled(a, b, kVTealDim); dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), kVTeal); }
                dl->AddLine(ImVec2(a.x, b.y + UI::S(3)), ImVec2(b.x, b.y + UI::S(3)), IM_COL32(236, 232, 225, 14));
                float cy = (a.y + b.y) * 0.5f;
                ImU32 col = SocColor(c->cid);
                VChamfer(dl, ImVec2(a.x + UI::S(14), cy - UI::S(24)), ImVec2(a.x + UI::S(62), cy + UI::S(24)), UI::S(10), VAlpha(col, 0.22f), VAlpha(col, 0.9f));
                LIcon(dl, IC_FLAG, ImVec2(a.x + UI::S(38), cy), UI::S(16), kVText);
                dl->PushClipRect(ImVec2(a.x + UI::S(76), a.y), ImVec2(b.x - UI::S(10), b.y), true);
                SocText(dl, ImVec2(a.x + UI::S(76), cy - fs * 0.95f), kVText, c->name.c_str());
                char sub[80]; snprintf(sub, sizeof(sub), "by %s%s%s", c->ownerName.c_str(), c->joined ? "  - joined" : "", (!c->joined && c->followed) ? "  - following" : "");
                SocText(dl, ImVec2(a.x + UI::S(76), cy + fs * 0.1f), kVDim, sub, 0.75f);
                dl->PopClipRect();
                int newPosts = 0; for(auto& p : c->posts) if(p.ts > c->seen && p.fromId != Social::MyId()) ++newPosts;
                if(newPosts && (c->joined || c->followed)) SocBadge(dl, ImVec2(b.x - UI::S(26), cy), newPosts, kVTeal);
                if(hit) { MpUiClick(); s_SocCom = c->cid; s_bSocNewCom = false; }
            }
        }
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, UI::S(14));
    { ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos(); dl->AddLine(ImVec2(p.x - UI::S(7), p.y), ImVec2(p.x - UI::S(7), p.y + H), kVLine, UI::S(1.5f)); }
    ImGui::BeginChild("##comR", ImVec2(R, H), false);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float fs = ImGui::GetFontSize();
        Social::Community* c = s_SocCom.empty() ? nullptr : Social::FindCommunity(s_SocCom);
        if(c && c->deleted) c = nullptr;
        if(s_bSocNewCom)
        {
            SocTitle("CREATE COMMUNITY", "everyone can find it, join or follow it");
            if(LInputBox("cname", s_szSocComName, "Community name", R, UI::S(68), IC_FLAG)) VKeyboard::Open("Community name", s_szSocComName, 40, OnSocComName);
            ImGui::Dummy(ImVec2(1, UI::S(8)));
            if(LInputBox("cdesc", s_szSocComDesc, "What is it about? (optional)", R, UI::S(68), IC_INFO)) VKeyboard::Open("Description", s_szSocComDesc, 300, OnSocComDesc);
            ImGui::Dummy(ImVec2(1, UI::S(12)));
            if(VBtn("ccreate", "CREATE", R * 0.5f - UI::S(6), UI::S(64), 1, s_szSocComName[0] != 0 && Social::Up()))
            { s_SocCom = Social::CreateCommunity(s_szSocComName, s_szSocComDesc); s_bSocNewCom = false; }
            ImGui::SameLine(0, UI::S(12));
            if(VBtn("ccancel", "Cancel", R * 0.5f - UI::S(6), UI::S(64), 0)) s_bSocNewCom = false;
        }
        else if(c)
        {
            Social::ViewCommunity(c->cid);
            // banner
            ImVec2 a = ImGui::GetCursorScreenPos(); float bh = UI::S(170);
            ImGui::Dummy(ImVec2(R, bh)); ImVec2 next = ImGui::GetCursorScreenPos();
            ImU32 col = SocColor(c->cid);
            dl->AddRectFilledMultiColor(a, ImVec2(a.x + R, a.y + bh - UI::S(10)), VAlpha(col, 0.30f), VAlpha(col, 0.05f), VAlpha(col, 0.02f), VAlpha(col, 0.18f));
            VChamfer(dl, a, ImVec2(a.x + R, a.y + bh - UI::S(10)), UI::S(16), 0, VAlpha(col, 0.6f));
            SocText(dl, ImVec2(a.x + UI::S(22), a.y + UI::S(18)), kVText, c->name.c_str(), 1.4f);
            char sub[128]; snprintf(sub, sizeof(sub), "by %s  #%s   -   %d members   -   %d followers", c->ownerName.c_str(), c->ownerId.c_str(), c->members, c->followers);
            SocText(dl, ImVec2(a.x + UI::S(22), a.y + UI::S(18) + fs * 1.55f), kVDim, sub, 0.8f);
            float by = a.y + bh - UI::S(10) - UI::S(70), x = a.x + UI::S(22);
            ImGui::SetCursorScreenPos(ImVec2(x, by));
            if(VBtn("cjoin", c->joined ? "JOINED" : "JOIN", UI::S(170), UI::S(56), c->joined ? 4 : 1, true, c->joined ? IC_CHECK : IC_PLUS) || false) Social::JoinCommunity(c->cid, true);
            if(c->joined && c->ownerId != Social::MyId()) { ImGui::SameLine(0, UI::S(10)); if(VBtn("cleave", "Leave", UI::S(130), UI::S(56), 3)) Social::JoinCommunity(c->cid, false); }
            ImGui::SameLine(0, UI::S(10));
            if(VBtn("cfol", c->followed ? "Unfollow" : "Follow", UI::S(160), UI::S(56), c->followed ? 0 : 0, true, IC_HEART)) Social::FollowCommunity(c->cid, !c->followed);
            if(c->ownerId == Social::MyId())
            {
                std::string arm = "cd" + c->cid;
                ImGui::SameLine(0, UI::S(10));
                if(VBtn("cdel", SocArmed(arm) ? "Delete?" : "", SocArmed(arm) ? UI::S(150) : UI::S(56), UI::S(56), SocArmed(arm) ? 2 : 3, true, SocArmed(arm) ? -1 : IC_TRASH))
                { if(SocArmed(arm)) { Social::DeleteCommunity(c->cid); s_SocCom.clear(); s_SocArm.clear(); } else SocArm(arm); }
            }
            ImGui::SetCursorScreenPos(next);
            if(!c->desc.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kVText)); ImGui::TextWrapped("%s", c->desc.c_str()); ImGui::PopStyleColor(); ImGui::Dummy(ImVec2(1, UI::S(8))); }
            if(!c->memberList.empty())
            {
                std::string ml; int k = 0;
                for(auto& m : c->memberList) { if(k++ >= 12) { ml += " ..."; break; } if(!ml.empty()) ml += ", "; ml += m.name; }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kVDim)); ImGui::TextWrapped("Members: %s", ml.c_str()); ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(1, UI::S(6)));
            }
            if(c->joined || c->ownerId == Social::MyId())
            {
                if(LInputBox("cpost", "", "Write a post to the community...", R, UI::S(66), IC_CHAT)) { s_SocPostCid = c->cid; VKeyboard::Open("Post", "", 300, OnSocPost); }
            }
            else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kVDim), "Join to post. Follow to get its new posts in your Inbox.");
            SocSection("POSTS", (int)c->posts.size());
            if(c->posts.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kVDim), "No posts yet.");
            for(auto& p : c->posts)
            {
                float tw = R - UI::S(40);
                ImVec2 ts = UI::Font()->CalcTextSizeA(fs * 0.92f, FLT_MAX, tw, p.text.c_str());
                ImVec2 q = ImGui::GetCursorScreenPos(); float ph = ts.y + fs * 1.4f + UI::S(34);
                ImGui::Dummy(ImVec2(R, ph + UI::S(8)));
                VChamfer(dl, q, ImVec2(q.x + R, q.y + ph), UI::S(12), kVPanel, IM_COL32(236, 232, 225, 22));
                SocAvatar(dl, ImVec2(q.x + UI::S(32), q.y + UI::S(32)), UI::S(16), p.fromName, p.fromId, -1);
                SocText(dl, ImVec2(q.x + UI::S(58), q.y + UI::S(16)), SocColor(p.fromId), p.fromName.c_str(), 0.85f);
                char ago[16]; SocAgo(p.ts, ago, sizeof(ago));
                SocText(dl, ImVec2(q.x + UI::S(66) + SocTextW(p.fromName.c_str(), 0.85f), q.y + UI::S(18)), kVDim, ago, 0.7f);
                SocText(dl, ImVec2(q.x + UI::S(20), q.y + UI::S(24) + fs * 1.1f), kVText, p.text.c_str(), 0.92f, tw);
            }
            LDragScroll();
        }
        else
        {
            ImGui::Dummy(ImVec2(1, H * 0.3f));
            const char* t = all.empty() ? "No communities yet. Make the first one." : "Pick a community on the left.";
            ImGui::SetCursorPosX((R - SocTextW(t)) * 0.5f);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kVDim), "%s", t);
        }
    }
    ImGui::EndChild();
}

// ---- Inbox ----
static void SocPageInbox(float W)
{
    char sub[48]; snprintf(sub, sizeof(sub), "%d new", Social::UnreadInbox());
    SocTitle("INBOX", sub);
    if(VBtn("iread", "Mark all read", UI::S(240), UI::S(56), 0)) Social::MarkInboxRead();
    ImGui::SameLine(0, UI::S(10));
    if(VBtn("iclear", "Clear", UI::S(150), UI::S(56), 3)) Social::ClearInbox();
    ImGui::Dummy(ImVec2(1, UI::S(8)));
    auto inbox = Social::Inbox();
    if(inbox.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kVDim), "Nothing here yet. Friend requests, messages, party invites and community posts show up here.");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    for(auto* n : inbox)
    {
        ImVec2 a = ImGui::GetCursorScreenPos(); const float rh = UI::S(92);
        ImGui::Dummy(ImVec2(W, rh)); ImVec2 next = ImGui::GetCursorScreenPos();
        VChamfer(dl, a, ImVec2(a.x + W, a.y + rh - UI::S(8)), UI::S(12), n->read ? kVPanel : IM_COL32(255, 70, 85, 22), n->read ? IM_COL32(236, 232, 225, 22) : VAlpha(kVRed, 0.5f));
        float cy = a.y + (rh - UI::S(8)) * 0.5f;
        int ic = IC_INFO;
        switch(n->kind)
        {
        case Social::N_FRIEND_REQ: case Social::N_FRIEND_ACC: case Social::N_FRIEND_DEC: ic = IC_USERPLUS; break;
        case Social::N_MSG: ic = IC_CHAT; break;
        case Social::N_GROUP: ic = IC_USERS; break;
        case Social::N_PARTY_INV: case Social::N_PARTY_ACC: ic = IC_PARTY; break;
        case Social::N_COMMUNITY: ic = IC_FLAG; break;
        }
        LIcon(dl, ic, ImVec2(a.x + UI::S(38), cy), UI::S(18), n->read ? kVDim : kVRed);
        if(!n->read) dl->AddCircleFilled(ImVec2(a.x + UI::S(12), cy), UI::S(5), kVRed);
        // actions on the right
        float x = a.x + W - UI::S(12); const float bh = UI::S(54);
        auto place = [&](float bw) { x -= bw; ImGui::SetCursorScreenPos(ImVec2(x, cy - bh * 0.5f)); x -= UI::S(10); };
        ImGui::PushID(n);
        if(n->kind == Social::N_FRIEND_REQ && !n->done && Social::IsIncoming(n->fromId))
        { place(UI::S(130)); if(VBtn("d", "Decline", UI::S(130), bh, 3)) Social::Decline(n->fromId); place(UI::S(130)); if(VBtn("a", "Accept", UI::S(130), bh, 1)) Social::Accept(n->fromId); }
        else if(n->kind == Social::N_PARTY_INV && !n->done)
        { place(UI::S(130)); if(VBtn("d", "Decline", UI::S(130), bh, 3)) Social::DeclineParty(n->ref); place(UI::S(130)); if(VBtn("a", "Join", UI::S(130), bh, 1)) { Social::AcceptParty(n->ref); s_nLTab = LT_PARTY; } }
        else if((n->kind == Social::N_MSG || n->kind == Social::N_GROUP) && Social::FindConv(n->ref))
        { place(UI::S(130)); if(VBtn("o", "Open", UI::S(130), bh, 0)) { s_SocConv = n->ref; s_nLTab = LT_CHATS; s_bSocNewGroup = s_bSocAddMember = false; n->read = true; } }
        else if(n->kind == Social::N_FRIEND_ACC && Social::IsFriend(n->fromId))
        { place(UI::S(130)); if(VBtn("c", "Chat", UI::S(130), bh, 1, true, IC_CHAT)) { if(Social::OpenDm(n->fromId)) { s_SocConv = "u" + n->fromId; s_nLTab = LT_CHATS; } n->read = true; } }
        else if(n->kind == Social::N_COMMUNITY && Social::FindCommunity(n->ref))
        { place(UI::S(130)); if(VBtn("o", "Open", UI::S(130), bh, 0)) { s_SocCom = n->ref; s_nLTab = LT_COMMUNITY; s_bSocNewCom = false; n->read = true; } }
        else if(n->kind == Social::N_PARTY_ACC && Social::Party())
        { place(UI::S(130)); if(VBtn("o", "Party", UI::S(130), bh, 0)) { s_nLTab = LT_PARTY; n->read = true; } }
        ImGui::PopID();
        float tx = a.x + UI::S(72);
        dl->PushClipRect(ImVec2(tx, a.y), ImVec2(x, a.y + rh), true);
        SocText(dl, ImVec2(tx, cy - fs * 0.95f), n->read ? kVText : IM_COL32_WHITE, n->text.c_str(), 0.92f);
        char ago[16]; SocAgo(n->ts, ago, sizeof(ago));
        char sub2[64]; snprintf(sub2, sizeof(sub2), "%s%s%s", ago, n->fromId.empty() ? "" : "  -  #", n->fromId.c_str());
        SocText(dl, ImVec2(tx, cy + fs * 0.12f), kVDim, sub2, 0.75f);
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(next);
    }
    // opened for 1.5 s = seen (requests / invites stay until answered)
    if(s_nSocInboxSeenAt && MP_NowMs() - s_nSocInboxSeenAt > 1500) { Social::MarkInboxRead(); s_nSocInboxSeenAt = 0; }
}

static void SocPage(int tab, float W, float H)
{
    s_nSocPageFrame = ImGui::GetFrameCount();
    W -= UI::S(6);
    if(!Social::Up())
    {   // a thin line at the top while connecting
        ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 p = ImGui::GetCursorScreenPos();
        float t = (float)fmod(ImGui::GetTime(), 1.4) / 1.4f;
        dl->AddRectFilled(ImVec2(p.x + W * t * 0.8f, p.y - UI::S(6)), ImVec2(p.x + W * (t * 0.8f + 0.2f), p.y - UI::S(3)), kVTeal);
    }
    switch(tab)
    {
    case LT_FRIENDS:   SocPageFriends(W); break;
    case LT_CHATS:     SocPageChats(W, H - UI::S(8)); break;
    case LT_PARTY:     SocPageParty(W, H - UI::S(8)); break;
    case LT_COMMUNITY: SocPageCommunity(W, H - UI::S(8)); break;
    case LT_INBOX:     if(!s_nSocInboxSeenAt && Social::UnreadInbox()) s_nSocInboxSeenAt = MP_NowMs(); SocPageInbox(W); break;
    }
}
