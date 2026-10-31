// 3.01: SA-MP features, part 1 (included before mp_games.inc): dialogs, textdraws,
// and the shared race / derby state of the other players (PKT_EVENT).
//
// Dialogs work like SA-MP's ShowPlayerDialog: DIALOG_STYLE_MSGBOX / INPUT / LIST / PASSWORD /
// TABLIST / TABLIST_HEADERS, {RRGGBB} colour codes, "\n" rows and "\t" columns. The games menus
// (/race, /job, /dm, /derby, /class, /turf) use them, and a server can send one (PKT_DIALOG:
// mp_server --motd shows a welcome dialog).
// Textdraws work like SA-MP's TextDraw*: 640 x 448 canvas, ~r~ ~g~ ~b~ ~y~ ~p~ ~w~ ~l~ ~h~ colours,
// ~n~ new line, fonts 0-3 (3 = Pricedown when the phone has it), boxes, alignment, outline / shadow.
// GameText() is SA-MP's GameTextForPlayer (styles 1, 3, 4, 6).
#include "net/turf.h"

// ---- 3.02: gamemodes (the host's switches: MP Launcher > Add server > Gamemodes) ---------------
// 3.03: Map icons and Chat bubbles are not gamemodes: each player's own switches in Settings
// (Display > Map icons, Chat > Chat bubbles). Map icons still only show the starts of the
// gamemodes this server has.
struct GameModeInfo { uint16_t bit; const char* name; const char* cmd; const char* desc; };
static const GameModeInfo kGameModes[] =
{
    { GM_CLASS,   "Class selection", "class",   "/class: pick a gang, police or civilian (team + skin)" },
    { GM_TURF,    "Turf wars",       "turf",    "gang zones on the radar, take them by standing in them (/turf, /teams)" },
    { GM_RACE,    "Races",           "race",    "/race queue: 14 players race a GTA SA race" },
    { GM_DERBY,   "Derbies",         "derby",   "/derby: last car standing" },
    { GM_DM,      "Deathmatch",      "dm",      "/dm: deathmatch zones with weapons" },
    { GM_JOBS,    "Jobs",            "jobs",    "/job: trucking, quarry, pizza, taxi, medic ..." },
    { GM_LABELS,  "3D text labels",  "labels",  "names floating over the starts" },
    { GM_ACTORS,  "Actors",          "actors",  "people standing at the job starts" },
    { GM_HOME,    "Home Kit",        "home",    "/create house <name>, /setup, /buyhouse, /edithouse, /edithouseinteriors (real GTA SA buildings + furniture)" },
    { GM_SURVIVAL,"Survival (DayZ)", "survival","/survival: thirst, loot crates, zombies at night (Sunset DayZ style)" },
    { GM_CNR,     "Cops and Robbers","cnr",     "/cop or /robber: /rob, /arrest, /cuff, wanted stars, /hitlist, /placehit" },
    { GM_AD,      "Attack / Defend", "ad",      "/ad: attackers take the base checkpoint, defenders hold it (5 min rounds, /vote, /spec)" },
    { GM_STUNT,   "Stunt freeroam",  "stunt",   "/ramp, /jump, /chiliad, /lv, /sf, /ls, /n nitro, /fix, stunt bonus" },
    { GM_DRIFT,   "Drift",           "drift",   "/drift: drift points + combo, /drifttop (RogueDrifterZ style)" },
};
#define MP_GAMEMODE_COUNT ((int)(sizeof(kGameModes) / sizeof(kGameModes[0])))
static const char* GameModeName(uint16_t bit) { for(auto& g : kGameModes) if(g.bit == bit) return g.name; return "That gamemode"; }
static std::string GameModeList(uint16_t modes, const char* sep, bool cmds)
{
    std::string s;
    for(auto& g : kGameModes) if(modes & g.bit) { if(!s.empty()) s += sep; s += cmds ? g.cmd : g.name; }
    return s;
}
// commands of a gamemode that is off here: say so (and who can turn it on)
static bool NeedMode(uint16_t gm)
{
    if(MP::ModeOn(gm)) return true;
    if(client.GetState() != NET_CONNECTED) SysMsg(COL_RED, "Connect to a server first.");
    else SysMsg(COL_YELLOW, "%s is off on this server. Its host turns gamemodes on in MP Launcher > Add server > Gamemodes.", GameModeName(gm));
    return false;
}

