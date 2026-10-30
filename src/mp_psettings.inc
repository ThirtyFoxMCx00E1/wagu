// MP 3.35 (included by mp.cpp after mp_auth.inc): player list > "Settings" - a SA-MP style dialog
// (Name | Status, Enable in green / Disable in red, Toggle + Close). These are YOUR switches (only on
// this phone, saved in the config). Every server has the common ones; Mega Vil Futuristic adds
// Global chat (OOC) and the Google map radar, MW3 the PUBG compass. Server modes / permissions are
// not in here (only the owner, [ADMIN] or [MOD] can change those).
struct PsRow { const char* name; bool* val; int kind; };   // kind 0 = all servers, 1 = Mega Vil, 2 = MW3
static bool s_PsGoogle = true;   // mirror of settings.radarStyle != 0 for the table
static int  s_PsSel = 0;
static bool s_PsOpen = false;

static void PSettingsOpen() { s_PsOpen = true; s_PsSel = 0; }

static void PsApply(const PsRow& r)
{
    if(r.val == &s_PsGoogle) { settings.radarStyle = s_PsGoogle ? 2 : 0; }
    MP::SaveConfig();
}

static void PSettingsPopup()
{
    if(s_PsOpen) { ImGui::OpenPopup("Settings##psdlg"); s_PsOpen = false; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(ds.x * 0.62f, UI::S(1100)), ds.y * 0.80f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.04f, 0.04f, 0.05f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.95f));
    if(!ImGui::BeginPopupModal("Settings##psdlg", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    { ImGui::PopStyleColor(2); return; }
    s_PsGoogle = settings.radarStyle != 0;
    const bool rp = RpOnServer(), mw3 = Mw3OnServer();
    PsRow rows[] = {
        { "Name Tag",          &settings.showNameTags,   0 },
        { "Damage Indicator",  &settings.showDmgInd,     0 },
        { "Kill Feed",         &settings.showKillFeed,   0 },
        { "Chat Bubble",       &settings.chatBubbles,    0 },
        { "Voice Chat",        &settings.voiceChat,      0 },
        { "Hit Sound",         &settings.hitSound,       0 },
        { "Chat Timestamp",    &settings.chatTimestamps, 0 },
        { "Radar Player Icons",&settings.showRadarIcons, 0 },
        { "Global Chat (OOC)", &settings.globalChat,     1 },
        { "Google Map Radar",  &s_PsGoogle,              1 },
        { "PUBG Compass",      &settings.pubgCompass,    2 },
    };
    const int nRows = (int)(sizeof(rows) / sizeof(rows[0]));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetContentRegionAvail().x, rh = UI::S(52);
    const ImU32 head = IM_COL32(170, 170, 180, 255), white = IM_COL32_WHITE;
    const ImU32 green = IM_COL32(60, 220, 90, 255), red = IM_COL32(235, 60, 60, 255);
    ImGui::BeginChild("##pslist", ImVec2(0, -UI::S(96)), false);
    auto Row = [&](const char* a, const char* b, ImU32 bc, bool selectable, int idx)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool sel = selectable && idx == s_PsSel;
        if(selectable) { ImGui::PushID(idx); if(ImGui::InvisibleButton("##r", ImVec2(W, rh))) s_PsSel = idx; ImGui::PopID(); }
        else ImGui::Dummy(ImVec2(W, rh));
        if(sel) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + rh), IM_COL32(70, 90, 160, 200));
        float ty = p.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p.x + UI::S(14), ty), sel ? white : IM_COL32(225, 225, 225, 255), a);
        dl->AddText(ImVec2(p.x + W * 0.55f, ty), bc, b);
    };
    {   // header + player info (like the SA-MP dialog)
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(ImVec2(p.x + UI::S(14), p.y), head, "Name"); dl->AddText(ImVec2(p.x + W * 0.55f, p.y), head, "Status");
        ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + UI::S(8)));
        char uid[24]; snprintf(uid, sizeof(uid), "%.10s", settings.deviceId[0] ? settings.deviceId : "-");
        Row("UID", uid, white, false, -1);
        const char* me = (IsConnected() && myId < MP_MAX_PLAYERS && players[myId].active) ? players[myId].name : settings.name;
        Row("Username", me, white, false, -1);
        Row("Discord", s_ProfDiscord.empty() ? "Not linked (verify on Discord)" : s_ProfDiscord.c_str(), s_ProfDiscord.empty() ? IM_COL32(160, 160, 160, 255) : white, false, -1);
        const char* role = MP::IsLocalAdmin() ? "[ADMIN]" : MP::IsLocalMod() ? "[MOD]" : "Player";
        Row("Rank", role, white, false, -1);
    }
    int shown = 0;
    for(int i = 0; i < nRows; ++i)
    {
        if(rows[i].kind == 1 && !rp) continue;
        if(rows[i].kind == 2 && !mw3) continue;
        if(s_PsSel == 0 && !shown) s_PsSel = i;
        ++shown;
        bool on = *rows[i].val;
        Row(rows[i].name, on ? "Enable" : "Disable", on ? green : red, true, i);
    }
    LDragScroll();
    ImGui::EndChild();
    ImGui::TextDisabled(rp ? "Your own switches (this phone). Mega Vil: Global chat hides (( [OOC] )) lines, Google map = /googlemap on|off."
                           : "Your own switches (this phone). Server modes can only be changed by the owner, [ADMIN] or [MOD].");
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if(ImGui::Button("Toggle##ps", ImVec2(bw, UI::S(64))) && s_PsSel >= 0 && s_PsSel < nRows)
    {
        PsRow& r = rows[s_PsSel];
        if(!(r.kind == 1 && !rp) && !(r.kind == 2 && !mw3)) { *r.val = !*r.val; PsApply(r); }
    }
    ImGui::SameLine();
    if(ImGui::Button("Close##ps", ImVec2(bw, UI::S(64)))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
}