// ---- {RRGGBB} colour segments -----------------------------------------------------------------
struct SampSeg { std::string t; ImU32 col; };
static bool SampHex6(const char* p, uint32_t& rgb)
{
    rgb = 0;
    for(int i = 0; i < 6; ++i)
    {
        char c = p[i]; int v;
        if(c >= '0' && c <= '9') v = c - '0'; else if(c >= 'a' && c <= 'f') v = c - 'a' + 10; else if(c >= 'A' && c <= 'F') v = c - 'A' + 10; else return false;
        rgb = (rgb << 4) | (uint32_t)v;
    }
    return p[6] == '}';
}
static void SampSplitColors(const char* s, ImU32 base, std::vector<SampSeg>& out)
{
    out.clear();
    ImU32 col = base; std::string cur;
    for(const char* p = s; *p; ++p)
    {
        uint32_t rgb;
        if(*p == '{' && SampHex6(p + 1, rgb))
        {
            if(!cur.empty()) { out.push_back({ cur, col }); cur.clear(); }
            col = IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
            p += 7; continue;
        }
        cur += *p;
    }
    if(!cur.empty()) out.push_back({ cur, col });
}
static float SampSegsWidth(ImFont* f, float fs, const std::vector<SampSeg>& v)
{
    float w = 0; for(auto& s : v) w += f->CalcTextSizeA(fs, FLT_MAX, 0, s.t.c_str()).x; return w;
}
static float SampDrawSegs(ImDrawList* dl, ImFont* f, float fs, ImVec2 p, const std::vector<SampSeg>& v, float alpha = 1.0f)
{
    float x = p.x;
    for(auto& s : v)
    {
        ImU32 c = (s.col & 0x00FFFFFF) | ((ImU32)(((s.col >> 24) & 0xFF) * alpha) << 24);
        dl->AddText(f, fs, ImVec2(x, p.y), c, s.t.c_str());
        x += f->CalcTextSizeA(fs, FLT_MAX, 0, s.t.c_str()).x;
    }
    return x - p.x;
}
static void SampSplit(const std::string& s, char sep, std::vector<std::string>& out)
{
    out.clear(); size_t a = 0;
    for(size_t i = 0; i <= s.size(); ++i) if(i == s.size() || s[i] == sep) { out.push_back(s.substr(a, i - a)); a = i + 1; }
}

// ---- dialogs ----------------------------------------------------------------------------------
typedef void (*SampDialogFn)(int response, int listItem, const char* input, int user);
struct SampDialog
{
    bool open = false, fromServer = false;
    int  id = 0, style = 0, sel = 0, user = 0;
    std::string caption, b1, b2, header;
    std::vector<std::string> lines, rows;
    char input[64] = "";
    SampDialogFn fn = nullptr;
    uint64_t openedAt = 0;
};
static SampDialog s_Dlg;
static bool SampDialogOpen() { return s_Dlg.open; }

static void ShowDialog(int id, int style, const char* caption, const char* info, const char* b1, const char* b2, SampDialogFn fn, int user = 0, bool fromServer = false)
{
    if(VKeyboard::IsOpen()) VKeyboard::Close();
    s_Dlg = SampDialog();
    s_Dlg.open = true; s_Dlg.fromServer = fromServer; s_Dlg.id = id; s_Dlg.style = style; s_Dlg.user = user; s_Dlg.fn = fn;
    s_Dlg.caption = caption ? caption : ""; s_Dlg.b1 = b1 ? b1 : ""; s_Dlg.b2 = b2 ? b2 : "";
    s_Dlg.openedAt = MP_NowMs();
    std::string body = info ? info : "";
    if(style == DIALOG_STYLE_LIST || style == DIALOG_STYLE_TABLIST || style == DIALOG_STYLE_TABLIST_HEADERS)
    {
        SampSplit(body, '\n', s_Dlg.rows);
        while(!s_Dlg.rows.empty() && s_Dlg.rows.back().empty()) s_Dlg.rows.pop_back();
        if(style == DIALOG_STYLE_TABLIST_HEADERS && !s_Dlg.rows.empty()) { s_Dlg.header = s_Dlg.rows[0]; s_Dlg.rows.erase(s_Dlg.rows.begin()); }
    }
    else SampSplit(body, '\n', s_Dlg.lines);
}
static void CloseDialog(int response)
{
    if(!s_Dlg.open) return;
    SampDialog d = s_Dlg; s_Dlg.open = false;
    if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) VKeyboard::Close();
    int item = (d.style == DIALOG_STYLE_LIST || d.style == DIALOG_STYLE_TABLIST || d.style == DIALOG_STYLE_TABLIST_HEADERS) ? d.sel : -1;
    if(d.fromServer)
    {
        if(client.GetState() != NET_CONNECTED) return;
        PktDialogResp r; memset(&r, 0, sizeof(r));
        r.type = PKT_DIALOG_RESPONSE; r.dialogId = (uint16_t)d.id; r.response = (uint8_t)response; r.listItem = (int16_t)item;
        MP_CopyStr(r.input, d.input, sizeof(r.input));
        client.Send(&r, sizeof(r));
        return;
    }
    if(d.fn) d.fn(response, item, d.input, d.user);
}
static void OnDialogInput(const char* text, void*) { MP_CopyStr(s_Dlg.input, text ? text : "", sizeof(s_Dlg.input)); }

// SA-MP look: black see-through panel, caption bar, white text, two grey buttons
static void RenderDialog()
{
    if(!s_Dlg.open) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImFont* f = UI::Font();
    const float fs = UI::S(30.0f), pad = UI::S(22.0f), rowH = UI::S(54.0f), btnH = UI::S(70.0f), capH = UI::S(56.0f);
    const bool list = s_Dlg.style == DIALOG_STYLE_LIST || s_Dlg.style == DIALOG_STYLE_TABLIST || s_Dlg.style == DIALOG_STYLE_TABLIST_HEADERS;
    const bool input = s_Dlg.style == DIALOG_STYLE_INPUT || s_Dlg.style == DIALOG_STYLE_PASSWORD;

    // columns (tablist): width of the widest cell per column
    std::vector<float> colW; std::vector<std::string> cells; std::vector<SampSeg> segs;
    auto measureRow = [&](const std::string& r)
    {
        if(s_Dlg.style == DIALOG_STYLE_LIST) { SampSplitColors(r.c_str(), IM_COL32_WHITE, segs); float w = SampSegsWidth(f, fs, segs); if(colW.empty()) colW.push_back(0); colW[0] = fmaxf(colW[0], w); return; }
        SampSplit(r, '\t', cells);
        if(colW.size() < cells.size()) colW.resize(cells.size(), 0.0f);
        for(size_t c = 0; c < cells.size(); ++c) { SampSplitColors(cells[c].c_str(), IM_COL32_WHITE, segs); colW[c] = fmaxf(colW[c], SampSegsWidth(f, fs, segs)); }
    };
    float contentW = 0, contentH = 0;
    if(list)
    {
        if(!s_Dlg.header.empty()) measureRow(s_Dlg.header);
        for(auto& r : s_Dlg.rows) measureRow(r);
        for(size_t c = 0; c < colW.size(); ++c) contentW += colW[c] + (c ? UI::S(36) : 0);
        contentH = fminf(rowH * (float)s_Dlg.rows.size(), ds.y * 0.52f) + (s_Dlg.header.empty() ? 0 : rowH);
    }
    else
    {
        for(auto& l : s_Dlg.lines) { SampSplitColors(l.c_str(), IM_COL32(169, 196, 228, 255), segs); contentW = fmaxf(contentW, SampSegsWidth(f, fs, segs)); }
        contentH = (fs + UI::S(8)) * (float)s_Dlg.lines.size();
        if(input) contentH += rowH + UI::S(16);
    }
    std::vector<SampSeg> cap; SampSplitColors(s_Dlg.caption.c_str(), IM_COL32(169, 196, 228, 255), cap);
    float w = fmaxf(contentW, SampSegsWidth(f, fs, cap)) + pad * 2;
    w = fmaxf(w, UI::S(620)); w = fminf(w, ds.x * 0.92f);
    float h = capH + pad + contentH + pad + btnH + pad;
    ImVec2 p0((ds.x - w) * 0.5f, (ds.y - h) * 0.5f);
    if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) p0.y = fmaxf(UI::S(10), fminf(p0.y, ds.y - VKeyboard::Height() - h - UI::S(10)));

    ImGui::SetNextWindowPos(p0); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.82f));
    if(ImGui::Begin("##SampDialog", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + capH), IM_COL32(25, 25, 30, 235), UI::S(6), ImDrawFlags_RoundCornersTop);
        SampDrawSegs(dl, f, fs, ImVec2(p0.x + pad, p0.y + (capH - fs) * 0.5f), cap);
        float y = p0.y + capH + pad;
        if(list)
        {
            auto drawRow = [&](const std::string& r, float ry, ImU32 base)
            {
                if(s_Dlg.style == DIALOG_STYLE_LIST) { SampSplitColors(r.c_str(), base, segs); SampDrawSegs(dl, f, fs, ImVec2(p0.x + pad, ry + (rowH - fs) * 0.5f), segs); return; }
                SampSplit(r, '\t', cells);
                float x = p0.x + pad;
                for(size_t c = 0; c < cells.size() && c < colW.size(); ++c)
                {
                    SampSplitColors(cells[c].c_str(), base, segs);
                    SampDrawSegs(dl, f, fs, ImVec2(x, ry + (rowH - fs) * 0.5f), segs);
                    x += colW[c] + UI::S(36);
                }
            };
            if(!s_Dlg.header.empty()) { drawRow(s_Dlg.header, y, IM_COL32(169, 196, 228, 255)); y += rowH; }
            float listH = contentH - (s_Dlg.header.empty() ? 0 : rowH);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + pad * 0.5f, y));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("##dlgrows", ImVec2(w - pad, listH), false, ImGuiWindowFlags_NoScrollbar);
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            for(int i = 0; i < (int)s_Dlg.rows.size(); ++i)
            {
                ImVec2 rp = ImGui::GetCursorScreenPos();
                ImGui::PushID(i);
                bool tap = ImGui::InvisibleButton("##row", ImVec2(w - pad, rowH));
                ImGui::PopID();
                bool dragged = ImGui::IsMouseDragging(0, UI::S(14));
                if(i == s_Dlg.sel) cdl->AddRectFilled(rp, ImVec2(rp.x + w - pad, rp.y + rowH), IM_COL32(169, 196, 228, 70), UI::S(4));
                std::swap(dl, cdl); drawRow(s_Dlg.rows[i], rp.y, IM_COL32_WHITE); std::swap(dl, cdl);
                if(tap && !dragged)
                {
                    if(i == s_Dlg.sel && MP_NowMs() - s_Dlg.openedAt > 400) { ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(3); CloseDialog(1); return; }   // 2nd tap = button 1
                    s_Dlg.sel = i;
                }
            }
            if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0, UI::S(14)))
            {   // drag the list with the finger
                float ddy = ImGui::GetIO().MouseDelta.y;
                if(ddy != 0) ImGui::SetScrollY(ImGui::GetScrollY() - ddy);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            y += listH;
        }
        else
        {
            for(auto& l : s_Dlg.lines)
            {
                SampSplitColors(l.c_str(), IM_COL32(169, 196, 228, 255), segs);
                SampDrawSegs(dl, f, fs, ImVec2(p0.x + pad, y), segs);
                y += fs + UI::S(8);
            }
            if(input)
            {
                y += UI::S(16);
                ImVec2 a(p0.x + pad, y), b(p0.x + w - pad, y + rowH);
                ImGui::SetCursorScreenPos(a);
                if(ImGui::InvisibleButton("##dlgin", ImVec2(b.x - a.x, rowH)))
                    VKeyboard::Open(s_Dlg.caption.c_str(), s_Dlg.input, (int)sizeof(s_Dlg.input) - 1, OnDialogInput);
                dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 30), UI::S(4));
                dl->AddRect(a, b, IM_COL32(169, 196, 228, 160), UI::S(4));
                std::string shown = s_Dlg.input;
                if(s_Dlg.style == DIALOG_STYLE_PASSWORD) shown.assign(shown.size(), '*');
                if(VKeyboard::IsOpen() && !VKeyboard::IsChat()) { shown = VKeyboard::GetText(); if(s_Dlg.style == DIALOG_STYLE_PASSWORD) shown.assign(shown.size(), '*'); }
                dl->AddText(f, fs, ImVec2(a.x + UI::S(12), a.y + (rowH - fs) * 0.5f), IM_COL32_WHITE, shown.c_str());
                y += rowH;
            }
        }
        // buttons (centred, like SA-MP)
        y = p0.y + h - pad - btnH;
        int nb = s_Dlg.b2.empty() ? 1 : 2;
        float bw = UI::S(220), gap = UI::S(24), bx = p0.x + (w - (bw * nb + gap * (nb - 1))) * 0.5f;
        for(int i = 0; i < nb; ++i)
        {
            const std::string& lab = i == 0 ? s_Dlg.b1 : s_Dlg.b2;
            ImVec2 a(bx + i * (bw + gap), y), b(a.x + bw, y + btnH);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(100 + i);
            bool hit = ImGui::InvisibleButton("##dlgbtn", ImVec2(bw, btnH));
            bool held = ImGui::IsItemActive();
            ImGui::PopID();
            dl->AddRectFilled(a, b, held ? IM_COL32(40, 40, 40, 245) : IM_COL32(0, 0, 0, 235), UI::S(2));   // 3.39: black, no outline, white text (SA-MP look)
            ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, lab.c_str());
            dl->AddText(f, fs, ImVec2(a.x + (bw - ts.x) * 0.5f, a.y + (btnH - ts.y) * 0.5f), IM_COL32_WHITE, lab.c_str());
            if(hit && MP_NowMs() - s_Dlg.openedAt > 250)
            {
                if(input && VKeyboard::IsOpen() && !VKeyboard::IsChat()) MP_CopyStr(s_Dlg.input, VKeyboard::GetText(), sizeof(s_Dlg.input));
                ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(3);
                CloseDialog(i == 0 ? 1 : 0);
                return;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ---- textdraws --------------------------------------------------------------------------------
// ids 0..63 = server textdraws (PKT_TEXTDRAW), 64..95 = this phone's own (GameText, race box...)
#define SAMP_TD_MAX   96
#define SAMP_TD_LOCAL 64
enum { TD_GAMETEXT_1 = SAMP_TD_LOCAL, TD_GAMETEXT_3, TD_GAMETEXT_4, TD_GAMETEXT_6, TD_EVENT_BOX, TD_EVENT_TOP, TD_TURF, TD_CLASS };
struct SampTd { bool used = false; PktTextDraw d; uint64_t until = 0; };
static SampTd s_Td[SAMP_TD_MAX];

static void TdSet(int id, const PktTextDraw& d, int ms = 0)
{
    if(id < 0 || id >= SAMP_TD_MAX) return;
    s_Td[id].used = d.show != 0; s_Td[id].d = d; s_Td[id].d.text[255] = 0;
    s_Td[id].until = ms > 0 ? MP_NowMs() + (uint64_t)ms : 0;
}
static void TdHide(int id) { if(id >= 0 && id < SAMP_TD_MAX) s_Td[id].used = false; }
static PktTextDraw TdMake(const char* text, float x, float y, float letterY, int font, int align, uint32_t color = 0xFFFFFFFF)
{
    PktTextDraw d; memset(&d, 0, sizeof(d));
    d.type = PKT_TEXTDRAW; d.show = 1; d.x = x; d.y = y; d.letterX = letterY * 0.4f; d.letterY = letterY;
    d.color = color; d.bgColor = 0x000000FF; d.font = (uint8_t)font; d.align = (uint8_t)align; d.outline = 2; d.proportional = 1;
    MP_CopyStr(d.text, text, sizeof(d.text));
    return d;
}
// SA-MP GameTextForPlayer styles: 1 = big bottom-right (passed), 3 = big centre (countdown),
// 4 = middle-bottom, 6 = top centre
static void GameText(const char* text, int ms, int style)
{
    PktTextDraw d;
    switch(style)
    {
    case 1:  d = TdMake(text, 600.0f, 300.0f, 1.9f, 3, 3, 0xE2C063FF); TdSet(TD_GAMETEXT_1, d, ms); break;
    case 3:  d = TdMake(text, 320.0f, 150.0f, 3.2f, 3, 2, 0xE2C063FF); TdSet(TD_GAMETEXT_3, d, ms); break;
    case 6:  d = TdMake(text, 320.0f,  60.0f, 1.3f, 1, 2, 0xFFFFFFFF); TdSet(TD_GAMETEXT_6, d, ms); break;
    default: d = TdMake(text, 320.0f, 330.0f, 1.3f, 1, 2, 0xFFFFFFFF); TdSet(TD_GAMETEXT_4, d, ms); break;
    }
}

// ~x~ codes -> coloured segments per line
static ImU32 SampTildeColor(char c, ImU32 cur)
{
    switch(c)
    {
    case 'r': return IM_COL32(180, 25, 29, 255);
    case 'g': return IM_COL32(54, 140, 44, 255);
    case 'b': return IM_COL32(50, 60, 127, 255);
    case 'y': return IM_COL32(226, 192, 99, 255);
    case 'p': return IM_COL32(168, 110, 252, 255);
    case 'w': return IM_COL32(225, 225, 225, 255);
    case 'l': return IM_COL32(0, 0, 0, 255);
    case 'h':   // lighter
    {
        int r = (cur >> IM_COL32_R_SHIFT) & 0xFF, g = (cur >> IM_COL32_G_SHIFT) & 0xFF, b = (cur >> IM_COL32_B_SHIFT) & 0xFF;
        r = r + 40 > 255 ? 255 : r + 40; g = g + 40 > 255 ? 255 : g + 40; b = b + 40 > 255 ? 255 : b + 40;
        return IM_COL32(r, g, b, 255);
    }
    }
    return cur;
}
static void SampTildeLines(const char* s, ImU32 base, std::vector<std::vector<SampSeg>>& lines)
{
    lines.clear(); lines.emplace_back();
    ImU32 col = base; std::string cur;
    auto flush = [&]() { if(!cur.empty()) { lines.back().push_back({ cur, col }); cur.clear(); } };
    for(const char* p = s; *p; ++p)
    {
        if(*p == '~' && p[1] && p[2] == '~')
        {
            char c = (char)tolower(p[1]);
            flush();
            if(c == 'n') lines.emplace_back();
            else col = SampTildeColor(c, c == 'h' ? col : base);
            p += 2; continue;
        }
        if(*p == '_') { cur += ' '; continue; }   // SA-MP: '_' = space in textdraws
        cur += *p;
    }
    flush();
}
static void RenderTextDraws(ImDrawList* dl)
{
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float kx = ds.x / 640.0f, ky = ds.y / 448.0f;
    const uint64_t now = MP_NowMs();
    std::vector<std::vector<SampSeg>> lines;
    for(int i = 0; i < SAMP_TD_MAX; ++i)
    {
        SampTd& t = s_Td[i];
        if(!t.used) continue;
        if(t.until && now >= t.until) { t.used = false; continue; }
        const PktTextDraw& d = t.d;
        ImFont* f = (d.font == 3 || d.font == 0) && UI::PricedownFont() ? UI::PricedownFont() : UI::Font();
        float fs = fmaxf(6.0f, d.letterY * 9.0f * ky * (f == UI::PricedownFont() ? 1.35f : 1.15f));
        uint32_t c = d.color;
        ImU32 base = IM_COL32(c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
        SampTildeLines(d.text, base, lines);
        float lh = fs * 1.05f, maxW = 0;
        std::vector<float> lw;
        for(auto& l : lines) { float w = SampSegsWidth(f, fs, l); lw.push_back(w); maxW = fmaxf(maxW, w); }
        float x = d.x * kx, y = d.y * ky;
        float bx0 = d.align == 2 ? x - maxW * 0.5f : d.align == 3 ? x - maxW : x;
        if(d.useBox)
        {
            float bw = d.boxX > 0 ? fabsf(d.boxX * kx - x) : maxW;
            float b0 = d.align == 2 ? x - bw * 0.5f : d.align == 3 ? x - bw : x;
            uint32_t bc = d.boxColor;
            dl->AddRectFilled(ImVec2(b0 - UI::S(10), y - UI::S(6)), ImVec2(b0 + fmaxf(bw, maxW) + UI::S(10), y + lh * lines.size() + UI::S(6)), IM_COL32(bc >> 24, (bc >> 16) & 0xFF, (bc >> 8) & 0xFF, bc & 0xFF));
        }
        for(size_t li = 0; li < lines.size(); ++li)
        {
            float lx = d.align == 2 ? x - lw[li] * 0.5f : d.align == 3 ? x - lw[li] : x;
            float ly = y + lh * li;
            if(d.outline || d.shadow)
            {
                float o = fmaxf(1.0f, fs * 0.06f) * (d.outline ? d.outline : 1);
                std::vector<SampSeg> sh = lines[li]; for(auto& s : sh) s.col = IM_COL32(0, 0, 0, 255);
                if(d.outline) { for(int dx = -1; dx <= 1; ++dx) for(int dy = -1; dy <= 1; ++dy) if(dx || dy) SampDrawSegs(dl, f, fs, ImVec2(lx + dx * o, ly + dy * o), sh); }
                else SampDrawSegs(dl, f, fs, ImVec2(lx + o * d.shadow, ly + o * d.shadow), sh);
            }
            SampDrawSegs(dl, f, fs, ImVec2(lx, ly), lines[li]);
        }
        (void)bx0;
    }
}

// ---- chat bubbles (SetPlayerChatBubble: chat text over the head for a few seconds) ------------
// 3.01: off by default; MP Launcher > Settings > Chat > "Chat bubbles" turns them on (or /bubbles).
struct SampBubble { char text[MP_CHAT_LEN]; uint64_t until; };
static SampBubble s_Bubble[MP_MAX_PLAYERS];
static void SampOnChat(int id, const char* text)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !text || !*text || text[0] == '/') return;
    MP_CopyStr(s_Bubble[id].text, text, sizeof(s_Bubble[id].text));
    s_Bubble[id].until = MP_NowMs() + 6000 + (uint64_t)strlen(text) * 60;
}
static void DrawOneBubble(ImDrawList* dl, const Vec3& pos, const char* text, uint64_t until)
{
    Vec3 head = pos; head.z += 1.55f;   // above the name tag
    float sx, sy, depth;
    if(!Game::WorldToScreen(head, sx, sy, depth)) return;
    const uint64_t now = MP_NowMs();
    float a = until - now < 500 ? (float)(until - now) / 500.0f : 1.0f;
    ImFont* f = UI::Font();
    const float fs = UI::S(26.0f), maxW = UI::S(420.0f);
    // word wrap (colour codes kept per line)
    std::vector<std::string> lines; std::string cur, word;
    auto width = [&](const std::string& s) { std::vector<SampSeg> g; SampSplitColors(s.c_str(), IM_COL32_WHITE, g); return SampSegsWidth(f, fs, g); };
    std::string t = text; t += ' ';
    for(char ch : t)
    {
        if(ch != ' ') { word += ch; continue; }
        std::string next = cur.empty() ? word : cur + " " + word;
        if(!cur.empty() && width(next) > maxW) { lines.push_back(cur); cur = word; } else cur = next;
        word.clear();
        if(lines.size() >= 3) break;
    }
    if(!cur.empty() && lines.size() < 4) lines.push_back(cur);
    float y = sy - fs * lines.size();
    std::vector<SampSeg> segs;
    for(auto& l : lines)
    {
        SampSplitColors(l.c_str(), IM_COL32_WHITE, segs);
        for(auto& g : segs) g.col = (g.col & 0x00FFFFFF) | ((ImU32)(255 * a) << 24);
        float w = SampSegsWidth(f, fs, segs);
        std::vector<SampSeg> sh = segs; for(auto& g : sh) g.col = IM_COL32(0, 0, 0, (int)(255 * a));
        const float o = fmaxf(1.0f, fs * 0.05f);
        for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) SampDrawSegs(dl, f, fs, ImVec2(sx - w * 0.5f + ox * o, y + oy * o), sh);
        SampDrawSegs(dl, f, fs, ImVec2(sx - w * 0.5f, y), segs);
        y += fs;
    }
}
static void RenderBubbles(ImDrawList* dl)
{
    if(!settings.chatBubbles || !myDrawValid) return;   // 3.03: only the player's setting (Settings > Chat)
    const uint64_t now = MP_NowMs();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        SampBubble& b = s_Bubble[i];
        if(!b.until || now >= b.until) { b.until = 0; continue; }
        if(i == myId) { if(settings.showOwnTag) DrawOneBubble(dl, myDrawPos, b.text, b.until); continue; }
        const RemotePlayer& p = players[i];
        if(!p.active || !p.drawValid || p.tagBlocked) continue;
        float dx = p.drawPos.x - myDrawPos.x, dy = p.drawPos.y - myDrawPos.y, dz = p.drawPos.z - myDrawPos.z;
        if(dx * dx + dy * dy + dz * dz > settings.nametagDistance * settings.nametagDistance) continue;
        DrawOneBubble(dl, p.drawPos, b.text, b.until);
    }
}

// ---- the other players' races / derbies (PKT_EVENT) ----------------------------------------
struct SampPart { bool on = false; uint8_t kind = 0, idx = 0, state = 0, cp = 0, slot = 0; uint32_t ms = 0; float dist = 0; uint64_t seen = 0, goAt = 0, finishedAt = 0; };
static SampPart s_Part[MP_MAX_PLAYERS];
static uint8_t  s_Team[MP_MAX_PLAYERS];    // the other players' class teams (PKT_TEAM)
static bool     s_bServer301 = false;      // the server relays 3.01 packets (got PKT_TURF)

static void SendEvent(int kind, int idx, int state, int cp, int slot, uint32_t ms, float dist)
{
    if(client.GetState() != NET_CONNECTED) return;
    PktEvent e; memset(&e, 0, sizeof(e));
    e.type = PKT_EVENT; e.id = myId; e.kind = (uint8_t)kind; e.idx = (uint8_t)idx; e.state = (uint8_t)state;
    e.cp = (uint8_t)cp; e.slot = (uint8_t)slot; e.ms = ms; e.dist = dist;
    client.Send(&e, sizeof(e));
}
static void GamesOnServerEvent(const PktEvent& e);   // mp_games.inc: race queue / race start
static void SampOnEvent(const PktEvent& e)
{
    if(e.id == MP_EV_SERVER) { GamesOnServerEvent(e); return; }   // 3.01: from the server itself
    if(e.idx == MP_EV_QUEUE) return;                               // someone's queue packet (old server relays it)
    if(e.id >= MP_MAX_PLAYERS || e.id == myId) return;
    SampPart& p = s_Part[e.id];
    uint64_t now = MP_NowMs();
    if(e.state == EVS_LEAVE) { p = SampPart(); return; }
    if(!p.on || p.kind != e.kind || p.idx != e.idx) { p = SampPart(); p.on = true; p.kind = e.kind; p.idx = e.idx; }
    p.state = e.state; p.cp = e.cp; p.slot = e.slot; p.dist = e.dist; p.seen = now;
    if(e.state == EVS_LOBBY) { p.goAt = now + e.ms; p.finishedAt = 0; }
    if(e.state == EVS_FINISH) { p.ms = e.ms; if(!p.finishedAt) p.finishedAt = now; }
}
// how many others are in this event (lobby / racing / finished), seen in the last few seconds
static int EventOthers(int kind, int idx, bool includeOut = false)
{
    int n = 0; uint64_t now = MP_NowMs();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const SampPart& p = s_Part[i];
        if(!p.on || p.kind != kind || p.idx != idx || !players[i].active) continue;
        if(p.state == EVS_OUT && !includeOut) continue;
        if(p.state != EVS_FINISH && now - p.seen > 6000) continue;
        ++n;
    }
    return n;
}
// an open lobby somebody else made: GO time (0 = none)
static uint64_t EventLobbyGo(int kind, int idx, int* slotsUsed)
{
    uint64_t go = 0, now = MP_NowMs(); int used = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const SampPart& p = s_Part[i];
        if(!p.on || p.kind != kind || p.idx != idx || p.state != EVS_LOBBY || !players[i].active || now - p.seen > 3000) continue;
        if(p.goAt > now + 1500 && (!go || p.goAt < go)) go = p.goAt;
        if(p.slot + 1 > used) used = p.slot + 1;
    }
    if(slotsUsed) *slotsUsed = used;
    return go;
}
static int OthersOnline() { int n = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(i != myId && players[i].active) ++n; return n; }
static const char* Ordinal(int n) { int m = n % 100; if(m >= 11 && m <= 13) return "th"; switch(n % 10) { case 1: return "st"; case 2: return "nd"; case 3: return "rd"; } return "th"; }
