// =============================================================================================
// 2.25 MP Launcher: the Server page as an almost full-screen window (95% of the screen, 40%
// transparent), laid out like a desktop SA-MP launcher:
//   title bar  : MP Launcher  v3.04.6 ........................................... [X]
//   toolbar    : [Search server] [> Connect] [+] [trash] [refresh] [gear] ..... [Nickname] [sliders] [i]
//   sidebar    : SERVERS Internet / Favorites / Add server   (2.37: "Hosted" merged into Add server)
//                ADMIN   Players (kick + ban) / Bans
//                SETTINGS Connection / Display / Chat / Player / (sections from other mods)
//   centre     : HostName | Players | Ping | Mode | Region   (tap = select, tap again = join)
//   right      : Player | Score   and   Rule | Value of the selected server
// Plus the kick / ban dialog: "You are banned from the server!" Ban date / Administrator / Reason.
// The administrator is the one who hosts the server (this phone's player name).
// =============================================================================================

#include "ui/server_style.h"   // 4.03: country flags + gamemode colours

enum { LT_INTERNET = 0, LT_FAVORITES, LT_HOSTED, LT_ADD, LT_PLAYERS, LT_BANS, LT_SRVMGMT,   // 3.53: + Server Management
       LT_FRIENDS, LT_CHATS, LT_PARTY, LT_COMMUNITY, LT_INBOX,                           // 3.63: social (mp_social.inc)
       LT_CLOUD,                                                                         // 4.02: Cloud Storage (downloads)
       LT_LAN,                                                                           // 4.03: Wi-Fi servers only (CS:GO style top tab)
       LT_SECTIONS };
static int   s_nLTab = LT_INTERNET;
static int   s_nInetSub = 0;   // 3.58: Internet = PUBLIC (default + custom servers) || FEATURED (the 24/7 Broker servers)
static bool  s_bLauncherCloseReq = false;   // 3.39: close the launcher (e.g. Server settings opens a dialog in game)
static int   s_nLSel = -1;                         // selected row
static char  s_szLSelKey[48] = "";                 // name|port of the selected row (rows move)
static char  s_szSearch[32] = "";
static int   s_nSortCol = -1;                      // -1 = list order, 0 name, 1 players, 2 ping, 3 mode, 4 region
static bool  s_bSortAsc = true;
static char  s_szKickReason[MP_REASON_LEN] = "";
static int   s_nMgmtSel = -1;
static int   s_nBanSel = -1;
static char  s_szBanName[MP_NAME_LEN] = "";

struct KickInfo { bool show; bool banned; uint32_t when; char admin[MP_NAME_LEN]; char reason[MP_REASON_LEN]; char server[32]; };
static KickInfo s_Kick = {};

static void ShowKicked(const PktKicked& k)
{
    s_Kick.show = true; s_Kick.banned = k.banned != 0; s_Kick.when = k.when;
    MP_CopyStr(s_Kick.admin, k.admin, sizeof(s_Kick.admin));
    MP_CopyStr(s_Kick.reason, k.reason, sizeof(s_Kick.reason));
    MP_CopyStr(s_Kick.server, client.GetServerName()[0] ? client.GetServerName() : (s_szJobName[0] ? s_szJobName : "the server"), sizeof(s_Kick.server));
}

// ---- small drawing helpers -------------------------------------------------------------------
enum { IC_PLAY, IC_PLUS, IC_TRASH, IC_REFRESH, IC_GEAR, IC_SLIDERS, IC_INFO, IC_CLOSE, IC_GLOBE, IC_HEART,
       IC_BOX, IC_USERS, IC_BAN, IC_LIST, IC_SEARCH, IC_CHAT, IC_USER, IC_STOP, IC_REGION,
       IC_BELL, IC_USERPLUS, IC_PARTY, IC_FLAG, IC_SEND, IC_CHECK, IC_CLOUD };   // 3.63, 4.02 cloud

// ---- 4.03: CS:GO community browser style (was Valorant 3.63) - same layout, square corners everywhere ----
// slate glass panels, thin light lines, white selection, blue accent, green CONNECT
static const ImU32 kVRed    = IM_COL32(222, 74, 60, 255);
static const ImU32 kVRedDim = IM_COL32(222, 74, 60, 60);
static const ImU32 kVTeal   = IM_COL32(126, 178, 226, 255);   // CS:GO blue (accent / selected)
static const ImU32 kVTealDim= IM_COL32(126, 178, 226, 40);
static const ImU32 kVText   = IM_COL32(226, 229, 233, 255);
static const ImU32 kVDim    = IM_COL32(150, 157, 166, 255);
static const ImU32 kVLine   = IM_COL32(255, 255, 255, 30);
static const ImU32 kVPanel  = IM_COL32(52, 58, 66, 150);
static const ImU32 kVDark   = IM_COL32(26, 29, 34, 255);
static const ImU32 kVGreen  = IM_COL32(76, 160, 48, 255);      // CONNECT
static inline ImU32 VAlpha(ImU32 c, float a) { int al = (int)(((c >> IM_COL32_A_SHIFT) & 0xFF) * a); return (c & ~IM_COL32_A_MASK) | ((ImU32)ImClamp(al, 0, 255) << IM_COL32_A_SHIFT); }
// 4.03: plain square box (the old cut corners are gone, the cut size is ignored)
static void VChamfer(ImDrawList* dl, ImVec2 a, ImVec2 b, float /*cut*/, ImU32 fill, ImU32 border = 0, float thick = 0)
{
    if(fill) dl->AddRectFilled(a, b, fill, 0.0f);
    if(border) dl->AddRect(a, b, border, 0.0f, 0, thick > 0 ? thick : UI::S(1.0f));
}
// 4.03: square tab / chip (was a pill)
static void VPill(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 fill, ImU32 border = 0)
{
    if(fill) dl->AddRectFilled(a, b, fill, 0.0f);
    if(border) dl->AddRect(a, b, border, 0.0f, 0, UI::S(1.0f));
}
static void VTicks(ImDrawList*, ImVec2, ImVec2, ImU32, float) {}   // 4.03: no corner brackets in the CS:GO look
// a soft vertical slate gradient (CS:GO panel)
static void VSlate(ImDrawList* dl, ImVec2 a, ImVec2 b, float alpha = 1.0f)
{
    dl->AddRectFilledMultiColor(a, b, IM_COL32(64, 72, 82, (int)(235 * alpha)), IM_COL32(64, 72, 82, (int)(235 * alpha)),
                                      IM_COL32(38, 43, 50, (int)(245 * alpha)), IM_COL32(38, 43, 50, (int)(245 * alpha)));
}
// 4.05.1: the CS:GO backdrop = ui_bg.jpg (blurred night city, built into the .so) covering the screen + a
// dark tint; pages are VGlass (see-through slate) on top, so the picture shows through every page
static void LImageCover(ImDrawList* dl, void* tex, ImVec2 uvMax, ImVec2 a, ImVec2 b, ImU32 tint);
static void VBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    ImVec2 uv(1, 1);
    if(void* t = HUD::Texture("ui_bg.jpg", &uv))
    {
        LImageCover(dl, t, uv, a, b, IM_COL32(255, 255, 255, 255));
        dl->AddRectFilled(a, b, IM_COL32(8, 10, 14, 95));
    }
    else dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 150));
}
static void VGlass(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    dl->AddRectFilledMultiColor(a, b, IM_COL32(58, 66, 76, 95), IM_COL32(58, 66, 76, 95), IM_COL32(18, 21, 26, 150), IM_COL32(18, 21, 26, 150));
}
// ImGui widgets inside the launcher pages (settings, add server, ...) get the same look
static void VPushWidgetStyle()
{
    ImGui::PushStyleColor(ImGuiCol_Button,         ImVec4(0.30f, 0.34f, 0.39f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.42f, 0.47f, 0.53f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.49f, 0.70f, 0.89f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.13f, 0.15f, 0.18f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.20f, 0.24f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.22f, 0.25f, 0.29f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,      ImVec4(0.49f, 0.70f, 0.89f, 1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,     ImVec4(0.49f, 0.70f, 0.89f, 1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.80f, 0.95f, 1));
    ImGui::PushStyleColor(ImGuiCol_Header,         ImVec4(0.49f, 0.70f, 0.89f, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.49f, 0.70f, 0.89f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.49f, 0.70f, 0.89f, 0.42f));
    ImGui::PushStyleColor(ImGuiCol_Separator,      ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(1, 1, 1, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0.89f, 0.90f, 0.91f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);   // 4.03: square boxes
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, UI::S(1));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
}
static void VPopWidgetStyle() { ImGui::PopStyleVar(6); ImGui::PopStyleColor(15); }

static void LIcon(ImDrawList* dl, int k, ImVec2 c, float r, ImU32 col)
{
    float t = ImMax(1.5f, r * 0.14f);
    switch(k)
    {
    case IC_PLAY:  dl->AddTriangle(ImVec2(c.x - r * 0.45f, c.y - r * 0.6f), ImVec2(c.x + r * 0.6f, c.y), ImVec2(c.x - r * 0.45f, c.y + r * 0.6f), col, t); break;
    case IC_STOP:  dl->AddRect(ImVec2(c.x - r * 0.5f, c.y - r * 0.5f), ImVec2(c.x + r * 0.5f, c.y + r * 0.5f), col, r * 0.12f, 0, t); break;
    case IC_PLUS:  dl->AddLine(ImVec2(c.x - r * 0.65f, c.y), ImVec2(c.x + r * 0.65f, c.y), col, t); dl->AddLine(ImVec2(c.x, c.y - r * 0.65f), ImVec2(c.x, c.y + r * 0.65f), col, t); break;
    case IC_TRASH:
        dl->AddRect(ImVec2(c.x - r * 0.42f, c.y - r * 0.38f), ImVec2(c.x + r * 0.42f, c.y + r * 0.7f), col, r * 0.1f, 0, t);
        dl->AddLine(ImVec2(c.x - r * 0.62f, c.y - r * 0.4f), ImVec2(c.x + r * 0.62f, c.y - r * 0.4f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.18f, c.y - r * 0.62f), ImVec2(c.x + r * 0.18f, c.y - r * 0.62f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.14f, c.y - r * 0.15f), ImVec2(c.x - r * 0.14f, c.y + r * 0.45f), col, t * 0.8f);
        dl->AddLine(ImVec2(c.x + r * 0.14f, c.y - r * 0.15f), ImVec2(c.x + r * 0.14f, c.y + r * 0.45f), col, t * 0.8f);
        break;
    case IC_REFRESH:
        dl->PathArcTo(c, r * 0.6f, -IM_PI * 0.35f, IM_PI * 1.45f, 24); dl->PathStroke(col, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.30f, c.y - r * 0.92f), ImVec2(c.x + r * 0.70f, c.y - r * 0.40f), ImVec2(c.x + r * 0.12f, c.y - r * 0.30f), col);
        break;
    case IC_GEAR:
        for(int i = 0; i < 8; ++i) { float a = i * IM_PI / 4; dl->AddLine(ImVec2(c.x + cosf(a) * r * 0.45f, c.y + sinf(a) * r * 0.45f), ImVec2(c.x + cosf(a) * r * 0.72f, c.y + sinf(a) * r * 0.72f), col, t * 1.6f); }
        dl->AddCircle(c, r * 0.48f, col, 20, t); dl->AddCircle(c, r * 0.18f, col, 12, t); break;
    case IC_SLIDERS:
        for(int i = 0; i < 2; ++i)
        {
            float y = c.y + (i ? r * 0.3f : -r * 0.3f), kx = c.x + (i ? -r * 0.25f : r * 0.25f);
            dl->AddLine(ImVec2(c.x - r * 0.7f, y), ImVec2(c.x + r * 0.7f, y), col, t);
            dl->AddCircleFilled(ImVec2(kx, y), r * 0.2f, IM_COL32(20, 20, 22, 255)); dl->AddCircle(ImVec2(kx, y), r * 0.2f, col, 12, t);
        }
        break;
    case IC_INFO:
        dl->AddCircle(c, r * 0.7f, col, 24, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.32f), t * 0.75f, col);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.08f), ImVec2(c.x, c.y + r * 0.38f), col, t); break;
    case IC_CLOSE: dl->AddLine(ImVec2(c.x - r * 0.5f, c.y - r * 0.5f), ImVec2(c.x + r * 0.5f, c.y + r * 0.5f), col, t); dl->AddLine(ImVec2(c.x + r * 0.5f, c.y - r * 0.5f), ImVec2(c.x - r * 0.5f, c.y + r * 0.5f), col, t); break;
    case IC_GLOBE:
        dl->AddCircle(c, r * 0.7f, col, 24, t);
        for(int i = 0; i <= 20; ++i) { float a = i * IM_PI * 2 / 20; dl->PathLineTo(ImVec2(c.x + cosf(a) * r * 0.3f, c.y + sinf(a) * r * 0.7f)); }
        dl->PathStroke(col, 0, t * 0.8f);
        dl->AddLine(ImVec2(c.x - r * 0.7f, c.y), ImVec2(c.x + r * 0.7f, c.y), col, t * 0.8f); break;
    case IC_HEART:
    {
        ImVec2 p0(c.x, c.y + r * 0.62f);
        dl->PathLineTo(p0);
        dl->PathBezierCubicCurveTo(ImVec2(c.x - r * 0.95f, c.y), ImVec2(c.x - r * 0.6f, c.y - r * 0.85f), ImVec2(c.x, c.y - r * 0.3f), 12);
        dl->PathBezierCubicCurveTo(ImVec2(c.x + r * 0.6f, c.y - r * 0.85f), ImVec2(c.x + r * 0.95f, c.y), p0, 12);
        dl->PathStroke(col, ImDrawFlags_Closed, t); break;
    }
    case IC_CLOUD:   // 4.02: Cloud Storage
        dl->PathArcTo(ImVec2(c.x - r * 0.38f, c.y + r * 0.12f), r * 0.32f, IM_PI * 0.5f, IM_PI * 1.5f, 10);
        dl->PathArcTo(ImVec2(c.x - r * 0.05f, c.y - r * 0.18f), r * 0.40f, IM_PI * 1.05f, IM_PI * 1.95f, 12);
        dl->PathArcTo(ImVec2(c.x + r * 0.40f, c.y + r * 0.10f), r * 0.34f, IM_PI * 1.5f, IM_PI * 2.5f, 10);
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.02f), ImVec2(c.x, c.y + r * 0.62f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.2f, c.y + r * 0.40f), ImVec2(c.x, c.y + r * 0.62f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.2f, c.y + r * 0.40f), ImVec2(c.x, c.y + r * 0.62f), col, t); break;
    case IC_BOX:
        dl->AddQuad(ImVec2(c.x, c.y - r * 0.7f), ImVec2(c.x + r * 0.65f, c.y - r * 0.35f), ImVec2(c.x, c.y), ImVec2(c.x - r * 0.65f, c.y - r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.65f, c.y - r * 0.35f), ImVec2(c.x - r * 0.65f, c.y + r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.65f, c.y - r * 0.35f), ImVec2(c.x + r * 0.65f, c.y + r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + r * 0.7f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.65f, c.y + r * 0.35f), ImVec2(c.x, c.y + r * 0.7f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.65f, c.y + r * 0.35f), ImVec2(c.x, c.y + r * 0.7f), col, t); break;
    case IC_USERS:
        dl->AddCircle(ImVec2(c.x - r * 0.22f, c.y - r * 0.3f), r * 0.24f, col, 14, t);
        dl->PathArcTo(ImVec2(c.x - r * 0.22f, c.y + r * 0.55f), r * 0.45f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
        dl->AddCircle(ImVec2(c.x + r * 0.35f, c.y - r * 0.22f), r * 0.18f, col, 12, t * 0.8f);
        dl->PathArcTo(ImVec2(c.x + r * 0.38f, c.y + r * 0.5f), r * 0.32f, IM_PI * 1.15f, IM_PI * 2, 10); dl->PathStroke(col, 0, t * 0.8f); break;
    case IC_USER:
        dl->AddCircle(ImVec2(c.x, c.y - r * 0.28f), r * 0.28f, col, 16, t);
        dl->PathArcTo(ImVec2(c.x, c.y + r * 0.62f), r * 0.55f, IM_PI, IM_PI * 2, 14); dl->PathStroke(col, 0, t); break;
    case IC_BAN:
        dl->AddCircle(c, r * 0.65f, col, 24, t);
        dl->AddLine(ImVec2(c.x - r * 0.46f, c.y + r * 0.46f), ImVec2(c.x + r * 0.46f, c.y - r * 0.46f), col, t); break;
    case IC_LIST:
        for(int i = -1; i <= 1; ++i) { float y = c.y + i * r * 0.4f; dl->AddCircleFilled(ImVec2(c.x - r * 0.55f, y), t * 0.7f, col); dl->AddLine(ImVec2(c.x - r * 0.3f, y), ImVec2(c.x + r * 0.65f, y), col, t); }
        break;
    case IC_SEARCH:
        dl->AddCircle(ImVec2(c.x - r * 0.12f, c.y - r * 0.12f), r * 0.42f, col, 18, t);
        dl->AddLine(ImVec2(c.x + r * 0.2f, c.y + r * 0.2f), ImVec2(c.x + r * 0.62f, c.y + r * 0.62f), col, t * 1.2f); break;
    case IC_CHAT:
        dl->AddRect(ImVec2(c.x - r * 0.65f, c.y - r * 0.5f), ImVec2(c.x + r * 0.65f, c.y + r * 0.3f), col, r * 0.2f, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.3f, c.y + r * 0.28f), ImVec2(c.x - r * 0.05f, c.y + r * 0.28f), ImVec2(c.x - r * 0.38f, c.y + r * 0.62f), col); break;
    case IC_BELL:
    {   // 3.63: notifications
        dl->PathArcTo(ImVec2(c.x, c.y - r * 0.1f), r * 0.45f, IM_PI, IM_PI * 2, 16);
        dl->PathLineTo(ImVec2(c.x + r * 0.45f, c.y + r * 0.3f)); dl->PathLineTo(ImVec2(c.x + r * 0.62f, c.y + r * 0.45f));
        dl->PathLineTo(ImVec2(c.x - r * 0.62f, c.y + r * 0.45f)); dl->PathLineTo(ImVec2(c.x - r * 0.45f, c.y + r * 0.3f));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.66f), t * 1.1f, col);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.72f), ImVec2(c.x, c.y - r * 0.55f), col, t);
        break;
    }
    case IC_USERPLUS:
        dl->AddCircle(ImVec2(c.x - r * 0.18f, c.y - r * 0.28f), r * 0.26f, col, 16, t);
        dl->PathArcTo(ImVec2(c.x - r * 0.18f, c.y + r * 0.6f), r * 0.5f, IM_PI, IM_PI * 2, 14); dl->PathStroke(col, 0, t);
        dl->AddLine(ImVec2(c.x + r * 0.42f, c.y - r * 0.2f), ImVec2(c.x + r * 0.82f, c.y - r * 0.2f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.62f, c.y - r * 0.4f), ImVec2(c.x + r * 0.62f, c.y), col, t); break;
    case IC_PARTY:   // a crown (the party leader) over 3 heads
        dl->PathLineTo(ImVec2(c.x - r * 0.6f, c.y + r * 0.45f)); dl->PathLineTo(ImVec2(c.x - r * 0.7f, c.y - r * 0.35f)); dl->PathLineTo(ImVec2(c.x - r * 0.3f, c.y));
        dl->PathLineTo(ImVec2(c.x, c.y - r * 0.6f)); dl->PathLineTo(ImVec2(c.x + r * 0.3f, c.y)); dl->PathLineTo(ImVec2(c.x + r * 0.7f, c.y - r * 0.35f));
        dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y + r * 0.45f)); dl->PathStroke(col, ImDrawFlags_Closed, t); break;
    case IC_FLAG:    // community
        dl->AddLine(ImVec2(c.x - r * 0.5f, c.y - r * 0.7f), ImVec2(c.x - r * 0.5f, c.y + r * 0.7f), col, t);
        dl->PathLineTo(ImVec2(c.x - r * 0.5f, c.y - r * 0.65f)); dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y - r * 0.45f));
        dl->PathLineTo(ImVec2(c.x + r * 0.25f, c.y - r * 0.15f)); dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y + r * 0.15f)); dl->PathLineTo(ImVec2(c.x - r * 0.5f, c.y + r * 0.1f));
        dl->PathStroke(col, 0, t); break;
    case IC_SEND:
        dl->PathLineTo(ImVec2(c.x - r * 0.65f, c.y - r * 0.6f)); dl->PathLineTo(ImVec2(c.x + r * 0.7f, c.y)); dl->PathLineTo(ImVec2(c.x - r * 0.65f, c.y + r * 0.6f)); dl->PathLineTo(ImVec2(c.x - r * 0.35f, c.y));
        dl->PathStroke(col, ImDrawFlags_Closed, t); dl->AddLine(ImVec2(c.x - r * 0.35f, c.y), ImVec2(c.x + r * 0.2f, c.y), col, t); break;
    case IC_CHECK:
        dl->AddLine(ImVec2(c.x - r * 0.55f, c.y), ImVec2(c.x - r * 0.15f, c.y + r * 0.42f), col, t * 1.3f);
        dl->AddLine(ImVec2(c.x - r * 0.15f, c.y + r * 0.42f), ImVec2(c.x + r * 0.6f, c.y - r * 0.45f), col, t * 1.3f); break;
    case IC_REGION:
    {   // 3.25.1: map pin = the region switch (round head, point at the bottom, a hole in the middle)
        const ImVec2 hc(c.x, c.y - r * 0.2f); const float pr = r * 0.46f, tipY = c.y + r * 0.75f;
        const float ta = acosf(pr / (tipY - hc.y));   // where the two straight sides leave the head
        dl->PathArcTo(hc, pr, IM_PI * 0.5f + ta, IM_PI * 2.5f - ta, 24);
        dl->PathLineTo(ImVec2(c.x, tipY));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircle(hc, pr * 0.38f, col, 12, t);
        break;
    }
    }
}

static void LPadlock(ImDrawList* dl, ImVec2 c, float r, bool locked, ImU32 col)
{
    float t = ImMax(1.5f, r * 0.14f);
    dl->AddRect(ImVec2(c.x - r * 0.5f, c.y - r * 0.05f), ImVec2(c.x + r * 0.5f, c.y + r * 0.62f), col, r * 0.12f, 0, t);
    if(locked) { dl->PathArcTo(ImVec2(c.x, c.y - r * 0.12f), r * 0.32f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
                 dl->AddLine(ImVec2(c.x - r * 0.32f, c.y - r * 0.12f), ImVec2(c.x - r * 0.32f, c.y - r * 0.05f), col, t); dl->AddLine(ImVec2(c.x + r * 0.32f, c.y - r * 0.12f), ImVec2(c.x + r * 0.32f, c.y - r * 0.05f), col, t); }
    else       { dl->PathArcTo(ImVec2(c.x + r * 0.3f, c.y - r * 0.2f), r * 0.32f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
                 dl->AddLine(ImVec2(c.x - r * 0.02f, c.y - r * 0.2f), ImVec2(c.x - r * 0.02f, c.y - r * 0.05f), col, t); }
}

// drag a child window with the finger to scroll it
static void LDragScroll()
{
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0, UI::S(14)))
    {
        float dy = ImGui::GetIO().MouseDelta.y;
        if(dy != 0) ImGui::SetScrollY(ImGui::GetScrollY() - dy);
    }
}

// toolbar button: icon + optional label, flat, white highlight while pressed
#define L_PRESS_ON_CLICK ((ImGuiButtonFlags)(1 << 4))   // 4.05.2: ImGuiButtonFlags_PressedOnClick (imgui_internal.h)
static bool LToolButton(const char* id, int icon, const char* label, float h, bool enabled = true, bool on = false)
{
    float isz = h * 0.42f;
    float w = label ? (h * 0.55f + isz + ImGui::CalcTextSize(label).x + h * 0.35f) : h;
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if(!enabled) ImGui::BeginDisabled();
    // 4.05.2: fires on the press (not the release) - the toolbar sits over the GTA weapon icon and a
    // release could get lost there, so refresh / region / settings / add server did nothing
    bool hit = ImGui::InvisibleButton("##tb", ImVec2(w, h), L_PRESS_ON_CLICK);
    if(!enabled) ImGui::EndDisabled();
    // 4.05.5: second hit test. The region icon (and the other toolbar icons) still did nothing for some
    // players: ImGui gave the press to some other window / item. A press inside this button where the
    // launcher is the top window (nothing visible of ours over it) counts as a tap now, whatever ImGui
    // thinks is hovered. ImGui's own hit (same frame) wins, so a tap never counts twice.
    if(!hit && enabled && ImGui::IsMouseClicked(0) && !VKeyboard::IsOpen())
    {
        const ImVec2 m = ImGui::GetIO().MouseClickedPos[0];
        const char* above = "";
        if(m.x >= p.x && m.y >= p.y && m.x < p.x + w && m.y < p.y + h && FloatMenu::LauncherOwnsPoint(m.x, m.y, &above))
        {
            static int s_nRawLogs = 0;
            if(s_nRawLogs < 8)
            {
                ++s_nRawLogs;
                logger->Info("4.05.6: toolbar '%s' tapped but ImGui gave it to %s - used anyway", id, FloatMenu::HoverInfo());
            }
            hit = true;
        }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 3.63: Valorant look - a thin chamfered frame, teal while selected, light fill while pressed
    const ImVec2 q(p.x + w, p.y + h), a1(p.x + UI::S(3), p.y + UI::S(5)), b1(q.x - UI::S(3), q.y - UI::S(5));
    if(on) VChamfer(dl, a1, b1, UI::S(9), kVTealDim, VAlpha(kVTeal, 0.85f), UI::S(1.5f));
    else if(enabled && ImGui::IsItemActive()) VChamfer(dl, a1, b1, UI::S(9), IM_COL32(236, 232, 225, 30), kVLine);
    else if(label && enabled) VChamfer(dl, a1, b1, UI::S(9), 0, IM_COL32(236, 232, 225, 22));
    ImU32 col = enabled ? (on ? kVTeal : kVText) : IM_COL32(110, 118, 126, 255);
    float cx = label ? p.x + h * 0.32f + isz * 0.5f : p.x + w * 0.5f;
    LIcon(dl, icon, ImVec2(cx, p.y + h * 0.5f), isz * 0.75f, col);
    if(label) dl->AddText(ImVec2(cx + isz * 0.5f + h * 0.22f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    ImGui::PopID();
    return hit && enabled;
}

// framed "input" box that opens the game keyboard
static bool LInputBox(const char* id, const char* value, const char* placeholder, float w, float h, int icon = -1)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton("##ib", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 3.63: Valorant field - navy glass, chamfered, a teal underline while touched
    VChamfer(dl, p, ImVec2(p.x + w, p.y + h), UI::S(10), IM_COL32(10, 18, 26, 150), ImGui::IsItemActive() ? VAlpha(kVTeal, 0.9f) : IM_COL32(236, 232, 225, 40), UI::S(1.5f));
    dl->AddLine(ImVec2(p.x + UI::S(12), p.y + h - UI::S(1.5f)), ImVec2(p.x + w - UI::S(14), p.y + h - UI::S(1.5f)), ImGui::IsItemActive() ? kVTeal : IM_COL32(236, 232, 225, 18), UI::S(2));
    float tx = p.x + UI::S(18);
    if(icon >= 0) { LIcon(dl, icon, ImVec2(p.x + UI::S(30), p.y + h * 0.5f), h * 0.26f, kVDim); tx = p.x + UI::S(56); }
    dl->PushClipRect(p, ImVec2(p.x + w - UI::S(8), p.y + h), true);
    bool empty = !value || !value[0];
    dl->AddText(ImVec2(tx, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), empty ? IM_COL32(125, 135, 145, 255) : kVText, empty ? placeholder : value);
    dl->PopClipRect();
    ImGui::PopID();
    return hit;
}

static void OnSearch(const char* t, void*) { MP_CopyStr(s_szSearch, t ? t : "", sizeof(s_szSearch)); s_nLSel = -1; }
static void OnKickReason(const char* t, void*) { MP_CopyStr(s_szKickReason, t ? t : "", sizeof(s_szKickReason)); }
static void OnBanName(const char* t, void*) { MP_CopyStr(s_szBanName, t ? t : "", sizeof(s_szBanName)); }

// ---- server rows -----------------------------------------------------------------------------
struct LRow
{
    int  kind;            // 0 default, 1 live (internet / Wi-Fi), 2 saved (favourite), 3 Brief Case War (3.04), 4 MW3 (3.23), 5 Mega Vil Futuristic (3.25), 6 Project Day Z (3.58)
    char name[40];
    int  players, maxp, ping, region, icon;
    bool locked, live, on, mine, internet;
    char ip[64]; int port; int saved;
    char lang[MP_LANG_LEN];   // 2.25
    // 4.03: CS:GO style list
    uint32_t token;           // internet lobby token (0 = Wi-Fi / saved)
    char cc[MP_CC_LEN];       // Server location
    int  modeCol;             // gamemode chip colour (kModeCols, 0 = auto)
    char mode[MP_MODE_LEN];   // gamemode chip ("" = Freeroam)
    char tags[MP_TAGS_LEN];
    char desc[MP_DESC_LEN];
    const char* bg;           // featured: bg_*.jpg in bg_server.jar
};

// ---- 4.03: favourites (any row, featured too) - favorites.txt "name|port" ---------------------
static std::set<std::string> s_Favs; static bool s_bFavsRead = false;
static std::string LFavKey(const char* name, int port)
{
    std::string k = name ? name : ""; for(char& c : k) c = (char)tolower((unsigned char)c);
    char b[16]; snprintf(b, sizeof(b), "|%d", port); return k + b;
}
static void LFavsRead()
{
    if(s_bFavsRead) return; s_bFavsRead = true;
    char path[300]; snprintf(path, sizeof(path), "%s/favorites.txt", s_szDataDir);
    FILE* f = fopen(path, "r"); if(!f) return;
    char line[128];
    while(fgets(line, sizeof(line), f)) { line[strcspn(line, "\r\n")] = 0; if(line[0]) s_Favs.insert(line); }
    fclose(f);
}
static bool LIsFav(const char* name, int port) { LFavsRead(); return s_Favs.count(LFavKey(name, port)) != 0; }
static void LSetFav(const char* name, int port, bool on)
{
    LFavsRead();
    if(on) s_Favs.insert(LFavKey(name, port)); else s_Favs.erase(LFavKey(name, port));
    char path[300]; snprintf(path, sizeof(path), "%s/favorites.txt", s_szDataDir);
    if(FILE* f = fopen(path, "w")) { for(auto& k : s_Favs) fprintf(f, "%s\n", k.c_str()); fclose(f); }
}

static ImU32 LModeColor(const LRow& r)
{
    if(r.modeCol > 0 && r.modeCol < kModeColCount) return kModeCols[r.modeCol].col;
    switch(r.kind) { case 3: return kModeCols[2].col; case 4: return kModeCols[3].col; case 5: return kModeCols[4].col; case 6: return kModeCols[5].col; }
    // auto: a stable colour from the gamemode name
    const char* m = r.mode[0] ? r.mode : "Freeroam"; unsigned h = 5381; for(const char* c = m; *c; ++c) h = h * 33 + (unsigned char)tolower((unsigned char)*c);
    return !strcasecmp(m, "Freeroam") ? kModeCols[0].col : kModeCols[1 + h % (kModeColCount - 1)].col;
}

// featured / default text (nobody can change it)
static void LFeaturedDetails(LRow& r)
{
    const char *md = "", *tg = "", *ds = "", *bg = nullptr;
    switch(r.kind)
    {
    case 0: md = "Freeroam"; tg = "freeroam,missions,races,deathmatch,jobs,24/7"; bg = "bg_default.jpg";
            ds = "The main MP server, always online. Free roam San Andreas with everyone: missions, races, deathmatch, jobs and events."; break;
    case 3: md = "Brief Case War"; tg = "bcw,team,pvp,objective,24/7"; bg = "bg_bcw.jpg";
            ds = "Two teams fight over the briefcase. Grab it, carry it home and hold the line."; break;
    case 4: md = "MW3"; tg = "mw3,fps,pvp,killstreaks,classes,24/7"; bg = "bg_mw3.jpg";
            ds = "Call Of Duty: Modern Warfare 3 in San Andreas: classes, killstreaks, ranked matches."; break;
    case 5: md = "Roleplay"; tg = "rp,roleplay,jobs,houses,futuristic,24/7"; bg = "bg_rp.jpg";
            ds = "Mega Vil Futuristic roleplay: jobs, houses, cars and a city that never sleeps."; break;
    case 6: md = "Day Z"; tg = "dayz,zombies,survival,loot,pvp,24/7"; bg = "bg_dz.jpg";
            ds = "Project Day Z Legacy: survive the zombies, find loot, build a camp, trust nobody."; break;
    default: return;
    }
    MP_CopyStr(r.cc, MP_CC_GLOBAL, sizeof(r.cc));   // 4.05.3: the featured 24/7 servers are worldwide - Global flag
    if(!r.mode[0]) MP_CopyStr(r.mode, md, sizeof(r.mode));
    if(!r.tags[0]) MP_CopyStr(r.tags, tg, sizeof(r.tags));
    if(!r.desc[0]) MP_CopyStr(r.desc, ds, sizeof(r.desc));
    r.bg = bg;
}
// d/<token> "v1|cc|modeCol|mode|tags|desc|players" -> row (players stay in the string, see LDetailPlayers)
static bool LParseDetails(const std::string& d, LRow& r, std::string* players)
{
    if(d.compare(0, 3, "v1|")) return false;
    std::vector<std::string> f; size_t at = 3;
    for(int i = 0; i < 6; ++i) { size_t e = i < 5 ? d.find('|', at) : std::string::npos; f.push_back(d.substr(at, e == std::string::npos ? std::string::npos : e - at)); if(e == std::string::npos) break; at = e + 1; }
    while(f.size() < 6) f.push_back("");
    if(!f[0].empty()) MP_CopyStr(r.cc, f[0].c_str(), sizeof(r.cc));
    if(atoi(f[1].c_str())) r.modeCol = atoi(f[1].c_str());
    if(!f[2].empty()) MP_CopyStr(r.mode, f[2].c_str(), sizeof(r.mode));
    if(!f[3].empty()) MP_CopyStr(r.tags, f[3].c_str(), sizeof(r.tags));
    if(!f[4].empty()) MP_CopyStr(r.desc, f[4].c_str(), sizeof(r.desc));
    if(players) *players = f[5];
    return true;
}
static void LFillDetails(LRow& r, const LanServer* sv)
{
    if(sv)
    {
        r.token = sv->token;
        if(sv->cc[0]) MP_CopyStr(r.cc, sv->cc, sizeof(r.cc));
        if(sv->modeCol) r.modeCol = sv->modeCol;
        if(sv->mode[0]) MP_CopyStr(r.mode, sv->mode, sizeof(r.mode));
        if(sv->tags[0]) MP_CopyStr(r.tags, sv->tags, sizeof(r.tags));
        if(sv->desc[0]) MP_CopyStr(r.desc, sv->desc, sizeof(r.desc));
    }
    std::string d;
    if(r.token && Online::Details(r.token, d)) LParseDetails(d, r, nullptr);
    if(r.mine && r.kind <= 2)   // our own server: what we host with
    {
        if(!r.cc[0]) MP_CopyStr(r.cc, settings.serverCC, sizeof(r.cc));
        if(!r.mode[0]) MP_CopyStr(r.mode, settings.serverMode, sizeof(r.mode));
        if(!r.modeCol) r.modeCol = settings.serverModeCol;
        if(!r.tags[0]) MP_CopyStr(r.tags, settings.serverTags, sizeof(r.tags));
        if(!r.desc[0]) MP_CopyStr(r.desc, settings.serverDesc, sizeof(r.desc));
    }
    LFeaturedDetails(r);
}


static bool LMatch(const char* name, const char* lang = nullptr)
{
    if(!s_szSearch[0]) return true;
    char a[64], b[32]; MP_CopyStr(a, name, sizeof(a)); MP_CopyStr(b, s_szSearch, sizeof(b));
    for(char* c = a; *c; ++c) *c = (char)tolower((unsigned char)*c);
    for(char* c = b; *c; ++c) *c = (char)tolower((unsigned char)*c);
    if(strstr(a, b)) return true;
    if(!lang || !lang[0]) return false;   // 2.25: search also finds the language ("filipino")
    MP_CopyStr(a, lang, sizeof(a)); for(char* c = a; *c; ++c) *c = (char)tolower((unsigned char)*c);
    return strstr(a, b) != nullptr;
}

static void LBuildRows(int tab, const std::vector<LanServer>& nearby, std::vector<LRow>& rows)
{
    rows.clear();
    if(tab == LT_INTERNET || tab == LT_LAN)
    {
        LRow r; memset(&r, 0, sizeof(r));
        r.kind = 0; MP_CopyStr(r.name, MP_DEFAULT_SERVER_NAME, sizeof(r.name)); r.port = MP_DEFAULT_PORT;
        r.players = -1; r.maxp = -1; r.ping = -1; r.region = -1; r.icon = 0; r.saved = -1;
        r.on = IsOnDefault();
        int found = -1;
        for(int i = 0; i < (int)nearby.size(); ++i)   // 2.29: the 24/7 copy first
            if(IsDefaultServer(nearby[i].name, nearby[i].port) && nearby[i].dedicated && found < 0) found = i;   // 3.53: only the 24/7 Broker copy counts
        if(r.on && IsConnected())  { r.players = OnlineCount(); r.maxp = OnlineMax(); r.ping = MP::DisplayPing(); r.live = true; }
        else if(IsHostingDefault()) { r.players = s_Server.GetPlayerCount(); r.maxp = s_Server.GetMaxPlayers(); r.ping = 0; r.live = true; r.mine = true; }
        else if(found >= 0)        { r.players = nearby[found].players; r.maxp = nearby[found].maxPlayers; r.ping = nearby[found].ping; r.live = true; r.internet = nearby[found].token != 0; }
        else                       { r.players = 0; r.maxp = MP_STD_MAX_PLAYERS; r.live = true; }   // 2.29: always there, 0/100 - Join starts it
        LFillDetails(r, found >= 0 ? &nearby[found] : nullptr);
        MP_CopyStr(r.lang, "All", sizeof(r.lang));   // 3.53: the 4 featured servers are always "All" (nobody can change it)
        if(tab == LT_INTERNET && s_nInetSub == 0 && LMatch(r.name)) rows.push_back(r);
        if(tab == LT_INTERNET && s_nInetSub == 1) {   // 3.58: FEATURED page
        {   // 3.04: Brief Case War - pinned under the default server, always there (0/20) like it
            LRow w; memset(&w, 0, sizeof(w));
            w.kind = 3; MP_CopyStr(w.name, MP_BCW_SERVER_NAME, sizeof(w.name)); w.port = MP_BCW_PORT;
            w.region = -1; w.icon = 0; w.saved = -1; w.live = true;
            w.on = MP::IsOnBcw();
            int bf = -1;
            for(int i = 0; i < (int)nearby.size(); ++i)
                if(nearby[i].port == MP_BCW_PORT && MP_IsBcwServerName(nearby[i].name) && nearby[i].dedicated && bf < 0) bf = i;   // 3.53: 24/7 copy only
            bool hosting = s_Server.IsRunning() && s_Server.IsBcwServer();
            if(w.on && IsConnected())  { w.players = OnlineCount(); w.maxp = OnlineMax(); w.ping = MP::DisplayPing(); }
            else if(hosting)           { w.players = s_Server.GetPlayerCount(); w.maxp = MP_BCW_MAX_PLAYERS; w.ping = 0; w.mine = true; }
            else if(bf >= 0)           { w.players = nearby[bf].players; w.maxp = nearby[bf].maxPlayers; w.ping = nearby[bf].ping; w.internet = nearby[bf].token != 0; }
            else                       { w.players = 0; w.maxp = MP_BCW_MAX_PLAYERS; w.ping = -1; }
            MP_CopyStr(w.lang, "All", sizeof(w.lang));
            LFillDetails(w, bf >= 0 ? &nearby[bf] : nullptr);
            if(LMatch(w.name) || LMatch("briefcase war")) rows.push_back(w);
        }
        {   // 3.23: Call Of Duty: Modern Warfare 3 - 24/7 featured, pinned under Brief Case War, the VPS hosts it, or a phone when the VPS is down (3.23.1)
            LRow w; memset(&w, 0, sizeof(w));
            w.kind = 4; MP_CopyStr(w.name, MP_MW3_SERVER_NAME, sizeof(w.name)); w.port = MP_MW3_PORT;
            w.region = -1; w.icon = 0; w.saved = -1; w.live = true;
            w.on = MP::IsOnMw3();
            int bf = -1;
            for(int i = 0; i < (int)nearby.size(); ++i)
                if(MP_IsMw3ServerName(nearby[i].name) && nearby[i].dedicated && bf < 0) bf = i;   // 3.53: 24/7 copy only
            bool hosting = s_Server.IsRunning() && s_Server.IsMw3Server();   // 3.23.1: a phone keeps it up when the VPS is down
            if(w.on && IsConnected())  { w.players = OnlineCount(); w.maxp = OnlineMax(); w.ping = MP::DisplayPing(); }
            else if(hosting)           { w.players = s_Server.GetPlayerCount(); w.maxp = MP_MW3_MAX_PLAYERS; w.ping = 0; w.mine = true; }
            else if(bf >= 0)           { w.players = nearby[bf].players; w.maxp = nearby[bf].maxPlayers; w.ping = nearby[bf].ping; w.internet = nearby[bf].token != 0; }
            else                       { w.players = 0; w.maxp = MP_MW3_MAX_PLAYERS; w.ping = -1; }
            MP_CopyStr(w.lang, "All", sizeof(w.lang));
            LFillDetails(w, bf >= 0 ? &nearby[bf] : nullptr);
            if(LMatch(w.name) || LMatch("mw3") || LMatch("modern warfare")) rows.push_back(w);
        }
        {   // 3.25: Mega Vil Futuristic - 24/7 featured Roleplay, pinned under MW3 (VPS, or the Broker standby phones)
            LRow w; memset(&w, 0, sizeof(w));
            w.kind = 5; MP_CopyStr(w.name, MP_RP_SERVER_NAME, sizeof(w.name)); w.port = MP_RP_PORT;
            w.region = -1; w.icon = 0; w.saved = -1; w.live = true;
            w.on = MP::IsOnRp();
            int bf = -1;
            for(int i = 0; i < (int)nearby.size(); ++i)
                if(MP_IsRpServerName(nearby[i].name) && nearby[i].dedicated && bf < 0) bf = i;   // 3.53: 24/7 copy only
            bool hosting = s_Server.IsRunning() && s_Server.IsRpServer();
            if(w.on && IsConnected())  { w.players = OnlineCount(); w.maxp = OnlineMax(); w.ping = MP::DisplayPing(); }
            else if(hosting)           { w.players = s_Server.GetPlayerCount(); w.maxp = MP_RP_MAX_PLAYERS; w.ping = 0; w.mine = true; }
            else if(bf >= 0)           { w.players = nearby[bf].players; w.maxp = nearby[bf].maxPlayers; w.ping = nearby[bf].ping; w.internet = nearby[bf].token != 0; }
            else                       { w.players = 0; w.maxp = MP_RP_MAX_PLAYERS; w.ping = -1; }
            MP_CopyStr(w.lang, "All", sizeof(w.lang));
            LFillDetails(w, bf >= 0 ? &nearby[bf] : nullptr);
            if(LMatch(w.name) || LMatch("roleplay") || LMatch("rp") || LMatch("mega vil")) rows.push_back(w);
        }
        {   // 3.58: Project Day Z Legacy - 24/7 featured (Broker bot on GitHub Actions)
            LRow w; memset(&w, 0, sizeof(w));
            w.kind = 6; MP_CopyStr(w.name, MP_DZ_SERVER_NAME, sizeof(w.name)); w.port = MP_DZ_PORT;
            w.region = -1; w.icon = 0; w.saved = -1; w.live = true;
            w.on = MP::IsOnDz();
            int bf = -1;
            for(int i = 0; i < (int)nearby.size(); ++i)
                if(MP_IsDzServerName(nearby[i].name) && nearby[i].dedicated && bf < 0) bf = i;
            bool hosting = s_Server.IsRunning() && s_Server.IsDayzServer();
            if(w.on && IsConnected())  { w.players = OnlineCount(); w.maxp = OnlineMax(); w.ping = MP::DisplayPing(); }
            else if(hosting)           { w.players = s_Server.GetPlayerCount(); w.maxp = MP_DZ_MAX_PLAYERS; w.ping = 0; w.mine = true; }
            else if(bf >= 0)           { w.players = nearby[bf].players; w.maxp = nearby[bf].maxPlayers; w.ping = nearby[bf].ping; w.internet = nearby[bf].token != 0; }
            else                       { w.players = 0; w.maxp = MP_DZ_MAX_PLAYERS; w.ping = -1; }
            MP_CopyStr(w.lang, "All", sizeof(w.lang));
            LFillDetails(w, bf >= 0 ? &nearby[bf] : nullptr);
            if(LMatch(w.name) || LMatch("dayz") || LMatch("day z") || LMatch("zombie")) rows.push_back(w);
        }
        }   // 3.58: end of the FEATURED page
        for(auto& sv : nearby)
        {
            if(tab == LT_INTERNET && s_nInetSub != 0) break;   // 3.58: custom servers are on the PUBLIC page
            if(tab == LT_LAN && sv.token) continue;             // 4.03: Lan = Wi-Fi only
            if(IsDefaultServer(sv.name, sv.port)) continue;
            if(MP_IsBcwServerName(sv.name)) continue;   // 3.04: the pinned row
            if(MP_IsMw3ServerName(sv.name)) continue;   // 3.23: the pinned row
            if(MP_IsRpServerName(sv.name)) continue;    // 3.25: the pinned row
            if(MP_IsDzServerName(sv.name)) continue;    // 3.58: the pinned row
            if(tab == LT_INTERNET && !settings.lobbyAuto && sv.token != 0 && sv.region != settings.region) continue;   // 3.25: region switch (Wi-Fi servers always show)
            LRow q; memset(&q, 0, sizeof(q));
            q.kind = 1; MP_CopyStr(q.name, sv.name, sizeof(q.name)); MP_CopyStr(q.ip, sv.ip, sizeof(q.ip)); q.port = sv.port;
            q.players = sv.players; q.maxp = sv.maxPlayers; q.ping = sv.ping; q.region = sv.region; q.icon = sv.icon;
            q.locked = sv.locked != 0; q.live = true; q.internet = sv.token != 0; q.saved = -1;
            q.on = IsConnectedTo(sv.ip, sv.port);
            MP_CopyStr(q.lang, sv.lang, sizeof(q.lang));
            q.mine = s_Server.IsRunning() && s_Server.GetPort() == sv.port && !strcasecmp(s_Server.GetName(), sv.name);
            LFillDetails(q, &sv);
            if(LMatch(q.name, q.lang) || LMatch(q.tags) || LMatch(q.mode)) rows.push_back(q);
        }
    }
    else if(tab == LT_FAVORITES)
    {
        for(int i = 0; i < ServerList::Count(); ++i)
        {
            ServerEntry* e = ServerList::Get(i);
            if(!e || !LMatch(e->name, e->liveLang[0] ? e->liveLang : e->lang)) continue;
            LRow q; memset(&q, 0, sizeof(q));
            q.kind = 2; q.saved = i; MP_CopyStr(q.name, e->name, sizeof(q.name)); q.port = e->port; q.region = e->region;
            const LanServer* lan = nullptr;
            for(auto& sv : nearby) if(sv.port == e->port && !strcasecmp(sv.name, e->name)) { lan = &sv; break; }
            q.mine = s_Server.IsRunning() && s_Server.GetPort() == e->port && !strcasecmp(s_Server.GetName(), e->name);
            q.on = client.GetState() != NET_DISCONNECTED && client.GetPort() == e->port &&
                   (IsConnectedTo(e->host, e->port) || (lan && !strcmp(client.GetHost(), lan->ip)) ||
                    (q.mine && IsLocalHost(client.GetHost())) || (client.GetServerName()[0] && !strcasecmp(client.GetServerName(), e->name)));
            q.locked = e->password[0] || e->locked;
            q.players = q.maxp = q.ping = -1;
            if(q.on && IsConnected()) { q.players = OnlineCount(); q.maxp = OnlineMax(); q.ping = MP::DisplayPing(); q.live = true; }
            else if(q.mine)           { q.players = s_Server.GetPlayerCount(); q.maxp = s_Server.GetMaxPlayers(); q.ping = 0; q.live = true; }
            else if(e->online)        { q.players = e->players; q.maxp = e->maxPlayers; q.ping = e->ping; q.live = true; }
            else if(lan)              { q.players = lan->players; q.maxp = lan->maxPlayers; q.ping = lan->ping; q.live = true; q.internet = lan->token != 0; }
            q.icon = q.mine ? settings.serverIcon : (e->online ? e->icon : (lan ? lan->icon : 0));
            const char* lg = e->online && e->liveLang[0] ? e->liveLang : (lan && lan->lang[0] ? lan->lang : e->lang);   // live, else saved
            MP_CopyStr(q.lang, lg, sizeof(q.lang));
            // 4.03: details saved in + Add server, else what the running server says
            MP_CopyStr(q.cc, e->cc[0] ? e->cc : e->liveCC, sizeof(q.cc)); q.modeCol = e->modeCol ? e->modeCol : e->liveModeCol;
            MP_CopyStr(q.mode, e->mode[0] ? e->mode : e->liveMode, sizeof(q.mode));
            MP_CopyStr(q.tags, e->tags[0] ? e->tags : e->liveTags, sizeof(q.tags));
            MP_CopyStr(q.desc, e->desc[0] ? e->desc : e->liveDesc, sizeof(q.desc));
            LFillDetails(q, lan);
            rows.push_back(q);
        }
        // 4.03: + every starred row from Internet (featured too) that is not saved here
        std::vector<LRow> more; int keep = s_nInetSub;
        for(int sub = 0; sub < 2; ++sub)
        {
            s_nInetSub = sub; std::vector<LRow> tmp; int sc = s_nSortCol; s_nSortCol = -1;
            LBuildRows(LT_INTERNET, nearby, tmp); s_nSortCol = sc;
            for(auto& r : tmp)
            {
                if(!LIsFav(r.name, r.port)) continue;
                bool dup = false; for(auto& q : rows) if(q.port == r.port && !strcasecmp(q.name, r.name)) dup = true;
                if(!dup) more.push_back(r);
            }
        }
        s_nInetSub = keep;
        rows.insert(rows.begin(), more.begin(), more.end());
    }
    if(s_nSortCol >= 0 && rows.size() > 1)
    {
        size_t from = 0;   // the default server + Brief Case War stay on top
        while(tab == LT_INTERNET && from < rows.size() && (rows[from].kind == 0 || rows[from].kind == 3 || rows[from].kind == 4 || rows[from].kind == 5 || rows[from].kind == 6)) ++from;
        auto key = [](const LRow& r, int c) -> int { return c == 1 ? r.players : (c == 2 ? (r.ping < 0 ? 99999 : r.ping) : (c == 3 ? r.region : 0)); };
        std::stable_sort(rows.begin() + from, rows.end(), [&](const LRow& a, const LRow& b)
        {
            int c = s_nSortCol; bool lt;
            const char* sa = c == 4 ? a.cc : (c == 3 ? a.mode : a.name); const char* sb = c == 4 ? b.cc : (c == 3 ? b.mode : b.name);
            bool str = c == 0 || c == 3 || c == 4;
            if(str) lt = strcasecmp(sa, sb) < 0;
            else lt = key(a, c) < key(b, c);
            bool gt = str ? strcasecmp(sb, sa) < 0 : key(b, c) < key(a, c);
            return s_bSortAsc ? lt : gt;
        });
    }
}

// 3.10: tapping the server you are already on (its row, Join / Connect) used to leave it and go
// through the whole connection again. Now it only says so; leaving is the Leave button.
static bool LAlreadyOn(const LRow& r)
{
    if(!r.on || client.GetState() == NET_DISCONNECTED) return false;
    SysMsg(COL_SYSTEM, client.GetState() == NET_CONNECTED ? "You are already connected to %s." : "Already connecting to %s...", r.name[0] ? r.name : "this server");
    return true;
}
static void LLeave(const LRow& r) { if(r.kind == 0) { if(IsOnDefault()) LeaveServer("Disconnected from the server."); } else LeaveServer("Left the server."); }
static void LJoin(const LRow& r)
{
    if(LAlreadyOn(r)) return;
    if(r.kind == 0) { if(IsOnDefault()) MP::ToggleConnect(); else { LeaveServer(nullptr); MP::ToggleConnect(); } return; }
    if(r.kind == 3) { if(!MP::IsOnBcw()) LeaveServer(nullptr); MP::JoinBcw(); return; }   // 3.04
    if(r.kind == 4) { if(!MP::IsOnMw3()) LeaveServer(nullptr); MP::JoinMw3(); return; }   // 3.23
    if(r.kind == 5) { if(!MP::IsOnRp()) LeaveServer(nullptr); MP::JoinRp(); return; }     // 3.25
    if(r.kind == 6) { if(!MP::IsOnDz()) LeaveServer(nullptr); MP::JoinDz(); return; }     // 3.58
    if(r.on) { LeaveServer("Left the server."); return; }
    if(r.kind == 1) MP::JoinServer(r.ip, r.port, r.name);
    else if(ServerEntry* e = ServerList::Get(r.saved)) MP::JoinServer(e->host, e->port, e->name);
}

static void LRowKey(const LRow& r, char* out, size_t n) { snprintf(out, n, "%d|%s|%d", r.kind, r.name, r.port); }

// ---- 3.25: region switch: Auto | Asia | Europe | NA | Oceania --------------------------------------
// 3.25.1: it is an icon button (map pin) in the toolbar, next to the gear. Tapping it opens this small list under
// the icon (it used to be a row of tabs above the server table).
// Auto = the lobby uses the lowest ping region and the list shows every region. Picking a region moves
// the lobby to that region's broker, makes it your hosting region and shows only its servers (the
// featured 24/7 servers and Wi-Fi servers always stay in the list).
static bool   s_bRegionPick = false;            // the region list is open
static uint64_t s_nRegionToggleMs = 0;          // 4.05.3: last open / close of the region list
static ImVec2 s_vRegionBtn(0, 0);               // top-left of the toolbar icon (set every frame by the toolbar)
static float  s_fRegionBtn = 0;                 // its size
// 4.05.8: third way to hit the region icon. The press is also checked straight from the touch event
// (before ImGui, before the game), so the list opens even when ImGui hands the tap to another window.
static std::mutex s_RawTapMx;
static bool  s_bRawTap = false; static float s_fRawTapX = 0, s_fRawTapY = 0; static uint64_t s_nRawTapMs = 0;
void MP::LauncherTouchDown(float x, float y)
{
    std::lock_guard<std::mutex> lk(s_RawTapMx);
    s_bRawTap = true; s_fRawTapX = x; s_fRawTapY = y; s_nRawTapMs = MP_NowMs();
}
// true once for a raw press inside the region icon (fresh, the launcher on top there)
static bool LRegionRawTap(ImVec2 p, float sz)
{
    float x, y; uint64_t t;
    { std::lock_guard<std::mutex> lk(s_RawTapMx); if(!s_bRawTap) return false; s_bRawTap = false; x = s_fRawTapX; y = s_fRawTapY; t = s_nRawTapMs; }
    if(MP_NowMs() - t > 500 || VKeyboard::IsOpen()) return false;
    // 4.10: much more slack (was 6 px). With ARM's AdvancedSettings HUD the pin sits right on the moved
    // clock / money / weapon icon and fingers land a bit off it - a third of the icon around it counts.
    const float m = ImMax(UI::S(14), sz * 0.35f);
    if(x < p.x - m || y < p.y - m || x > p.x + sz + m || y > p.y + sz + m) return false;
    const char* above = "";
    if(!FloatMenu::LauncherOwnsPoint(x, y, &above)) return false;
    static int s_nLogs = 0;
    if(s_nLogs < 6) { ++s_nLogs; logger->Info("4.05.8: region icon pressed (raw touch %.0f,%.0f)", x, y); }
    return true;
}

static void LRegionChoose(int i)                // i < 0 = Auto
{
    if(i < 0)
    {
        settings.lobbyAuto = true;
        int best = Online::BestRegion();
        if(best >= 0) { settings.region = best; s_Server.SetRegion(best); }
        SysMsg(COL_SYSTEM, "Region: Auto (lowest ping)%s%s. All regions are listed.", best >= 0 ? " - " : "", best >= 0 ? Online::RegionLabel(best) : "");
    }
    else
    {
        settings.lobbyAuto = false; settings.region = i; settings.regionPicked = true;
        s_Server.SetRegion(i);
        int ms = Online::RegionPingMs(i);
        if(ms >= 0) SysMsg(COL_SYSTEM, "Region: %s (%d ms). The lobby moves to the %s broker; only %s servers are listed.", Online::RegionLabel(i), ms, g_MPRegionNames[i], g_MPRegionNames[i]);
        else SysMsg(COL_SYSTEM, "Region: %s. Only %s servers are listed.", Online::RegionLabel(i), g_MPRegionNames[i]);
    }
    Online::SetRegionPrefs(settings.region, settings.lobbyAuto);
    s_nLSel = -1; s_szLSelKey[0] = 0;
    SaveConfig();
}

static void LRegionPicker(const std::vector<LanServer>& nearby)
{
    if(!s_bRegionPick) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    // 4.10: the text follows the launcher panel's own font size (Settings > Display > Font scale) - it was
    // a fixed 26 / 19 px, smaller than the server list next to it. The rows and the width grow with it.
    const float fs1 = ImMax(UI::S(26), ImGui::GetFontSize() * 1.08f), fs2 = ImMax(UI::S(19), ImGui::GetFontSize() * 0.86f);
    const float rowH = ImMax(UI::S(62), fs1 * 2.0f), gap = UI::S(6), pw = UI::S(12);
    float wantW = 0;
    {
        ImFont* f = ImGui::GetFont();
        float l1w = f->CalcTextSizeA(fs1, FLT_MAX, 0, "Oceania").x, l2w = f->CalcTextSizeA(fs2, FLT_MAX, 0, "offline  |  888 servers").x;
        wantW = pw * 2 + UI::S(34) + l1w + UI::S(28) + l2w + UI::S(16);
    }
    const float w = ImMin(ImMax(ImMax(UI::S(470), ds.x * 0.26f), wantW), ds.x - UI::S(20));
    const float h = pw * 2 + (REGION_COUNT + 1) * rowH + REGION_COUNT * gap;
    float x = s_vRegionBtn.x, y = s_vRegionBtn.y + s_fRegionBtn + UI::S(8);
    if(x + w > ds.x - UI::S(10)) x = ds.x - UI::S(10) - w;
    if(y + h > ds.y - UI::S(10)) y = ds.y - UI::S(10) - h;
    // 4.05.6: on a short screen the list was pushed up over the icon itself (the next tap on the icon
    // landed on the list) - then it opens to the left of the icon
    if(y < s_vRegionBtn.y + s_fRegionBtn && x < s_vRegionBtn.x + s_fRegionBtn) { x = ImMax(UI::S(10), s_vRegionBtn.x - w - UI::S(8)); if(y < UI::S(10)) y = UI::S(10); }
    int cnt[REGION_COUNT] = { 0 }, all = 0;
    for(auto& sv : nearby) if(sv.token != 0 && sv.region < REGION_COUNT) { ++cnt[sv.region]; ++all; }
    const int cur = Online::CurrentRegion();

    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pw, pw));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);   // 4.03: square
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(1));
    // 4.08: same panel as the launcher / server list (slate, black edge) - was a Valorant navy + teal box
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(36 / 255.f, 40 / 255.f, 46 / 255.f, 250 / 255.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 200 / 255.f));
    ImGui::SetNextWindowFocus();
    if(ImGui::Begin("##MPRegionPick", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        for(int i = -1; i < REGION_COUNT; ++i)
        {
            ImVec2 a(p.x + pw, p.y + pw + (i + 1) * (rowH + gap)), b(a.x + w - pw * 2, a.y + rowH);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i + 100);
            bool tap = ImGui::InvisibleButton("##reg", ImVec2(b.x - a.x, rowH));
            bool held = ImGui::IsItemActive();
            ImGui::PopID();
            const bool sel = i < 0 ? settings.lobbyAuto : (!settings.lobbyAuto && settings.region == i);
            // 4.08: rows like the server list (slate rows, white highlight, blue stripe on the picked one)
            dl->AddRectFilled(a, b, ((i + 1) & 1) ? IM_COL32(30, 34, 40, 170) : IM_COL32(44, 49, 56, 170));
            if(sel) { dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 26)); dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), kVTeal); }
            if(held) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 14));
            dl->AddLine(ImVec2(a.x, b.y + gap * 0.5f), ImVec2(b.x, b.y + gap * 0.5f), IM_COL32(0, 0, 0, 90));
            const ImU32 tc = sel ? IM_COL32_WHITE : kVText, sc = kVDim;
            char l1[48], l2[64];
            if(i < 0)
            {
                int best = Online::BestRegion();
                snprintf(l1, sizeof(l1), "Auto");
                if(best >= 0) snprintf(l2, sizeof(l2), "%s  |  %d servers", Online::RegionLabel(best), all); else snprintf(l2, sizeof(l2), "lowest ping  |  %d", all);
            }
            else
            {
                snprintf(l1, sizeof(l1), "%s", Online::RegionLabel(i));   // 4.12: "Asia - Singapore"
                int ms = Online::RegionPingMs(i), same = Online::RegionSameAs(i);
                if(same >= 0) snprintf(l2, sizeof(l2), "same server as %s  |  %d", g_MPRegionNames[same], cnt[i]);   // 4.12: no VPS of its own yet
                else if(ms >= 0) snprintf(l2, sizeof(l2), "%d ms  |  %d servers", ms, cnt[i]);
                else snprintf(l2, sizeof(l2), "%s  |  %d servers", ms == -2 ? "offline" : "-- ms", cnt[i]);
            }
            ImFont* rf = ImGui::GetFont();   // 4.10: the panel's font (was the fixed UI::Font())
            const ImVec2 s2 = rf->CalcTextSizeA(fs2, FLT_MAX, 0, l2);
            dl->AddText(rf, fs1, ImVec2(a.x + UI::S(34), a.y + (rowH - fs1) * 0.5f), tc, l1);
            dl->AddText(rf, fs2, ImVec2(b.x - UI::S(16) - s2.x, a.y + (rowH - fs2) * 0.5f), sc, l2);
            if(i >= 0 && cur == i) dl->AddCircleFilled(ImVec2(a.x + UI::S(18), a.y + rowH * 0.5f), UI::S(6), IM_COL32(34, 197, 94, 255));   // lobby broker in use
            if(tap)
            {
                if(!sel) LRegionChoose(i);
                s_bRegionPick = false;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    // a tap anywhere else (not on the picker, not on the icon that toggles it) closes the list
    if(s_bRegionPick && ImGui::IsMouseClicked(0) && MP_NowMs() - s_nRegionToggleMs > 300   // 4.05.3: not the opening tap itself
       && !ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, y + h), false)
       && !ImGui::IsMouseHoveringRect(s_vRegionBtn, ImVec2(s_vRegionBtn.x + s_fRegionBtn, s_vRegionBtn.y + s_fRegionBtn), false))
        s_bRegionPick = false;
}

// ---- centre table ------------------------------------------------------------------------------
// ---- 4.03: CS:GO style table pieces ---------------------------------------------------------------
static std::map<std::string, std::vector<int>> s_PingHist;   // row key -> last 40 pings (1 a second)
static void LStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool filled)
{
    ImVec2 p[10];
    for(int i = 0; i < 10; ++i) { float an = -IM_PI * 0.5f + i * IM_PI / 5, rr = (i & 1) ? r * 0.45f : r; p[i] = ImVec2(c.x + cosf(an) * rr, c.y + sinf(an) * rr); }
    if(filled) for(int i = 0; i < 10; i += 2) dl->AddTriangleFilled(c, p[i], p[(i + 1) % 10], col), dl->AddTriangleFilled(c, p[(i + 9) % 10], p[i], col);
    dl->AddPolyline(p, 10, col, ImDrawFlags_Closed, UI::S(1.5f));
}
static void* LRowBg(const LRow& r, ImVec2* uv)
{
    if(r.bg) if(void* t = HUD::Texture(r.bg, uv)) return t;
    if(r.icon > 0) return HUD::ServerIcon(r.icon, uv);
    return nullptr;
}
// 4.04: the host's own Upload preview image (p/<token> in the lobby), written once to files/MP/pv_<tok>_<crc>.jpg
static void* LRowPreview(const LRow& r, ImVec2* uv)
{
    if(r.mine && r.kind <= 2 && settings.serverPreview[0]) if(void* t = HUD::Texture(settings.serverPreview, uv)) return t;
    if(!r.token) return nullptr;
    static std::map<uint32_t, std::pair<uint64_t, std::string>> s_Pv;   // token -> last check, file name ("" = none)
    auto& c = s_Pv[r.token];
    uint64_t now = MP_NowMs();
    if(!c.first || now - c.first > 5000)
    {
        c.first = now;
        std::string jpg;
        if(!Online::Preview(r.token, jpg)) c.second.clear();
        else
        {
            uint32_t h = 2166136261u; for(unsigned char ch : jpg) h = (h ^ ch) * 16777619u;
            char fn[48]; snprintf(fn, sizeof(fn), "pv_%08x_%08x.jpg", r.token, h);
            if(c.second != fn)
            {
                char pp[512]; snprintf(pp, sizeof(pp), "%s/%s", s_szDataDir, fn);
                FILE* f = fopen(pp, "rb");
                if(f) fclose(f);
                else if((f = fopen(pp, "wb"))) { fwrite(jpg.data(), 1, jpg.size(), f); fclose(f); }
                c.second = fn;
            }
        }
    }
    if(c.second.empty()) return nullptr;
    return HUD::Texture(c.second.c_str(), uv);
}
// the picture covers [a, b] (cropped to keep its 2:1 shape)
static void LImageCover(ImDrawList* dl, void* tex, ImVec2 uv, ImVec2 a, ImVec2 b, ImU32 tint)
{
    float bw = b.x - a.x, bh = b.y - a.y; if(bw <= 0 || bh <= 0) return;
    float img = 2.0f, box = bw / bh;   // 512x256
    ImVec2 u0(0, 0), u1(uv.x, uv.y);
    if(box > img) { float k = img / box; u0.y = uv.y * (1 - k) * 0.5f; u1.y = uv.y - u0.y; }
    else          { float k = box / img; u0.x = uv.x * (1 - k) * 0.5f; u1.x = uv.x - u0.x; }
    dl->AddImage((ImTextureID)tex, a, b, u0, u1, tint);
}
// 4.08: the row background picture fades out to the right (alpha -> 0 over the last part) instead of
// stopping with a hard cut at 46 % of the row
static void LImageCoverFade(ImDrawList* dl, void* tex, ImVec2 uv, ImVec2 a, ImVec2 b, int alpha, float solid)
{
    float bw = b.x - a.x, bh = b.y - a.y; if(bw <= 0 || bh <= 0) return;
    float img = 2.0f, box = bw / bh;
    ImVec2 u0(0, 0), u1(uv.x, uv.y);
    if(box > img) { float k = img / box; u0.y = uv.y * (1 - k) * 0.5f; u1.y = uv.y - u0.y; }
    else          { float k = box / img; u0.x = uv.x * (1 - k) * 0.5f; u1.x = uv.x - u0.x; }
    const int N = 8;   // the fade part in 8 steps (smooth alpha, easing out)
    float xs = a.x + bw * solid, us = u0.x + (u1.x - u0.x) * solid;
    dl->PushTextureID((ImTextureID)tex);
    auto quad = [&](float x0, float x1, float ua, float ub, int al0, int al1)
    {
        ImU32 c0 = IM_COL32(255, 255, 255, al0), c1 = IM_COL32(255, 255, 255, al1);
        dl->PrimReserve(6, 4);
        ImDrawIdx i = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteIdx(i); dl->PrimWriteIdx(i + 1); dl->PrimWriteIdx(i + 2);
        dl->PrimWriteIdx(i); dl->PrimWriteIdx(i + 2); dl->PrimWriteIdx(i + 3);
        dl->PrimWriteVtx(ImVec2(x0, a.y), ImVec2(ua, u0.y), c0);
        dl->PrimWriteVtx(ImVec2(x1, a.y), ImVec2(ub, u0.y), c1);
        dl->PrimWriteVtx(ImVec2(x1, b.y), ImVec2(ub, u1.y), c1);
        dl->PrimWriteVtx(ImVec2(x0, b.y), ImVec2(ua, u1.y), c0);
    };
    if(solid > 0) quad(a.x, xs, u0.x, us, alpha, alpha);
    for(int k = 0; k < N; ++k)
    {
        float f0 = (float)k / N, f1 = (float)(k + 1) / N;
        float e0 = 1 - f0, e1 = 1 - f1; e0 *= e0; e1 *= e1;   // ease out
        quad(xs + (b.x - xs) * f0, xs + (b.x - xs) * f1, us + (u1.x - us) * f0, us + (u1.x - us) * f1, (int)(alpha * e0), (int)(alpha * e1));
    }
    dl->PopTextureID();
}
static void LChip(ImDrawList* dl, ImVec2 p, float h, const char* txt, ImU32 col, float maxW)
{
    float tw = ImMin(ImGui::CalcTextSize(txt).x, maxW - UI::S(16));
    ImVec2 b(p.x + tw + UI::S(16), p.y + h);
    dl->AddRectFilled(p, b, col);
    dl->AddRect(p, b, IM_COL32(255, 255, 255, 40));
    dl->PushClipRect(p, b, true);
    dl->AddText(ImVec2(p.x + UI::S(8), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32_WHITE, txt);
    dl->PopClipRect();
}
// who is inside: live list when we are on it / host it, else what the host publishes (d/<token>)
static void LDetailPlayers(const LRow& r, std::vector<std::string>& out)
{
    out.clear();
    if(r.on && IsConnected())
    {
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(i == myId || players[i].active) out.push_back(i == myId ? settings.name : players[i].name);
        return;
    }
    if(r.mine) { std::vector<MPServer::PlayerView> pv; s_Server.GetPlayers(pv); for(auto& v : pv) out.push_back(v.name); return; }
    std::string d, pl; LRow tmp = r;
    if(r.token && Online::Details(r.token, d) && LParseDetails(d, tmp, &pl))
    {
        size_t at = 0;
        while(at < pl.size()) { size_t e = pl.find(',', at); if(e == std::string::npos) e = pl.size(); if(e > at) out.push_back(pl.substr(at, e - at)); at = e + 1; }
    }
}
static void LWrapText(ImDrawList* dl, ImVec2 p, float w, float maxY, ImU32 col, const char* txt)
{
    dl->PushClipRect(p, ImVec2(p.x + w, maxY), true);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, p, col, txt, nullptr, w);
    dl->PopClipRect();
}

static void LTable(std::vector<LRow>& rows, float w, float h)
{
    ImGui::BeginChild("##ltable", ImVec2(w, h), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hdrH = UI::S(52), rowH = UI::S(62), detH = UI::S(300);
    float W = ImGui::GetContentRegionAvail().x;
    // 4.03: CS:GO columns: [lock] Name | Gamemode | Tags | Players | Map | Ping | Location
    float cLock = UI::S(10), cName = UI::S(52);
    // 4.05.3: Language has its own column now (it used to show inside Tags when a server had no tags)
    float cMode = W * 0.33f, cTags = W * 0.45f, cLang = W * 0.59f, cPl = W * 0.69f, cMap = W * 0.765f, cPing = W * 0.88f, cLoc = W * 0.935f;
    static const char* hdr[6] = { "Name", "Gamemode", "Players", "Map", "Ping", "" };
    const float cx[6] = { cName, cMode, cPl, cMap, cPing, cLoc };
    const float cw[6] = { cMode - cName, cTags - cMode, cMap - cPl, cPing - cMap, cLoc - cPing, W - cLoc };
    const int   sortOf[6] = { 0, 3, 1, -1, 2, 4 };
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + W, p.y + hdrH), IM_COL32(20, 23, 28, 130));
        for(int c = 0; c < 6; ++c)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x + cx[c], p.y));
            ImGui::PushID(c);
            if(ImGui::InvisibleButton("##hdr", ImVec2(cw[c], hdrH)) && sortOf[c] >= 0)
            {
                int k = sortOf[c];
                if(s_nSortCol == k) { if(!s_bSortAsc) s_nSortCol = -1; s_bSortAsc = !s_bSortAsc; }
                else { s_nSortCol = k; s_bSortAsc = true; }
            }
            ImGui::PopID();
            const char* ht = c == 5 ? "Loc" : hdr[c];
            float ty = p.y + (hdrH - ImGui::GetTextLineHeight()) * 0.5f;
            bool act = sortOf[c] >= 0 && s_nSortCol == sortOf[c];
            dl->AddText(ImVec2(p.x + cx[c], ty), act ? IM_COL32_WHITE : IM_COL32(170, 176, 184, 255), ht);
            if(act)
            {
                float ax = p.x + cx[c] + ImGui::CalcTextSize(ht).x + UI::S(12), ay = p.y + hdrH * 0.5f, s = UI::S(5);
                if(s_bSortAsc) dl->AddTriangleFilled(ImVec2(ax - s, ay + s * 0.5f), ImVec2(ax + s, ay + s * 0.5f), ImVec2(ax, ay - s * 0.7f), IM_COL32_WHITE);
                else           dl->AddTriangleFilled(ImVec2(ax - s, ay - s * 0.5f), ImVec2(ax + s, ay - s * 0.5f), ImVec2(ax, ay + s * 0.7f), IM_COL32_WHITE);
            }
        }
        dl->AddText(ImVec2(p.x + cTags, p.y + (hdrH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(170, 176, 184, 255), "Tags");
        dl->PushClipRect(ImVec2(p.x + cLang, p.y), ImVec2(p.x + cPl - UI::S(6), p.y + hdrH), true);
        dl->AddText(ImVec2(p.x + cLang, p.y + (hdrH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(170, 176, 184, 255), "Language");
        dl->PopClipRect();
        dl->AddLine(ImVec2(p.x, p.y + hdrH), ImVec2(p.x + W, p.y + hdrH), IM_COL32(255, 255, 255, 34), UI::S(1));
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hdrH + UI::S(2)));
    }
    ImGui::BeginChild("##lrows", ImVec2(0, 0), false);
    dl = ImGui::GetWindowDrawList();
    W = ImGui::GetContentRegionAvail().x;
    char key[48];
    s_nLSel = -1;
    for(int i = 0; i < (int)rows.size(); ++i) { LRowKey(rows[i], key, sizeof(key)); if(s_szLSelKey[0] && !strcmp(key, s_szLSelKey)) s_nLSel = i; }
    {   // ping stability graph samples (every row, once a second)
        static uint64_t last = 0; uint64_t now = MP_NowMs();
        if(now - last >= 1000)
        {
            last = now;
            for(auto& r : rows) { if(r.ping < 0) continue; LRowKey(r, key, sizeof(key)); auto& v = s_PingHist[key]; v.push_back(r.ping); if(v.size() > 40) v.erase(v.begin()); }
            if(s_PingHist.size() > 300) s_PingHist.clear();
        }
    }
    for(int i = 0; i < (int)rows.size(); ++i)
    {
        LRow& r = rows[i];
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        bool tap = ImGui::InvisibleButton("##r", ImVec2(W, rowH));
        bool sel = s_nLSel == i;
        bool vis = ImGui::IsItemVisible() || sel;
        ImU32 mc = LModeColor(r);
        if(vis)
        {
            // row: slate base, the server background picture on the left fading into the row (CS:GO)
            dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), (i & 1) ? IM_COL32(30, 34, 40, 110) : IM_COL32(44, 49, 56, 110));   // 4.05.1: see-through rows
            ImVec2 uv(1, 1);
            if(void* bt = LRowBg(r, &uv))
            {
                float bw = W * 0.46f;
                LImageCoverFade(dl, bt, uv, a, ImVec2(a.x + bw, a.y + rowH), 150, 0.45f);   // 4.08: soft fade on the right edge, no cut
            }
            if(r.kind >= 3 && r.kind <= 6)   // featured: gamemode colour highlight
                dl->AddRectFilledMultiColor(a, ImVec2(a.x + W, a.y + rowH), VAlpha(mc, 0.30f), VAlpha(mc, 0.04f), VAlpha(mc, 0.04f), VAlpha(mc, 0.30f));
            if((r.kind != 1 && r.kind != 2) || r.modeCol) dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), a.y + rowH), mc);   // gamemode colour stripe
            if(sel) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 26));
            else if(ImGui::IsItemActive()) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 14));
            dl->AddLine(ImVec2(a.x, a.y + rowH), ImVec2(a.x + W, a.y + rowH), IM_COL32(0, 0, 0, 90));
        }
        if(tap)
        {
            LRowKey(r, key, sizeof(key));
            if(sel) { MP_CopyStr(s_szLSelKey, "", sizeof(s_szLSelKey)); s_nLSel = -1; sel = false; }   // tap again = close the details (CONNECT joins)
            else { MP_CopyStr(s_szLSelKey, key, sizeof(s_szLSelKey)); s_nLSel = i; sel = true; }
        }
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) LJoin(r);   // double tap = join
        if(vis)
        {
            float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            if(r.locked) LPadlock(dl, ImVec2(a.x + cLock + UI::S(16), a.y + rowH * 0.5f - UI::S(3)), UI::S(20), true, IM_COL32(230, 230, 230, 255));
            ImU32 nameCol = (r.live || sel) ? IM_COL32_WHITE : IM_COL32(175, 180, 186, 255);
            dl->PushClipRect(ImVec2(a.x + cName, a.y), ImVec2(a.x + cMode - UI::S(10), a.y + rowH), true);
            float nx = a.x + cName;
            if(r.on) { dl->AddCircleFilled(ImVec2(nx + UI::S(7), a.y + rowH * 0.5f), UI::S(6), IM_COL32(90, 200, 90, 255)); nx += UI::S(22); }
            if(LIsFav(r.name, r.port)) { LStar(dl, ImVec2(nx + UI::S(9), a.y + rowH * 0.5f), UI::S(9), IM_COL32(240, 200, 60, 255), true); nx += UI::S(24); }
            char nm[80]; snprintf(nm, sizeof(nm), "%s%s", r.name, r.kind == 0 ? "  (default)" : (r.mine ? "  (yours)" : ""));
            dl->AddText(ImVec2(nx, ty), nameCol, nm);
            dl->PopClipRect();
            // gamemode chip
            const char* md = r.mode[0] ? r.mode : "Freeroam";
            LChip(dl, ImVec2(a.x + cMode, a.y + rowH * 0.5f - UI::S(15)), UI::S(30), md, mc, cTags - cMode - UI::S(8));
            // tags (comma list, dim)
            dl->PushClipRect(ImVec2(a.x + cTags, a.y), ImVec2(a.x + cLang - UI::S(10), a.y + rowH), true);
            {
                char tg[MP_TAGS_LEN + 16]; MP_CopyStr(tg, r.tags[0] ? r.tags : "-", sizeof(tg));   // 4.05.3: tags only
                for(char* c = tg; *c; ++c) if(*c == ',') *c = ' ';
                std::string t2; for(char* c = tg; *c; ++c) { t2 += *c; if(*c == ' ' && c[1] && c[1] != ' ') t2.insert(t2.size() - 1, ","); }
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(a.x + cTags, a.y + (rowH - ImGui::GetFontSize() * 0.82f) * 0.5f), IM_COL32(160, 166, 174, 255), t2.c_str());
            }
            dl->PopClipRect();
            // 4.05.3: language column
            dl->PushClipRect(ImVec2(a.x + cLang, a.y), ImVec2(a.x + cPl - UI::S(10), a.y + rowH), true);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(a.x + cLang, a.y + (rowH - ImGui::GetFontSize() * 0.82f) * 0.5f), IM_COL32(190, 196, 204, 255), r.lang[0] ? r.lang : "-");
            dl->PopClipRect();
            char b[48];
            if(r.players >= 0) snprintf(b, sizeof(b), "%d/%d", r.players, r.maxp); else snprintf(b, sizeof(b), "-");
            bool full = r.players >= 0 && r.maxp > 0 && r.players >= r.maxp;
            dl->AddText(ImVec2(a.x + cPl, ty), full ? IM_COL32(222, 74, 60, 255) : (r.players > 0 ? IM_COL32(230, 200, 90, 255) : IM_COL32(200, 204, 210, 255)), b);
            const char* map = r.kind == 3 ? "Bone County" : r.kind == 5 ? "Mega Vil" : r.kind == 6 ? "SA Wasteland" : r.kind == 4 ? "SA Warzone" : "San Andreas";
            dl->PushClipRect(ImVec2(a.x + cMap, a.y), ImVec2(a.x + cPing - UI::S(8), a.y + rowH), true);
            dl->AddText(ImVec2(a.x + cMap, ty), IM_COL32(200, 204, 210, 255), map);
            dl->PopClipRect();
            if(r.ping >= 0) snprintf(b, sizeof(b), "%d", r.ping); else snprintf(b, sizeof(b), "-");
            ImU32 pc = r.ping < 0 ? IM_COL32(150, 150, 150, 255) : r.ping < 80 ? IM_COL32(110, 200, 90, 255) : r.ping < 150 ? IM_COL32(230, 200, 90, 255) : IM_COL32(222, 74, 60, 255);
            dl->AddText(ImVec2(a.x + cPing, ty), pc, b);
            float fh = rowH * 0.40f, fw = fh * 4.0f / 3.0f;
            ImVec2 fa(a.x + cLoc, a.y + (rowH - fh) * 0.5f);
            LDrawFlag(dl, LCountryIndex(r.cc), fa, ImVec2(fa.x + fw, fa.y + fh));
        }
        if(sel)
        {   // ---- expanded CS:GO detail strip ----
            ImVec2 d = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(W, detH));
            ImVec2 e(d.x + W, d.y + detH);
            dl->AddRectFilled(d, e, IM_COL32(24, 27, 32, 200));
            dl->AddRect(ImVec2(a.x, a.y), e, IM_COL32(235, 238, 242, 220), 0.0f, 0, UI::S(2));   // white selection frame (row + details)
            const float pad = UI::S(14), lh = ImGui::GetTextLineHeight();
            float c1 = d.x + pad, c2 = d.x + W * 0.34f, c3 = d.x + W * 0.52f, c4 = d.x + W * 0.67f, c5 = d.x + W * 0.84f;
            ImU32 hc = IM_COL32(150, 157, 166, 255), tc = IM_COL32(222, 226, 231, 255);
            float y0 = d.y + pad;
            // 1: description, IP, favourite
            dl->AddText(ImVec2(c1, y0), hc, "Server Description");
            LWrapText(dl, ImVec2(c1, y0 + lh + UI::S(6)), c2 - c1 - pad, e.y - lh * 2.6f, tc, r.desc[0] ? r.desc : "No description.");
            {
                char ip[96];
                if(r.kind == 2 && r.saved >= 0) { ServerEntry* se = ServerList::Get(r.saved); snprintf(ip, sizeof(ip), "IP: %s:%d", se ? se->host : "?", r.port); }
                else if(r.kind == 1 && !r.internet && r.ip[0]) snprintf(ip, sizeof(ip), "IP: %s:%d (Wi-Fi)", r.ip, r.port);
                else if(r.kind == 0 || r.kind >= 3) snprintf(ip, sizeof(ip), "IP: 24/7 internet lobby, port %d", r.port);
                else if(r.internet) snprintf(ip, sizeof(ip), "IP: internet lobby, port %d", r.port);
                else snprintf(ip, sizeof(ip), "IP: hidden, port %d", r.port);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, ImVec2(c1, e.y - pad - lh), hc, ip);
                // Favourite toggle (star)
                bool fav = LIsFav(r.name, r.port);
                ImVec2 sa(c2 - pad - UI::S(150), e.y - pad - lh - UI::S(10)), sb(c2 - pad, e.y - pad + UI::S(4));
                ImGui::SetCursorScreenPos(sa);
                if(ImGui::InvisibleButton("##fav", ImVec2(sb.x - sa.x, sb.y - sa.y))) { LSetFav(r.name, r.port, !fav); fav = !fav; }
                dl->AddRectFilled(sa, sb, ImGui::IsItemActive() ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 14));
                dl->AddText(ImVec2(sa.x + UI::S(10), sa.y + (sb.y - sa.y - lh) * 0.5f), tc, "Favorite");
                LStar(dl, ImVec2(sb.x - UI::S(22), (sa.y + sb.y) * 0.5f), UI::S(11), IM_COL32(240, 200, 60, 255), fav);
                if(r.kind == 2 && r.saved >= 0 && !r.mine)
                {   // saved server: Delete (the old trash button)
                    ImVec2 da(sa.x - UI::S(130), sa.y), db(sa.x - UI::S(10), sb.y);
                    ImGui::SetCursorScreenPos(da);
                    if(ImGui::InvisibleButton("##del", ImVec2(db.x - da.x, db.y - da.y))) { ServerList::Remove(r.saved); s_szLSelKey[0] = 0; }
                    dl->AddRectFilled(da, db, ImGui::IsItemActive() ? IM_COL32(222, 74, 60, 120) : IM_COL32(222, 74, 60, 50));
                    dl->AddText(ImVec2(da.x + UI::S(14), da.y + (db.y - da.y - lh) * 0.5f), tc, "Delete");
                }
            }
            // 2: gamemode + tags
            dl->AddText(ImVec2(c2, y0), hc, r.mode[0] ? r.mode : "Freeroam");
            {
                char tl[MP_TAGS_LEN + 8]; MP_CopyStr(tl, r.tags[0] ? r.tags : "-", sizeof(tl));
                std::string t; for(char* c = tl; *c; ++c) { t += *c == ',' ? ", " : std::string(1, *c); }
                dl->AddText(ImVec2(c3 - UI::S(40) > c2 ? c2 : c2, y0 + lh * 1.3f), hc, "Tags");
                LWrapText(dl, ImVec2(c2, y0 + lh * 2.4f), c3 - c2 - pad, e.y - pad, tc, t.c_str());
            }
            // 3: all players
            {
                std::vector<std::string> pl; LDetailPlayers(r, pl);
                char hd[48]; snprintf(hd, sizeof(hd), "All players (%d)", r.players >= 0 ? r.players : (int)pl.size());
                dl->AddText(ImVec2(c3, y0), hc, hd);
                float yy = y0 + lh + UI::S(8); int shown = 0;
                dl->PushClipRect(ImVec2(c3, d.y), ImVec2(c4 - pad, e.y), true);
                for(auto& n : pl)
                {
                    if(yy + lh > e.y - pad - lh) { char more[32]; snprintf(more, sizeof(more), "+%d more", (int)pl.size() - shown); dl->AddText(ImVec2(c3, yy), hc, more); break; }
                    dl->AddRectFilled(ImVec2(c3, yy + lh * 0.2f), ImVec2(c3 + lh * 0.6f, yy + lh * 0.8f), IM_COL32(126, 178, 226, 255));
                    dl->AddText(ImVec2(c3 + lh, yy), tc, n.c_str()); yy += lh * 1.15f; ++shown;
                }
                if(pl.empty()) dl->AddText(ImVec2(c3, yy), hc, r.players > 0 ? "Join to see who is playing." : "Nobody inside yet.");
                dl->PopClipRect();
            }
            // 4: preview image
            {
                ImVec2 uv(1, 1); ImVec2 pa(c4, y0), pb(c5 - pad, e.y - pad);
                float ph = (pb.x - pa.x) * 0.5f; if(ph < pb.y - pa.y) { pa.y += (pb.y - pa.y - ph) * 0.5f; pb.y = pa.y + ph; }
                void* bt = LRowPreview(r, &uv);
                if(!bt) bt = LRowBg(r, &uv);
                if(bt) LImageCover(dl, bt, uv, pa, pb, IM_COL32_WHITE);
                else { dl->AddRectFilled(pa, pb, IM_COL32(24, 27, 32, 255)); dl->AddText(ImVec2(pa.x + UI::S(10), pa.y + UI::S(8)), hc, "No preview"); }
                dl->AddRect(pa, pb, IM_COL32(255, 255, 255, 50));
            }
            // 5: Server location + ping stability graph
            {
                const char* cn = LCountryName(r.cc);
                char loc[96]; snprintf(loc, sizeof(loc), "Server location: %s", cn ? cn : Online::RegionLabel(r.region));
                float fh = lh, fw = fh * 4.0f / 3.0f;
                LDrawFlag(dl, LCountryIndex(r.cc), ImVec2(c5, y0), ImVec2(c5 + fw, y0 + fh));
                dl->PushClipRect(ImVec2(c5, d.y), ImVec2(e.x - UI::S(6), e.y), true);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f, ImVec2(c5 + fw + UI::S(6), y0 + UI::S(2)), tc, loc);
                LRowKey(r, key, sizeof(key));
                const std::vector<int>* hv = nullptr; auto it = s_PingHist.find(key); if(it != s_PingHist.end()) hv = &it->second;
                ImVec2 ga(c5, y0 + lh * 2.2f), gb(e.x - pad, e.y - pad - lh * 1.2f);
                dl->AddRectFilled(ga, gb, IM_COL32(20, 23, 27, 255)); dl->AddRect(ga, gb, IM_COL32(255, 255, 255, 40));
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f, ImVec2(c5, ga.y - lh * 0.95f), hc, "Ping stability");
                int mx = 100, sum = 0, mn = 99999, mxp = 0, n = hv ? (int)hv->size() : 0;
                for(int k = 0; k < n; ++k) { int v = (*hv)[k]; mx = ImMax(mx, v); sum += v; mn = ImMin(mn, v); mxp = ImMax(mxp, v); }
                float bw = (gb.x - ga.x) / 40.0f;
                for(int k = 0; k < n; ++k)
                {
                    int v = (*hv)[k]; float hh = (gb.y - ga.y - UI::S(4)) * ImClamp((float)v / mx, 0.04f, 1.0f);
                    float x = gb.x - (n - k) * bw;
                    ImU32 bc = v < 80 ? IM_COL32(96, 186, 80, 255) : v < 150 ? IM_COL32(220, 190, 70, 255) : IM_COL32(210, 70, 56, 255);
                    dl->AddRectFilled(ImVec2(x + 1, gb.y - hh), ImVec2(x + bw - 1, gb.y - 1), bc);
                }
                char st[64];
                if(n) snprintf(st, sizeof(st), "avg %d ms   jitter %d ms", sum / n, mxp - mn); else snprintf(st, sizeof(st), "no ping yet");
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f, ImVec2(c5, e.y - pad - lh), hc, st);
                dl->PopClipRect();
            }
            ImGui::SetCursorScreenPos(ImVec2(d.x, e.y));
            ImGui::Dummy(ImVec2(W, 0));
        }
        ImGui::PopID();
    }
    if(rows.empty())
        ImGui::TextDisabled(s_szSearch[0] ? "No server matches \"%s\"." : (s_nLTab == LT_FAVORITES ? "No favourite servers yet. Tap a server and press Favorite, or tap + to add one." :
                            s_nLTab == LT_LAN ? "No Wi-Fi servers on this network." : "Searching..."), s_szSearch);
    LDragScroll();
    ImGui::EndChild();
    ImGui::EndChild();
}

// ---- right panel: players + rules ----------------------------------------------------------------
static void LTwoCol(ImDrawList* dl, ImVec2 p, float w, float h, const char* a, const char* b, bool header, float split = 0.42f)
{
    float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    ImU32 c = header ? IM_COL32(205, 205, 205, 255) : IM_COL32_WHITE;
    dl->PushClipRect(p, ImVec2(p.x + w * split - UI::S(6), p.y + h), true);
    dl->AddText(ImVec2(p.x + UI::S(14), ty), c, a);
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(p.x + w * split, p.y), ImVec2(p.x + w, p.y + h), true);
    dl->AddText(ImVec2(p.x + w * split, ty), header ? c : IM_COL32(230, 230, 230, 255), b);
    dl->PopClipRect();
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, header ? 30 : 14));
}

static void LRightPanel(const LRow* r, float w, float h)
{
    ImGui::BeginChild("##lright", ImVec2(w, h), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rh = UI::S(56);
    float W = ImGui::GetContentRegionAvail().x;
    float half = h * 0.5f;
    // players
    ImGui::BeginChild("##lpl", ImVec2(W, half - UI::S(4)), false);
    dl = ImGui::GetWindowDrawList();
    { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, "Player", "Score", true, 0.70f); ImGui::Dummy(ImVec2(W, rh + UI::S(2))); }
    if(r && r->on && IsConnected())
    {
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(i != myId && !players[i].active) continue;
            ImVec2 p = ImGui::GetCursorScreenPos();
            char sc[16]; snprintf(sc, sizeof(sc), "%d", i == myId ? s_nMyScore : players[i].score);
            const char* nm = i == myId ? settings.name : players[i].name;
            LTwoCol(dl, p, W, rh, nm, sc, false, 0.70f);
            ImGui::Dummy(ImVec2(W, rh));
        }
    }
    else if(r && r->mine)
    {
        std::vector<MPServer::PlayerView> pv; s_Server.GetPlayers(pv);
        for(auto& v : pv) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, v.name, "-", false, 0.70f); ImGui::Dummy(ImVec2(W, rh)); }
        if(pv.empty()) ImGui::TextDisabled("  Nobody on your server yet.");
    }
    else ImGui::TextDisabled(r ? "  Join to see who is playing." : "  Select a server.");
    LDragScroll();
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
    { ImVec2 p = ImGui::GetCursorScreenPos(); dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + W, p.y), IM_COL32(255, 255, 255, 40), UI::S(2)); ImGui::Dummy(ImVec2(W, UI::S(4))); }
    // rules
    ImGui::BeginChild("##lrules", ImVec2(W, 0), false);
    dl = ImGui::GetWindowDrawList();
    { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, "Rule", "Value", true); ImGui::Dummy(ImVec2(W, rh + UI::S(2))); }
    if(r)
    {
        uint32_t wm = MPServer::WorldMinute();
        char t[32]; snprintf(t, sizeof(t), "%02u:%02u", (unsigned)((wm / 60) % 24), (unsigned)(wm % 60));
        char pl[32]; if(r->players >= 0) snprintf(pl, sizeof(pl), "%d/%d", r->players, r->maxp); else snprintf(pl, sizeof(pl), "-");
        char ic[24]; snprintf(ic, sizeof(ic), "%s", r->icon > 0 ? "custom" : "default");   // 2.32: no icon numbers
        char pt[16]; snprintf(pt, sizeof(pt), "%d", r->port);
        const char* rules[][2] = {
            { "address",   r->kind == 0 ? "default (always on)" : (r->internet ? "internet lobby" : "hidden") },
            { "port",      pt },
            { "status",    r->on ? "connected" : (r->live ? "online" : "always active") },
            { "players",   pl },
            { "password",  r->locked ? "yes" : "no" },
            { "region",    r->region >= 0 && r->region < REGION_COUNT ? g_MPRegionNames[r->region] : "All" },
            { "language",  r->lang[0] ? r->lang : "-" },
            { "mapname",   "San Andreas" },
            { "version",   "MP 3.04" },
            { "worldtime", t },
            { "weather",   "server synced" },
            { "icon",      ic },
        };
        if(r->kind == 6)
        {   // 3.58
            const char* dz[][2] = {
                { "address",   "24/7 featured (Broker bot, GitHub Actions)" }, { "gamemode", MP_DZ_GAMEMODE }, { "port", pt },
                { "players",   pl }, { "spawns", "15 random places" }, { "loot", "houses, 19 gas stations, 10 military bases" },
                { "zombies",   "everywhere (more at night / in the fog)" }, { "parties", "max 4, unique colours" },
                { "anti-relog","disconnect in combat = death" }, { "cheats", "off (MP + CLEO + CheatMenu)" }, { "login", "required" }, { "version", "MP 3.58" },
            };
            for(auto& kv : dz) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
        }
        else if(r->kind == 5)
        {   // 3.25
            const char* rp[][2] = {
                { "address",   "24/7 featured (always on, VPS + Broker)" }, { "gamemode", "Roleplay RP (only)" }, { "port", pt },
                { "players",   pl }, { "city", "Mega Vil (Los Santos of the future)" }, { "jobs", "20 jobs, payday every 30 min" },
                { "phone",     "calls, SMS, bank, GPS, shop, games..." }, { "rules", "peaceful: only police / military on duty" },
                { "cheats",    "off (MP + CLEO menu)" }, { "login", "required" }, { "version", "MP 3.25" },
            };
            for(auto& kv : rp) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
        }
        else if(r->kind == 4)
        {   // 3.23
            const char* mw[][2] = {
                { "address",   "24/7 featured (always on, VPS)" }, { "gamemode", "Call Of Duty: Modern Warfare 3" }, { "port", pt },
                { "players",   pl }, { "teams", "Eurasia, Arabia, Soviet, USA, Australia, Brazil, Taliban, France" },
                { "zones",     "34 capture zones + 8 bases" }, { "extras", "6 DM arenas, duels, events, hourly tournament" },
                { "cheats",    "off (MP + CLEO menu)" }, { "login", "required" }, { "version", "MP 3.23" },
            };
            for(auto& kv : mw) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
        }
        else if(r->kind == 3)
        {   // 3.04
            const char* bcw[][2] = {
                { "address",   "private gamemode (always on)" }, { "gamemode", "Brief Case War" }, { "port", pt },
                { "players",   pl }, { "teams", "Gustav 10 vs Schelapz 10" }, { "starts at", "20 players" },
                { "maps",      "Bone County, San Fierro Bay" }, { "cheats", "off (MP + CLEO menu)" }, { "CheatMenu", "not allowed" },
                { "music",     "war_ambient.mp3 (45%)" }, { "version", "MP 3.04.6" },
            };
            for(auto& kv : bcw) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
        }
        else
        for(auto& kv : rules) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
    }
    LDragScroll();
    ImGui::EndChild();
    ImGui::EndChild();
}

// ---- admin pages: players (kick / ban) + ban list -------------------------------------------------
// 2.37: [ADMIN] SENJI manages the default server from his phone (the server checks the admin key)
// ---- 3.53: Server Management page --------------------------------------------------------------
// Only there while you are ON a server (it goes away when you leave). Everybody can look; who can change:
//   the 4 featured servers (default, Brief Case War, MW3, Mega Vil) -> [ADMIN] SENJI + moderators
//   (moderator.txt + the name Trunk) - also with many players on it, the first joiner is nobody special;
//   a custom server -> only its host owner (the phone that hosts it).
static bool LSrvMgmtAvailable() { return client.GetState() == NET_CONNECTED; }
static bool LSrvFeatured() { return MP::IsOnDefault() || BcwOnServer() || Mw3OnServer() || RpOnServer() || DzOnServer(); }
static void LSrvHeader(const char* t) { ImGui::Dummy(ImVec2(0, UI::S(10))); ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.15f, 1), "%s", t); ImGui::Separator(); }
static void LPageServerMgmt()
{
    if(!LSrvMgmtAvailable()) { s_nLTab = LT_INTERNET; return; }
    static uint64_t s_nAskAt = 0;
    const uint64_t now = MP_NowMs();
    const bool newSrv = client.GetServerBuild() >= 33900;
    // ask the server for the live state (at most every 4 s while the page is open)
    if(newSrv && now - s_nAskAt > 4000) { s_nAskAt = now; PktSrvSettings q; memset(&q, 0, sizeof(q)); q.type = PKT_SRV_SETTINGS; q.op = 0; client.Send(&q, sizeof(q)); }
    if(!s_SrvSet.have)
    {
        memset(&s_SrvSet.s, 0, sizeof(s_SrvSet.s));
        s_SrvSet.s.type = PKT_SRV_SETTINGS; s_SrvSet.s.modes = s_nServerModes; s_SrvSet.s.perms = s_nServerPerms;
        s_SrvSet.s.rulesOn = MP::IsOnDefault() ? 1 : 0;
    }
    const bool featured = LSrvFeatured();
    const bool staff = MP::IsLocalAdmin() || MP::IsLocalMod();
    const bool canEdit = newSrv && (s_SrvSet.s.can || (featured && staff) || (!featured && IsHosting()));
    PktSrvSettings& st = s_SrvSet.s;

    ImGui::SetWindowFontScale(1.15f);
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "Server Management");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("%s", client.GetServerName()[0] ? client.GetServerName() : "...");

    LSrvHeader("SERVER");
    if(featured)
    {
        ImGui::TextWrapped("Hosted by: " MP_BOT_NAME " (24/7 bot)  &  [MOD] Trunk (moderator)  &  [ADMIN] SENJI (admin)");
        ImGui::TextDisabled("Online 24/7 on the MP console - it never needs a player to start it.");
    }
    else if(IsHosting()) ImGui::TextWrapped("Hosted by: you (this phone is the host owner)");
    else ImGui::TextWrapped("Hosted by: the host owner's phone / VPS");
    ImGui::Text("Players: %d/%d     Ping: %d ms     Loss: %d%%     Jitter: %d ms%s", MP::OnlineCount(), MP::OnlineMax(), MP::DisplayPing(), client.GetLossPct(), MP::DisplayJitter(), client.IsWeakLink() ? "     (weak link: syncs sent twice)" : "");   // 4.05.8
    if(featured) ImGui::Text("Language: All  (fixed - nobody can change it on the featured servers)");
    else ImGui::Text("Language: %s", settings.serverLang[0] ? settings.serverLang : "All");
    if(canEdit) ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "You can change these settings (%s).", MP::IsLocalAdmin() ? "[ADMIN]" : MP::IsLocalMod() ? "[MOD]" : "host owner");
    else if(!newSrv) ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "This server runs an old MP: view only until it updates.");
    else ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "View only - %s.", featured ? "only the admin and the moderators change the featured servers" : "only the host owner changes this server");
    if(featured && !staff && MP::LocalModName()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "moderator.txt was not accepted here - type /modcheck in the chat to see why.");

    bool send = false;
    LSrvHeader("GAMEMODES");
    const bool modesFixed = featured && !MP::IsOnDefault();
    if(modesFixed) ImGui::TextDisabled("This featured server runs only its own gamemode.");
    if(!canEdit || modesFixed) ImGui::BeginDisabled();
    for(int i = 0; i < MP_GAMEMODE_COUNT; ++i)
    {
        bool on = (st.modes & kGameModes[i].bit) != 0;
        if(FloatMenu::ToggleSwitch(kGameModes[i].name, &on)) { st.modes ^= kGameModes[i].bit; send = true; }
    }
    if(!canEdit || modesFixed) ImGui::EndDisabled();

    LSrvHeader("PERMISSIONS");
    if(!canEdit) ImGui::BeginDisabled();
    for(int i = 0; i < SRV_PERM_ROWS; ++i)
    {
        bool on = (st.perms & kSrvPerms[i].bit) != 0;
        if(FloatMenu::ToggleSwitch(kSrvPerms[i].name, &on)) { st.perms ^= kSrvPerms[i].bit; send = true; }
    }
    { bool on = st.rulesOn != 0; if(FloatMenu::ToggleSwitch("Rules window after the login", &on)) { st.rulesOn = on ? 1 : 0; send = true; } }
    if(!canEdit) ImGui::EndDisabled();
    // 3.58: the anti-cheat gamemode (fly / teleport / airbreak / speed / rapid fire / CLEO + CheatMenu block)
    LSrvHeader("ANTI-CHEAT");
    {
        // 4.12.4: the default server: always OFF. The host owner of a custom server can ALWAYS switch it: the
        // switch used to stay grey until the server's settings answer arrived (pad unknown), and on the phone
        // that hosts it the answer sometimes never came - so it could be neither turned on nor off.
        const bool onDefault = MP::IsOnDefault();
        const bool ownHost = !featured && IsHosting() && s_Server.IsRunning() && !MP::IsHostingDefault();
        const bool known = (st.pad & 0x80) != 0, fixedOn = !onDefault && known && !(st.pad & 2) && featured;
        bool on = onDefault ? false : ownHost ? s_Server.AntiCheatOn() : known ? (st.pad & 1) != 0 : featured;
        const bool locked = onDefault || fixedOn || !(canEdit || ownHost) || (!known && !ownHost);
        if(locked) ImGui::BeginDisabled();
        if(FloatMenu::ToggleSwitch("Anti-cheat gamemode", &on))
        {
            st.pad = (uint8_t)((st.pad & ~1) | (on ? 1 : 0) | 0x80); send = true;
            if(ownHost) { settings.hostAc = on; SaveConfig(); s_Server.SetAntiCheat(on, settings.hostAcMask); SysMsg(on ? COL_GREEN : COL_YELLOW, "Anti-cheat %s on \"%s\".", on ? "on" : "off", s_Server.GetName()); }
        }
        if(locked) ImGui::EndDisabled();
        if(onDefault) ImGui::TextDisabled("Always OFF on the default server.");
        else if(fixedOn) ImGui::TextDisabled("Always ON on Brief Case War, MW3 and Mega Vil.");
        ImGui::TextDisabled("Checks: teleport, airbreak, fly (on foot only - helicopters, jets and cars in the air are fine),");
        ImGui::TextDisabled("speed, rapid fire, infinite ammo, packet flood. Floating / teleporting: kick, 3 kicks = permanent ban");
        ImGui::TextDisabled("(only [ADMIN] / [MOD] can unban). CLEO menus and libCheatMenu.so / libCheatMenu64.so are blocked.");
    }
    if(send && canEdit) { SrvSettingsSend(); s_SrvSet.at = now; s_nAskAt = now; }

    LSrvHeader("MORE");
    if(FloatMenu::BigButton("Players (kick / ban / spectate)")) s_nLTab = LT_PLAYERS;
    if(FloatMenu::BigButton("Ban list")) s_nLTab = LT_BANS;
    if(FloatMenu::BigButton("Refresh")) { s_nAskAt = 0; }
}

// 3.53: controller LB / RB - the pages in the order of the side bar
bool MP::LauncherPadPage(int dir)
{
    if(!FloatMenu::IsModal()) return false;
    int order[64]; int n = 0;
    order[n++] = LT_INTERNET; order[n++] = LT_FAVORITES; order[n++] = LT_ADD; order[n++] = LT_CLOUD; order[n++] = LT_PLAYERS; order[n++] = LT_BANS;
    if(LSrvMgmtAvailable()) order[n++] = LT_SRVMGMT;
    for(int i = 0; i < FloatMenu::SectionCount() && n < 64; ++i) order[n++] = LT_SECTIONS + i;
    int at = 0; for(int i = 0; i < n; ++i) if(order[i] == s_nLTab) { at = i; break; }
    at = (at + dir + n) % n;
    s_nLTab = order[at];
    return true;
}

static void LPagePlayersAdmin()
{
    ImGui::Text(MP::IsLocalMod() ? "This server   |   Moderator: " : "Default server   |   Administrator: ");
    ImGui::SameLine(0, 0);
    { ImVec2 p = ImGui::GetCursorScreenPos(); const char* me = players[myId].name; UI::NameText(ImGui::GetWindowDrawList(), ImGui::GetFontSize(), p, IM_COL32_WHITE, me, 1.0f, false);
      ImGui::Dummy(ImVec2(ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, me).x, ImGui::GetTextLineHeight())); }
    ImGui::Separator();
    float listH = ImGui::GetContentRegionAvail().y * 0.48f;
    ImGui::BeginChild("##mga", ImVec2(0, listH), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x, rh = UI::S(60);
    int n = 0; bool selOk = false;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active || i == myId) continue;
        ++n;
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##p", ImVec2(W, rh))) s_nMgmtSel = (s_nMgmtSel == i) ? -1 : i;
        bool sel = s_nMgmtSel == i;
        if(sel) { dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(190, 25, 25, 200), 0.0f); selOk = true; }
        float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        char b[24]; snprintf(b, sizeof(b), "%d", i);
        dl->AddText(ImVec2(a.x + UI::S(14), ty), IM_COL32_WHITE, b);
        UI::NameText(dl, ImGui::GetFontSize(), ImVec2(a.x + W * 0.1f, ty), sel ? IM_COL32_WHITE : UI::FromRGBA(p.color | 0xFF), p.name, 1.0f, false);
        snprintf(b, sizeof(b), p.ping >= 0 ? "%d ms" : "-", p.ping);
        dl->AddText(ImVec2(a.x + W * 0.8f, ty), IM_COL32(200, 200, 200, 255), b);
        ImGui::PopID();
    }
    if(!n) ImGui::TextDisabled("  Nobody else on the default server right now.");
    LDragScroll();
    ImGui::EndChild();
    if(!selOk) s_nMgmtSel = -1;
    ImGui::Separator();
    FloatMenu::TextField("Reason", s_szKickReason, "Reason (kick / ban)", MP_REASON_LEN - 1, OnKickReason);
    static const char* quick[] = { "Cheating", "Spamming", "Insulting", "Griefing", "Bug abuse", "AFK" };
    for(int i = 0; i < 6; ++i)
    {
        if(i) ImGui::SameLine();
        if(ImGui::Button(quick[i], ImVec2(0, UI::S(54)))) MP_CopyStr(s_szKickReason, quick[i], sizeof(s_szKickReason));
    }
    int t = s_nMgmtSel;
    bool can = t >= 0 && t < MP_MAX_PLAYERS && players[t].active;
    float q = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    if(!can) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.10f, 0.95f));
    if(ImGui::Button("Kick##ak", ImVec2(q, UI::S(72))) && can) { SendAdminCmd(ADMIN_KICK, t, 0, s_szKickReason); s_nMgmtSel = -1; }
    ImGui::PopStyleColor(); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.08f, 0.08f, 0.95f));
    if(ImGui::Button("Ban##ab", ImVec2(q, UI::S(72))) && can) { SendAdminCmd(ADMIN_BAN, t, 0, s_szKickReason); s_nMgmtSel = -1; }
    ImGui::PopStyleColor(); ImGui::SameLine();
    if(ImGui::Button("Mute##am", ImVec2(q * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, UI::S(72))) && can) SendAdminCmd(ADMIN_MUTE, t, 0, "");
    ImGui::SameLine();
    if(ImGui::Button("Unmute##au", ImVec2(q * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, UI::S(72))) && can) SendAdminCmd(ADMIN_UNMUTE, t, 0, "");
    {   // 3.54: the chat commands as buttons too
        float w4 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f;
        if(ImGui::Button("Warn##aw", ImVec2(w4, UI::S(64))) && can) SendAdminCmd2(ADMIN_WARN, t, 0, 0, s_szKickReason);
        ImGui::SameLine();
        if(ImGui::Button("Go to##ag", ImVec2(w4, UI::S(64))) && can) SendAdminCmd2(ADMIN_GOTO, t, 0, 0, "");
        ImGui::SameLine();
        if(ImGui::Button("Get here##ah", ImVec2(w4, UI::S(64))) && can) SendAdminCmd2(ADMIN_GETHERE, t, 0, 0, "");
        ImGui::SameLine();
        if(ImGui::Button("Heal##ahp", ImVec2(w4, UI::S(64))) && can) SendAdminCmd2(ADMIN_SETHEALTH, t, 0, 100, "");
    }
    if(!can) ImGui::EndDisabled();
    ImGui::TextDisabled("Tap a player, pick a reason, then Kick / Ban / Mute / Warn. The 24/7 server keeps its ban list.");
    ImGui::TextDisabled("Chat: /acmds lists every staff command (/sethealth /givecash /fine /makevip /makeadmin /unban ...).");
}

static void LPagePlayers()
{
    // 3.39: admin + moderators manage the 4 featured servers only; a custom server is managed by its host owner
    const bool onFeatured = client.GetState() == NET_CONNECTED && (MP::IsOnDefault() || BcwOnServer() || Mw3OnServer() || RpOnServer() || DzOnServer());
    if(client.GetState() == NET_CONNECTED && FloatMenu::BigButton("Server Management (gamemodes / permissions / rules)")) s_nLTab = LT_SRVMGMT;   // 3.53
    if(onFeatured && !MP::IsLocalMod() && !MP::IsDefaultAdmin() && MP::LocalModName())   // 3.50.1
    { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.85f, 0.2f, 1)); ImGui::TextWrapped("moderator.txt has the MP moderator key, but this server didn't give you [MOD] (older MP copy). Type /modcheck, then rejoin."); ImGui::PopStyleColor(); }
    if((MP::IsDefaultAdmin() || MP::IsLocalMod()) && onFeatured) { LPagePlayersAdmin(); return; }   // 3.35: [MOD] kicks / mutes too   // 2.37
    if(MP::IsHostingDefault())
    {
        ImGui::TextUnformatted("Player management");
        ImGui::TextDisabled("The default server has one administrator: [ADMIN] SENJI. Joining first (or hosting it) doesn't make you the host.");
        ImGui::TextDisabled("Want your own rules? Add a server with your own name (Add server -> Host this server).");
        if(FloatMenu::BigButton("Go to Add server")) s_nLTab = LT_ADD;
        return;
    }
    if(!s_Server.IsRunning())
    {
        ImGui::TextUnformatted("Player management");
        ImGui::TextDisabled("Only the administrator can kick and ban: the phone that hosts the server.");
        ImGui::TextDisabled("Host a server first (Add server -> Host this server), then your players show up here.");
        if(FloatMenu::BigButton("Go to Add server")) s_nLTab = LT_ADD;
        return;
    }
    std::vector<MPServer::PlayerView> pv; s_Server.GetPlayers(pv);
    ImGui::Text("Server: %s   |   Administrator: %s   |   %d/%d players", s_Server.GetName(), settings.name, (int)pv.size(), s_Server.GetMaxPlayers());
    ImGui::Separator();
    float listH = ImGui::GetContentRegionAvail().y * 0.48f;
    ImGui::BeginChild("##mgl", ImVec2(0, listH), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x, rh = UI::S(60);
    {
        ImVec2 p = ImGui::GetCursorScreenPos(); ImU32 tc = IM_COL32(205, 205, 205, 255);
        float ty = p.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p.x + UI::S(14), ty), tc, "id"); dl->AddText(ImVec2(p.x + W * 0.1f, ty), tc, "Player");
        dl->AddText(ImVec2(p.x + W * 0.52f, ty), tc, "Phone id"); dl->AddText(ImVec2(p.x + W * 0.8f, ty), tc, "Online");
        dl->AddLine(ImVec2(p.x, p.y + rh), ImVec2(p.x + W, p.y + rh), IM_COL32(255, 255, 255, 30), UI::S(2));
        ImGui::Dummy(ImVec2(W, rh + UI::S(2)));
    }
    bool selOk = false;
    const MPServer::PlayerView* selP = nullptr;
    for(auto& v : pv)
    {
        ImGui::PushID(v.id);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##p", ImVec2(W, rh))) s_nMgmtSel = (s_nMgmtSel == v.id) ? -1 : v.id;
        bool sel = s_nMgmtSel == v.id;
        if(sel) { dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(190, 25, 25, 200), 0.0f); selOk = true; selP = &v; }
        float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        char b[64]; snprintf(b, sizeof(b), "%d", v.id);
        dl->AddText(ImVec2(a.x + UI::S(14), ty), IM_COL32_WHITE, b);
        bool me = v.uid[0] && !strcmp(v.uid, settings.deviceId);
        snprintf(b, sizeof(b), "%s%s", v.name, me ? "  (you, administrator)" : "");
        dl->AddText(ImVec2(a.x + W * 0.1f, ty), sel ? IM_COL32_WHITE : UI::FromRGBA(v.color | 0xFF), b);
        dl->AddText(ImVec2(a.x + W * 0.52f, ty), IM_COL32(200, 200, 200, 255), v.uid[0] ? v.uid : "old version");
        uint64_t mins = (MP_NowMs() - v.joinedMs) / 60000;
        snprintf(b, sizeof(b), "%llu min", (unsigned long long)mins);
        dl->AddText(ImVec2(a.x + W * 0.8f, ty), IM_COL32(200, 200, 200, 255), b);
        dl->AddLine(ImVec2(a.x, a.y + rh), ImVec2(a.x + W, a.y + rh), IM_COL32(255, 255, 255, 14));
        ImGui::PopID();
    }
    if(pv.empty()) ImGui::TextDisabled("  Nobody on your server yet.");
    LDragScroll();
    ImGui::EndChild();
    if(!selOk) s_nMgmtSel = -1;
    ImGui::Separator();
    FloatMenu::TextField("Reason", s_szKickReason, "Reason (kick / ban)", MP_REASON_LEN - 1, OnKickReason);
    static const char* quick[] = { "Cheating", "Spamming", "Insulting", "Griefing", "Bug abuse", "AFK" };
    for(int i = 0; i < 6; ++i)
    {
        if(i) ImGui::SameLine();
        if(ImGui::Button(quick[i], ImVec2(0, UI::S(54)))) MP_CopyStr(s_szKickReason, quick[i], sizeof(s_szKickReason));
    }
    bool me = selP && selP->uid[0] && !strcmp(selP->uid, settings.deviceId);
    bool can = selP && !me;
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if(!can) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.10f, 0.95f));
    char lbl[96]; snprintf(lbl, sizeof(lbl), "Kick %s", selP ? selP->name : "");
    if(ImGui::Button(lbl, ImVec2(half, UI::S(72))) && selP)
    {
        int id = selP->id; char nm[MP_NAME_LEN]; MP_CopyStr(nm, selP->name, sizeof(nm));
        if(s_Server.Kick(id, settings.name, s_szKickReason, false)) SysMsg(COL_YELLOW, "Kicked %s.", nm);
        s_nMgmtSel = -1;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.08f, 0.08f, 0.95f));
    snprintf(lbl, sizeof(lbl), "Ban %s", selP ? selP->name : "");
    if(ImGui::Button(lbl, ImVec2(half, UI::S(72))) && selP)
    {
        int id = selP->id; char nm[MP_NAME_LEN]; MP_CopyStr(nm, selP->name, sizeof(nm));
        if(s_Server.Kick(id, settings.name, s_szKickReason, true)) SysMsg(COL_RED, "Banned %s.", nm);
        s_nMgmtSel = -1;
    }
    ImGui::PopStyleColor();
    if(!can) ImGui::EndDisabled();
    if(me) ImGui::TextDisabled("That is you (the administrator).");
    else ImGui::TextDisabled("Tap a player, type or pick a reason, then Kick or Ban. Banned players see the date, you as administrator and the reason.");
}

static void LPageBans()
{
    // 3.58: on a server this phone doesn't host (the 24/7 featured servers, a friend's server) the list
    // lives THERE - "Ban name" used to write this phone's own list, so the player was never banned.
    const bool remote = client.GetState() == NET_CONNECTED && !s_Server.IsRunning();
    if(remote)
    {
        const bool staff = MP::IsDefaultAdmin() || MP::IsLocalMod() || AdmLocalStaff();
        ImGui::Text("Bans of the server you are on");
        ImGui::TextDisabled("This server is not hosted by your phone: bans and unbans go to the server itself.");
        ImGui::TextDisabled("Online players: Players page -> tap -> Ban. Offline players / any name: here.");
        ImGui::Separator();
        if(!staff) { ImGui::TextWrapped("Only [ADMIN], [MOD] or the owner of this server can ban here."); return; }
        if(FloatMenu::BigButton("Show the ban list (chat)")) { SendAdminCmd2(ADMIN_BANS, -1, 0, 0, ""); SysMsg(COL_YELLOW, "The ban list is in the chat."); }
        ImGui::Separator();
        ImGui::TextUnformatted("Ban a name (online = kicked at once, or when they join)");
        FloatMenu::TextField("Player name", s_szBanName, "Player name", MP_NAME_LEN - 1, OnBanName);
        FloatMenu::TextField("Reason##bn", s_szKickReason, "Reason", MP_REASON_LEN - 1, OnKickReason);
        if(!s_szBanName[0]) ImGui::BeginDisabled();
        if(FloatMenu::BigButton("Ban name", IM_COL32(150, 25, 25, 240)))
        {
            char t[MP_REASON_LEN]; snprintf(t, sizeof(t), "%s\t%s", s_szBanName, s_szKickReason);
            SendAdminCmd2(ADMIN_BANNAME, -1, 0, 0, t);
            SysMsg(COL_RED, "Ban sent for %s.", s_szBanName); s_szBanName[0] = 0;
        }
        if(FloatMenu::BigButton("Unban this name"))
        {
            SendAdminCmd2(ADMIN_UNBAN, -1, 0, 0, s_szBanName);
            s_szBanName[0] = 0;
        }
        if(!s_szBanName[0]) ImGui::EndDisabled();
        return;
    }
    std::vector<MPServer::BanEntry> bans; s_Server.GetBans(bans);
    ImGui::Text("Ban list  (%d)   |   Administrator: %s", (int)bans.size(), settings.name);
    ImGui::TextDisabled("Saved in files/MP/bans.txt, used by every server this phone hosts.");
    ImGui::Separator();
    float listH = ImGui::GetContentRegionAvail().y * 0.55f;
    ImGui::BeginChild("##bl", ImVec2(0, listH), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x, rh = UI::S(60);
    {
        ImVec2 p = ImGui::GetCursorScreenPos(); ImU32 tc = IM_COL32(205, 205, 205, 255);
        float ty = p.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p.x + UI::S(14), ty), tc, "Player"); dl->AddText(ImVec2(p.x + W * 0.28f, ty), tc, "Ban date");
        dl->AddText(ImVec2(p.x + W * 0.5f, ty), tc, "Administrator"); dl->AddText(ImVec2(p.x + W * 0.7f, ty), tc, "Reason");
        dl->AddLine(ImVec2(p.x, p.y + rh), ImVec2(p.x + W, p.y + rh), IM_COL32(255, 255, 255, 30), UI::S(2));
        ImGui::Dummy(ImVec2(W, rh + UI::S(2)));
    }
    if(s_nBanSel >= (int)bans.size()) s_nBanSel = -1;
    for(int i = 0; i < (int)bans.size(); ++i)
    {
        const MPServer::BanEntry& b = bans[i];
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##b", ImVec2(W, rh))) s_nBanSel = (s_nBanSel == i) ? -1 : i;
        if(s_nBanSel == i) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(255, 255, 255, 30), 0.0f);
        float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        char d[32]; time_t tt = (time_t)b.when; struct tm lt; localtime_r(&tt, &lt); strftime(d, sizeof(d), "%Y-%m-%d %H:%M", &lt);
        auto col = [&](float x0, float x1, const char* s, ImU32 c) { dl->PushClipRect(ImVec2(a.x + x0, a.y), ImVec2(a.x + x1, a.y + rh), true); dl->AddText(ImVec2(a.x + x0, ty), c, s); dl->PopClipRect(); };
        col(UI::S(14), W * 0.27f, b.name[0] ? b.name : b.uid, IM_COL32_WHITE);
        col(W * 0.28f, W * 0.49f, d, IM_COL32(210, 210, 210, 255));
        col(W * 0.5f, W * 0.69f, b.admin, IM_COL32(210, 210, 210, 255));
        col(W * 0.7f, W, b.reason[0] ? b.reason : "-", IM_COL32(210, 210, 210, 255));
        dl->AddLine(ImVec2(a.x, a.y + rh), ImVec2(a.x + W, a.y + rh), IM_COL32(255, 255, 255, 14));
        ImGui::PopID();
    }
    if(bans.empty()) ImGui::TextDisabled("  Nobody is banned.");
    LDragScroll();
    ImGui::EndChild();
    if(s_nBanSel < 0) ImGui::BeginDisabled();
    if(FloatMenu::BigButton("Unban selected")) { s_Server.Unban(s_nBanSel); s_nBanSel = -1; }
    if(s_nBanSel < 0) ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::TextUnformatted("Ban a name (also when that player is offline)");
    FloatMenu::TextField("Player name", s_szBanName, "Player name", MP_NAME_LEN - 1, OnBanName);
    FloatMenu::TextField("Reason##bn", s_szKickReason, "Reason", MP_REASON_LEN - 1, OnKickReason);
    if(!s_szBanName[0]) ImGui::BeginDisabled();
    if(FloatMenu::BigButton("Ban name", IM_COL32(150, 25, 25, 240)))
    {
        if(s_Server.AddBan(s_szBanName, "", settings.name, s_szKickReason)) { SysMsg(COL_RED, "Banned the name %s (kicked if online).", s_szBanName); s_szBanName[0] = 0; }
        else SysMsg(COL_YELLOW, "%s is already banned.", s_szBanName);
    }
    if(!s_szBanName[0]) ImGui::EndDisabled();
}

// ---- the window -----------------------------------------------------------------------------------
// ---- 4.02: Cloud Storage - the resources the servers download (Project Day Z Legacy MTA models from
// the wagu repo / website hosting, Host/DayZ/models). Download a group before joining, or delete it
// from the phone and download it again (a broken copy, a new version, or to free space).
static void LFmtBytes(char* o, size_t n, unsigned long long b)
{
    if(b >= 1024ull * 1024 * 1024) snprintf(o, n, "%.2f GB", b / (1024.0 * 1024 * 1024));
    else if(b >= 1024ull * 1024) snprintf(o, n, "%.1f MB", b / (1024.0 * 1024));
    else snprintf(o, n, "%llu KB", (b + 1023) / 1024);
}
// ---- 4.12.3: Project Day Z model pack for XkiLoader -------------------------------------------------
// XkiLoader (release/libXkiLoader.so / libXkiLoader64.so, an AML mod) swaps models by file name from
// <game storage>/xkiloader/<folder>/. The pack (tools/make_dz_xkiloader.py) = the MTA DayZ models with
// their textures as PNG. Download it into files/MP/dayz/ (a real file), or import a zip from the Files app;
// both unpack to xkiloader/ProjectDayZ. Restart the game after.
static const char* const kDzXkiUrls[] = {
    "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/Host/DayZ/xkiloader/MP_ProjectDayZ_XkiLoader.zip",
    "https://github.com/ThirtyFoxMCx00E1/wagu/releases/download/DayZ-XkiLoader/MP_ProjectDayZ_XkiLoader.zip" };
static std::atomic<int> s_nXki{ 0 };            // 0 idle, 1 downloading, 2 unpacking, 3 done, 4 failed
static std::atomic<size_t> s_nXkiGot{ 0 };
static std::mutex s_XkiMx; static std::string s_XkiMsg;
static bool s_bXkiPick = false;                  // the Files app was opened by this page
static std::string DzXkiRoot()
{
    const char* ad = aml->GetAndroidDataPath();
    std::string own = std::string(ad && *ad ? ad : "/sdcard/Android/data/com.rockstargames.gtasa/files") + "/xkiloader";
    struct stat st;
    if(stat(own.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return own;
    if(stat("/sdcard/xkiloader", &st) == 0 && S_ISDIR(st.st_mode) && access("/sdcard/xkiloader", W_OK) == 0) return "/sdcard/xkiloader";
    return own;
}
static bool DzXkiInstalled()
{
    struct stat st; return stat((DzXkiRoot() + "/ProjectDayZ/README_MP.txt").c_str(), &st) == 0;
}
static void DzXkiUnpack(const std::string& zip)
{
    s_nXki = 2;
    std::string msg; const std::string dst = DzXkiRoot();
    bool ok = MP_UnZip(zip, dst, msg);
    struct stat st; if(ok && stat((dst + "/ProjectDayZ").c_str(), &st) != 0) { ok = false; msg = "no ProjectDayZ folder in that zip"; }
    { std::lock_guard<std::mutex> l(s_XkiMx); s_XkiMsg = ok ? (msg + " -> " + dst + "/ProjectDayZ. Restart the game (XkiLoader loads at start).") : msg; }
    logger->Info("4.12.3: XkiLoader pack %s: %s (%s)", ok ? "installed" : "failed", msg.c_str(), zip.c_str());
    s_nXki = ok ? 3 : 4;
}
static void DzXkiDownload()
{
    if(s_nXki == 1 || s_nXki == 2) return;
    s_nXki = 1; s_nXkiGot = 0;
    std::thread([]{
        const std::string dir = std::string(s_szDataDir) + "/dayz"; mkdir(dir.c_str(), 0775);
        const std::string zip = dir + "/MP_ProjectDayZ_XkiLoader.zip";
        std::string body, why;
        for(const char* u : kDzXkiUrls)
        {
            body.clear(); s_nXkiGot = 0;
            if(Online::HttpsFetch(u, body, 600000, why, 200u * 1024 * 1024, &s_nXkiGot) && body.size() > 1024 && body[0] == 'P' && body[1] == 'K') break;
            logger->Error("4.12.3: XkiLoader pack %s: %s", u, why.c_str()); body.clear();
        }
        if(body.empty()) { std::lock_guard<std::mutex> l(s_XkiMx); s_XkiMsg = "Download failed: " + (why.empty() ? std::string("no answer") : why); s_nXki = 4; return; }
        FILE* f = fopen(zip.c_str(), "wb");
        if(!f || fwrite(body.data(), 1, body.size(), f) != body.size()) { if(f) fclose(f); std::lock_guard<std::mutex> l(s_XkiMx); s_XkiMsg = "Can't write " + zip; s_nXki = 4; return; }
        fclose(f); body.clear(); body.shrink_to_fit();
        DzXkiUnpack(zip);
    }).detach();
}
static void DzXkiSection(float w)
{
    ImGui::TextColored(ImVec4(0.25f, 0.9f, 0.75f, 1), "XKILOADER MODEL PACK");
    ImGui::TextWrapped("The Project Day Z models as an XkiLoader pack (123 models, textures as PNG, about 50 MB). Needs XkiLoader "
                       "(libXkiLoader.so / libXkiLoader64.so) in your AML mods. Unpacks to %s/ProjectDayZ.", DzXkiRoot().c_str());
    if(DzXkiInstalled()) ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.6f, 1), "Installed. Restart the game after a new install.");
    // the Files app (only the zip this page asked for)
    if(s_bXkiPick)
    {
        const int fp = FilePick::State();
        if(fp == 3) { s_bXkiPick = false; std::string n = FilePick::Name(); FilePick::Reset();
                      std::string zip = std::string(s_szDataDir) + "/dayz/" + n; std::thread([zip]{ DzXkiUnpack(zip); }).detach(); }
        else if(fp == 4 || fp == 6) { s_bXkiPick = false; SysMsg(COL_RED, "Files app: %s", FilePick::Error().empty() ? "could not open the file" : FilePick::Error().c_str()); FilePick::Reset(); }
        else if(fp == 5 || fp == 0) { s_bXkiPick = false; FilePick::Reset(); }
        else if(fp == 1) ImGui::TextUnformatted("Waiting for the Files app...");
        else if(fp == 2) ImGui::Text("Copying... %.1f MB", FilePick::Copied() / 1048576.0);
    }
    const int st = s_nXki;
    if(st == 1) ImGui::ProgressBar(fminf(1.0f, (float)(s_nXkiGot / 1048576.0) / 50.0f), ImVec2(ImMax(UI::S(200), w - UI::S(20)), UI::S(22)), (std::to_string((int)(s_nXkiGot / 1048576)) + " MB").c_str());
    else if(st == 2) ImGui::TextUnformatted("Unpacking...");
    else if(st == 3 || st == 4) { std::lock_guard<std::mutex> l(s_XkiMx); ImGui::TextColored(st == 3 ? ImVec4(0.6f, 0.95f, 0.7f, 1) : ImVec4(1, 0.45f, 0.4f, 1), "%s", s_XkiMsg.c_str()); }
    const bool busy = st == 1 || st == 2 || s_bXkiPick;
    if(busy) ImGui::BeginDisabled();
    if(FloatMenu::BigButton(DzXkiInstalled() ? "Download the pack again" : "Download pack (into files/MP/dayz)")) DzXkiDownload();
    if(FloatMenu::BigButton("Import pack (zip) - opens Files"))
    {
        if(FilePick::Open((std::string(s_szDataDir) + "/dayz").c_str(), "application/zip,application/x-zip-compressed,application/x-zip,application/octet-stream")) s_bXkiPick = true;
        else SysMsg(COL_YELLOW, "The Files app is already open.");
    }
    if(busy) ImGui::EndDisabled();
    ImGui::TextDisabled("XkiLoader swaps these models everywhere (also outside Day Z). Turn the folder off in XkiLoader's menu or delete it for the normal models.");
}

static int s_nCloudAsk = -3;   // a delete waiting for "tap again" (group, -1 = all, -3 = none)
static uint64_t s_nCloudAskAt = 0;
static void LPageCloud(float w)
{
    static uint64_t askedAt = 0;
    DzModels::Cloud c; DzModels::GetCloud(c);
    if(!askedAt || (!c.listReady && !c.listBusy && !c.busy && c.cache && MP_NowMs() - askedAt > 15000)) { askedAt = MP_NowMs(); DzModels::CloudRefresh(); }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::TextColored(ImVec4(0.25f, 0.9f, 0.75f, 1), "CLOUD STORAGE");
    ImGui::TextDisabled("Server resources hosted on GitHub (wagu repo + website downloads). They are kept in the game cache.");
    ImGui::Separator();
    DzXkiSection(w);   // 4.12.3: the XkiLoader pack first (works without the file list)
    ImGui::Separator();
    if(!c.cache) { ImGui::TextWrapped("No writable cache folder on this phone - nothing can be downloaded."); return; }

    unsigned long long tot = 0, have = 0; int models = 0, fTot = 0, fHave = 0;
    for(auto& g : c.g) { tot += g.bytesTotal; have += g.bytesOnPhone; models += g.models; fTot += g.filesTotal; fHave += g.filesOnPhone; }
    // storage bar
    {
        char a[32], b[32], t[160]; LFmtBytes(a, sizeof(a), have); LFmtBytes(b, sizeof(b), tot);
        snprintf(t, sizeof(t), "Project Day Z Legacy model pack   %s of %s on this phone   (%d / %d files, %d models)", a, b, fHave, fTot, models);
        ImGui::TextUnformatted(t);
        ImVec2 p = ImGui::GetCursorScreenPos(); float bw = ImMax(UI::S(200), w - UI::S(20)), bh = UI::S(16);
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), IM_COL32(236, 232, 225, 30), bh * 0.5f);
        float k = tot ? (float)((double)have / (double)tot) : 0.0f;
        if(k > 0) dl->AddRectFilled(p, ImVec2(p.x + bw * ImClamp(k, 0.02f, 1.0f), p.y + bh), IM_COL32(64, 230, 190, 230), bh * 0.5f);
        ImGui::Dummy(ImVec2(bw, bh + UI::S(6)));
    }
    if(!c.listReady) { ImGui::TextDisabled("%s", c.listBusy ? "Getting the file list from GitHub..." : (c.msg[0] ? c.msg : "No file list yet.")); if(!c.listBusy && FloatMenu::BigButton("Try again")) DzModels::CloudRefresh(); return; }
    // progress
    if(c.busy)
    {
        if(c.gameDownload && c.group == -2) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "The Day Z server download is running - wait for it to finish.");
        else
        {
            char a[32], b[32]; LFmtBytes(a, sizeof(a), c.bytesDone); LFmtBytes(b, sizeof(b), c.bytesTotal);
            ImGui::Text("Downloading %s: %s / %s  (%d / %d files)", c.group < 0 ? "everything" : c.g[c.group].name, a, b, c.filesDone, c.filesTotal);
            ImGui::ProgressBar(c.bytesTotal ? (float)((double)c.bytesDone / (double)c.bytesTotal) : 0.0f, ImVec2(ImMax(UI::S(200), w - UI::S(20)), UI::S(22)), c.file);
        }
    }
    else if(c.msg[0]) ImGui::TextColored(ImVec4(0.6f, 0.95f, 0.7f, 1), "%s", c.msg);
    if(c.inUse > 0) ImGui::TextDisabled("%d models are in the game right now (Day Z server) - those are kept when you delete.", c.inUse);
    ImGui::Dummy(ImVec2(0, UI::S(6)));

    // "tap again" for a delete (3 s)
    if(s_nCloudAsk != -3 && MP_NowMs() - s_nCloudAskAt > 3000) s_nCloudAsk = -3;
    auto delButton = [&](int group, const char* id, float bw, bool enabled) -> void
    {
        const bool armed = s_nCloudAsk == group;
        char lb[48]; snprintf(lb, sizeof(lb), "%s%s", armed ? "Tap again to delete" : "Delete", id);
        if(!enabled) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button, armed ? ImVec4(0.75f, 0.12f, 0.15f, 1) : ImVec4(0.45f, 0.1f, 0.12f, 0.85f));
        if(ImGui::Button(lb, ImVec2(bw, UI::S(52))))
        {
            if(armed) { DzModels::CloudDelete(group); s_nCloudAsk = -3; }
            else { s_nCloudAsk = group; s_nCloudAskAt = MP_NowMs(); }
        }
        ImGui::PopStyleColor();
        if(!enabled) ImGui::EndDisabled();
    };
    // one row per folder
    const float bw = UI::S(190), rowH = UI::S(64);
    for(int i = 0; i < DzModels::kCloudGroups; ++i)
    {
        const DzModels::CloudGroup& g = c.g[i];
        if(!g.models) continue;
        ImGui::PushID(4200 + i);
        ImVec2 p = ImGui::GetCursorScreenPos(); float rw = ImMax(UI::S(300), w - UI::S(20));
        VChamfer(dl, p, ImVec2(p.x + rw, p.y + rowH), UI::S(8), kVPanel, kVLine, UI::S(1));
        LIcon(dl, g.filesOnPhone >= g.filesTotal ? IC_CHECK : IC_CLOUD, ImVec2(p.x + UI::S(30), p.y + rowH * 0.5f), UI::S(18), g.filesOnPhone >= g.filesTotal ? kVTeal : kVText);
        char a[32], b[32], t[160]; LFmtBytes(a, sizeof(a), g.bytesOnPhone); LFmtBytes(b, sizeof(b), g.bytesTotal);
        char nm[24]; snprintf(nm, sizeof(nm), "%s", g.name); if(nm[0]) nm[0] = (char)toupper(nm[0]);
        snprintf(t, sizeof(t), "%s  -  %d models", nm, g.models);
        dl->AddText(ImVec2(p.x + UI::S(60), p.y + UI::S(8)), kVText, t);
        snprintf(t, sizeof(t), "%s / %s on the phone  (%d / %d files)", a, b, g.filesOnPhone, g.filesTotal);
        dl->AddText(ImVec2(p.x + UI::S(60), p.y + UI::S(34)), kVDim, t);
        const float bx = p.x + rw - bw * 2 - UI::S(16);
        ImGui::SetCursorScreenPos(ImVec2(bx, p.y + (rowH - UI::S(52)) * 0.5f));
        const bool full = g.filesOnPhone >= g.filesTotal;
        if(c.busy || full) ImGui::BeginDisabled();
        if(ImGui::Button(full ? "Downloaded" : "Download", ImVec2(bw, UI::S(52)))) DzModels::CloudDownload(i);
        if(c.busy || full) ImGui::EndDisabled();
        ImGui::SameLine(0, UI::S(8));
        delButton(i, "", bw, !c.busy && g.filesOnPhone > 0);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + UI::S(6)));
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, UI::S(6)));
    if(c.busy || fHave >= fTot) ImGui::BeginDisabled();
    if(FloatMenu::BigButton("Download everything")) DzModels::CloudDownload(-1);
    if(c.busy || fHave >= fTot) ImGui::EndDisabled();
    ImGui::PushID(4299); delButton(-1, " everything (download again later)", ImMax(UI::S(300), w - UI::S(20)), !c.busy && fHave > 0); ImGui::PopID();
    if(!c.busy && FloatMenu::BigButton("Refresh the file list")) DzModels::CloudRefresh();
    ImGui::TextDisabled("Files: github.com/ThirtyFoxMCx00E1/wagu  Host/DayZ/models  (second host: the mtadayz fork)");
}

static int LSectionIcon(const char* t)
{
    if(!strcasecmp(t, "Connection")) return IC_GLOBE;
    if(!strcasecmp(t, "Display"))    return IC_SLIDERS;
    if(!strcasecmp(t, "Chat"))       return IC_CHAT;
    if(!strcasecmp(t, "Player"))     return IC_USER;
    if(!strcasecmp(t, "Cheats"))     return IC_BAN;
    if(!strcasecmp(t, "Language"))   return IC_GLOBE;   // 3.52
    if(!strcasecmp(t, "Sound"))      return IC_PLAY;   // 3.00
    return IC_LIST;
}

static bool LSideItem(const char* id, int icon, const char* label, bool on, float w, float h)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton("##si", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 3.63: selected = teal pill + a teal bar on the left (Valorant tabs)
    const ImVec2 a1(p.x + UI::S(6), p.y + UI::S(6)), b1(p.x + w - UI::S(4), p.y + h - UI::S(6));
    if(on) { VPill(dl, a1, b1, kVTealDim, VAlpha(kVTeal, 0.55f)); dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.28f), ImVec2(p.x + UI::S(4), p.y + h * 0.72f), kVTeal, 0.0f); }
    else if(ImGui::IsItemActive()) VPill(dl, a1, b1, IM_COL32(236, 232, 225, 22));
    ImU32 col = on ? kVTeal : IM_COL32(205, 210, 214, 255);
    LIcon(dl, icon, ImVec2(p.x + UI::S(36), p.y + h * 0.5f), h * 0.24f, col);
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    dl->AddText(ImVec2(p.x + UI::S(74), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    dl->PopClipRect();
    ImGui::PopID();
    return hit;
}

static void LGroupLabel(const char* t)
{
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    // 3.63: small dim caps + a thin line to the right (Valorant section headers)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float fs = ImGui::GetFontSize() * 0.82f, lh = ImGui::GetTextLineHeight();
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    float x = p.x + UI::S(14), y = p.y + (lh - fs) * 0.5f;
    dl->AddText(UI::Font(), fs, ImVec2(x, y), kVDim, t);
    float lx = x + ts.x + UI::S(12), rx = p.x + ImGui::GetContentRegionAvail().x - UI::S(6);
    if(rx > lx) dl->AddLine(ImVec2(lx, p.y + lh * 0.5f), ImVec2(rx, p.y + lh * 0.5f), kVLine, UI::S(1));
    ImGui::Dummy(ImVec2(1, lh));
}

#include "mp_social.inc"   // 3.63: friends, chats, party, communities, inbox

static void RenderLauncher(void* openPtr)
{
    bool* open = (bool*)openPtr;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float mx = ds.x * 0.025f, my = ds.y * 0.03f;   // 4.03: almost full screen (CS:GO browser), the game shows around it
    // 2.26: dim the game behind the launcher (touches never reach it while this is open)
    // 4.05.1: CS:GO look - a blurred night Los Santos picture behind everything, then see-through pages
    // 4.05.2: the picture only fills the launcher panel (almost full screen), not the whole screen -
    //         the game shows dimmed around it like before
    {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 150));
        VBackdrop(bg, ImVec2(mx, my), ImVec2(ds.x - mx, ds.y - my));
    }
    ImGui::SetNextWindowPos(ImVec2(mx, my));
    ImGui::SetNextWindowSize(ImVec2(ds.x - mx * 2, ds.y - my * 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);         // square
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(1));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));   // 4.05.1: see-through (VGlass below), the backdrop shows
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0.6f));
    if(!ImGui::Begin("##MPLauncher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse))
    { ImGui::End(); ImGui::PopStyleColor(3); ImGui::PopStyleVar(3); return; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const ImU32 line = kVLine;
    const float pad = UI::S(16);
    // 4.03: CS:GO panel: lighter at the top, darker at the bottom
    VGlass(dl, wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    VPushWidgetStyle();

    // ---- title bar: PLAY ... profile (top right) ----
    const float titleH = UI::S(70);
    static bool s_bProfMenu = false, s_bAbout = false;
    ImVec2 profA(0, 0), profB(0, 0);
    {
        float tx = wp.x + pad + UI::S(6), ty = wp.y + (titleH - ImGui::GetTextLineHeight() * 1.25f) * 0.5f;
        dl->AddText(UI::Font(), ImGui::GetFontSize() * 1.25f, ImVec2(tx, ty), IM_COL32_WHITE, "PLAY");
        float px = tx + ImGui::CalcTextSize("PLAY").x * 1.25f + UI::S(22);
        float ty2 = wp.y + (titleH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(px, ty2), kVDim, "MP Launcher  v" MP_VERSION_STR);
        // [X] at the far right, then friends + bell, then the profile (avatar + name) with its menu
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - titleH - UI::S(6), wp.y));
        if(LToolButton("close", IC_CLOSE, nullptr, titleH)) { *open = false; s_bRegionPick = false; s_bProfMenu = false; }
        if(s_bLauncherCloseReq) { s_bLauncherCloseReq = false; *open = false; s_bRegionPick = false; s_bProfMenu = false; }
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - titleH * 3 - UI::S(6), wp.y));
        SocTitleButtons(titleH);
        const std::string pu = ProfileUser();
        const bool online = s_Auth.LoggedIn() && !s_AuthUser.empty();
        const char* l1 = pu.empty() ? "Guest" : pu.c_str();
        char l2[96];
        if(pu.empty()) snprintf(l2, sizeof(l2), "Not logged in");
        else if(!s_ProfDiscord.empty() && !strcasecmp(pu.c_str(), s_ProfOwner.c_str())) snprintf(l2, sizeof(l2), "@%s%s", s_ProfDiscord.c_str(), online ? "" : "  (offline)");
        else snprintf(l2, sizeof(l2), online ? "Logged in" : "Log in on a server");
        const float sc = 0.78f, lh = ImGui::GetTextLineHeight(), ar = UI::S(24);
        float pw = std::max(ImGui::CalcTextSize(l1).x, ImGui::CalcTextSize(l2).x * sc);
        float right = wp.x + ws.x - titleH * 3 - UI::S(18);
        profA = ImVec2(right - pw - ar * 2 - UI::S(44), wp.y + UI::S(6)); profB = ImVec2(right, wp.y + titleH - UI::S(6));
        ImGui::SetCursorScreenPos(profA);
        if(ImGui::InvisibleButton("##prof", ImVec2(profB.x - profA.x, profB.y - profA.y))) s_bProfMenu = !s_bProfMenu;
        if(s_bProfMenu || ImGui::IsItemActive()) dl->AddRectFilled(profA, profB, IM_COL32(255, 255, 255, s_bProfMenu ? 24 : 14));
        ImVec2 c(profB.x - ar - UI::S(10), (profA.y + profB.y) * 0.5f);
        ProfileDrawAvatar(dl, c, ar);
        dl->AddCircleFilled(ImVec2(c.x + ar * 0.72f, c.y + ar * 0.72f), UI::S(6), online ? IM_COL32(67, 181, 129, 255) : IM_COL32(116, 127, 141, 255), 16);
        float nx = c.x - ar - UI::S(12);
        dl->AddText(ImVec2(nx - ImGui::CalcTextSize(l1).x, c.y - lh * 0.5f - lh * sc * 0.5f), IM_COL32_WHITE, l1);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * sc, ImVec2(nx - ImGui::CalcTextSize(l2).x * sc, c.y + lh * 0.5f - lh * sc * 0.45f), online ? IM_COL32(95, 211, 95, 255) : IM_COL32(150, 150, 150, 255), l2);
        {   // small down arrow
            float ax = profA.x + UI::S(14), ay = c.y, s2 = UI::S(5);
            dl->AddTriangleFilled(ImVec2(ax - s2, ay - s2 * 0.5f), ImVec2(ax + s2, ay - s2 * 0.5f), ImVec2(ax, ay + s2 * 0.6f), kVDim);
        }
        // watermark (2.30), between the version and the profile
        {
            const char* wm = "powered by HiveMQ/ThirtyFoxMC";
            ImVec2 wsz = ImGui::CalcTextSize(wm);
            float wx = profA.x - UI::S(20) - wsz.x * 0.8f, minX = px + ImGui::CalcTextSize("MP Launcher  v" MP_VERSION_STR).x + UI::S(30);
            if(wx > minX) dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(wx, ty2 + lh * 0.1f), IM_COL32(95, 211, 95, 110), wm);
        }
        dl->AddLine(ImVec2(wp.x, wp.y + titleH), ImVec2(wp.x + ws.x, wp.y + titleH), line, UI::S(1));
    }
    // ---- data ----
    ServerList::Poll();
    if(MP_NowMs() - ServerList::LastRefreshMs() > 10000) ServerList::Refresh();
    std::vector<LanServer> nearby;
    { std::lock_guard<std::mutex> lk(s_LanMutex); nearby = s_Nearby; }
    {   // 3.53.1: live player counts from the lobby (every host re-announces every 3 s). The rows used the last
        // scan only (every 15 s, none while another job ran), so after leaving a server it still showed 2/100
        // for minutes. Same token = same server: take its live numbers.
        static std::vector<LanServer> live; static uint64_t liveAt = 0;
        if(MP_NowMs() - liveAt > 1000) { liveAt = MP_NowMs(); live = Online::Snapshot(); }
        for(auto& n : nearby)
            if(n.token) for(auto& l : live) if(l.token == n.token) { n.players = l.players; n.maxPlayers = l.maxPlayers; break; }
    }
    if(s_bHasNetwork && !IsSearching() && MP_NowMs() - s_nNearbyTime > 15000) { s_nNearbyTime = MP_NowMs(); StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); }
    bool listTab = s_nLTab == LT_INTERNET || s_nLTab == LT_FAVORITES || s_nLTab == LT_LAN;
    std::vector<LRow> rows;
    if(listTab) LBuildRows(s_nLTab, nearby, rows);
    // keep the selection valid for CONNECT (LTable re-resolves it by key)
    s_nLSel = -1;
    { char key[48]; for(int i = 0; i < (int)rows.size(); ++i) { LRowKey(rows[i], key, sizeof(key)); if(s_szLSelKey[0] && !strcmp(key, s_szLSelKey)) s_nLSel = i; } }
    const LRow* sel = (s_nLSel >= 0 && s_nLSel < (int)rows.size()) ? &rows[s_nLSel] : nullptr;

    // ---- 4.03: CS:GO top tabs  Internet | Featured | Favorites | Lan     [search] refresh region gear + help ----
    const float tbH = UI::S(78), btnH = UI::S(60);
    {
        float y = wp.y + titleH + (tbH - btnH) * 0.5f;
        float sideW = ImMax(UI::S(300), ws.x * 0.17f);
        float x = wp.x + UI::S(10) + sideW + UI::S(18);
        struct { const char* t; int tab, sub; } tabs[4] = { { "Internet", LT_INTERNET, 0 }, { "Featured", LT_INTERNET, 1 }, { "Favorites", LT_FAVORITES, -1 }, { "Lan", LT_LAN, -1 } };
        if(!listTab)
        {   // 4.03: pages stay in the left sidebar - the top row only shows the page name
            const char* pt = s_nLTab == LT_ADD || s_nLTab == LT_HOSTED ? "Add server" : s_nLTab == LT_CLOUD ? "Cloud Storage" : s_nLTab == LT_PLAYERS ? "Players" :
                             s_nLTab == LT_BANS ? "Bans" : s_nLTab == LT_SRVMGMT ? "Server Management" : s_nLTab == LT_FRIENDS ? "Friends" : s_nLTab == LT_CHATS ? "Chats" :
                             s_nLTab == LT_PARTY ? "Party" : s_nLTab == LT_COMMUNITY ? "Communities" : s_nLTab == LT_INBOX ? "Inbox" :
                             (s_nLTab >= LT_SECTIONS && s_nLTab - LT_SECTIONS < FloatMenu::SectionCount()) ? FloatMenu::SectionTitle(s_nLTab - LT_SECTIONS) : "";
            dl->AddText(ImVec2(x, y + (btnH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32_WHITE, pt);
        }
        for(int i = 0; i < 4 && listTab; ++i)
        {
            ImVec2 ts = ImGui::CalcTextSize(tabs[i].t);
            float tw = ts.x + UI::S(44);
            ImVec2 ta(x, y), tb(x + tw, y + btnH);
            ImGui::SetCursorScreenPos(ta); ImGui::PushID(950 + i);
            bool on = s_nLTab == tabs[i].tab && (tabs[i].sub < 0 || s_nInetSub == tabs[i].sub);
            if(ImGui::InvisibleButton("##ttab", ImVec2(tw, btnH), L_PRESS_ON_CLICK) && !on) { s_nLTab = tabs[i].tab; if(tabs[i].sub >= 0) s_nInetSub = tabs[i].sub; s_szLSelKey[0] = 0; s_nLSel = -1; }   // 4.05.2: on the press
            bool held = ImGui::IsItemActive(); ImGui::PopID();
            if(held) dl->AddRectFilled(ta, tb, IM_COL32(255, 255, 255, 18));
            dl->AddText(ImVec2(x + (tw - ts.x) * 0.5f, y + (btnH - ts.y) * 0.5f), on ? IM_COL32_WHITE : IM_COL32(165, 172, 180, 255), tabs[i].t);
            if(on) dl->AddRectFilled(ImVec2(ta.x + UI::S(8), tb.y - UI::S(3)), ImVec2(tb.x - UI::S(8), tb.y), IM_COL32_WHITE);
            x += tw + UI::S(6);
        }
        // right side: search, refresh, region, connection settings, add server, help
        float searchW = ImMax(UI::S(260), ws.x * 0.16f);
        float rx = wp.x + ws.x - pad - btnH * 4 - UI::S(6) * 4 - searchW;   // 4.05.7: 4 icons (no +)
        if(rx < x + UI::S(10)) { searchW = ImMax(UI::S(140), searchW - (x + UI::S(10) - rx)); rx = wp.x + ws.x - pad - btnH * 4 - UI::S(6) * 4 - searchW; }
        ImGui::SetCursorScreenPos(ImVec2(rx, y));
        if(LInputBox("search", s_szSearch, "Search", searchW, btnH, IC_SEARCH)) VKeyboard::Open("Search server", s_szSearch, 31, OnSearch);
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("refresh", IC_REFRESH, nullptr, btnH)) { ServerList::Refresh(); if(s_bHasNetwork) { s_nNearbyTime = MP_NowMs(); StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); } }
        ImGui::SameLine(0, UI::S(6));
        s_vRegionBtn = ImGui::GetCursorScreenPos(); s_fRegionBtn = btnH;   // 3.25.1: region switch
        // 4.05.3: the only toolbar button that TOGGLES - one tap that arrived twice (press seen twice) opened
        //         and closed the list in the same moment, so it looked dead. Now one toggle per 400 ms
        bool regionHit = LToolButton("region", IC_REGION, nullptr, btnH, true, s_bRegionPick);
        if(LRegionRawTap(s_vRegionBtn, btnH)) regionHit = true;   // 4.05.8
        if(regionHit && MP_NowMs() - s_nRegionToggleMs > 400)
        { s_bRegionPick = !s_bRegionPick; s_nRegionToggleMs = MP_NowMs(); s_bProfMenu = false; }
        ImGui::SameLine(0, UI::S(6));
        int connTab = -1;
        for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Connection")) connTab = LT_SECTIONS + i;
        if(connTab < 0) for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(strcasecmp(FloatMenu::SectionTitle(i), "Servers")) { connTab = LT_SECTIONS + i; break; }   // 4.05.2: never a dead gear
        if(LToolButton("gear", IC_GEAR, nullptr, btnH, connTab >= 0, s_nLTab == connTab) && connTab >= 0) { s_nLTab = connTab; s_bRegionPick = false; }
        ImGui::SameLine(0, UI::S(6));
        // 4.05.7: no + icon here any more - "Add server" is in the side bar (it was there twice)
        if(LToolButton("help", IC_INFO, nullptr, btnH)) FloatMenu::OpenPage(FloatMenu::PAGE_HELP);
        dl->AddLine(ImVec2(wp.x, wp.y + titleH + tbH), ImVec2(wp.x + ws.x, wp.y + titleH + tbH), line, UI::S(1));
    }
    // ---- body ----
    float bodyY = wp.y + titleH + tbH + UI::S(2);
    const float barH = UI::S(88);   // 4.03: bottom bar (status + nickname + CONNECT)
    float bodyH = wp.y + ws.y - bodyY - UI::S(6) - barH;
    float sideW = ImMax(UI::S(300), ws.x * 0.17f);
    ImGui::SetCursorScreenPos(ImVec2(wp.x + UI::S(10), bodyY + UI::S(8)));
    ImGui::BeginChild("##lside", ImVec2(sideW, bodyH - UI::S(10)), false);
    {
        float iw = ImGui::GetContentRegionAvail().x, ih = UI::S(70);
        LGroupLabel("SERVERS");
        // 4.05.7: Favorites and Lan are only the top tabs now (they were in the side bar too). The server
        // list item stays lit on all of its tabs (Internet / Featured / Favorites / Lan)
        if(LSideItem("inet", IC_GLOBE, "Internet",  listTab, iw, ih)) { if(!listTab) s_nLTab = LT_INTERNET; }
        if(LSideItem("add",  IC_PLUS,  "Add server",s_nLTab == LT_ADD,       iw, ih)) { s_nLTab = LT_ADD; }
        LGroupLabel("STORAGE");   // 4.02
        if(LSideItem("cloud", IC_CLOUD, "Cloud Storage", s_nLTab == LT_CLOUD, iw, ih)) { s_nLTab = LT_CLOUD; }
        LGroupLabel("ADMIN");
        if(LSideItem("pl",   IC_USERS, "Players",   s_nLTab == LT_PLAYERS,   iw, ih)) { s_nLTab = LT_PLAYERS; }
        if(LSideItem("bans", IC_BAN,   "Bans",      s_nLTab == LT_BANS,      iw, ih)) { s_nLTab = LT_BANS; }
        if(LSrvMgmtAvailable()) { if(LSideItem("srvm", IC_SLIDERS, "Server Management", s_nLTab == LT_SRVMGMT, iw, ih)) { s_nLTab = LT_SRVMGMT; } }   // 3.53: only while joined
        else if(s_nLTab == LT_SRVMGMT) s_nLTab = LT_INTERNET;
        SocSideItems(iw, ih);   // 3.63: SOCIAL  Friends / Chats / Party / Communities / Inbox
        LGroupLabel("SETTINGS");
        for(int i = 0; i < FloatMenu::SectionCount(); ++i)
        {
            const char* t = FloatMenu::SectionTitle(i);
            if(!strcasecmp(t, "Servers")) continue;   // shown as Internet / Favorites / Add server
            char id[16]; snprintf(id, sizeof(id), "sec%d", i);
            if(LSideItem(id, LSectionIcon(t), t, s_nLTab == LT_SECTIONS + i, iw, ih)) s_nLTab = LT_SECTIONS + i;
        }
        LDragScroll();
    }
    ImGui::EndChild();
    float cx0 = wp.x + UI::S(10) + sideW + UI::S(8);
    dl->AddLine(ImVec2(cx0, bodyY), ImVec2(cx0, wp.y + ws.y), line, UI::S(2));
    float contentW = wp.x + ws.x - cx0 - UI::S(4);

    if(listTab)
    {
        float tableW = contentW - UI::S(10);
        ImGui::SetCursorScreenPos(ImVec2(cx0 + UI::S(4), bodyY + UI::S(4)));
        LTable(rows, tableW, bodyH - UI::S(4));
        // the table may have changed the selection
        sel = nullptr;
        { char key[48]; for(auto& r : rows) { LRowKey(r, key, sizeof(key)); if(s_szLSelKey[0] && !strcmp(key, s_szLSelKey)) { sel = &r; break; } } }
    }
    else
    {
        ImGui::SetCursorScreenPos(ImVec2(cx0 + pad, bodyY + pad));
        ImGui::BeginChild("##lpage", ImVec2(contentW - pad * 2, bodyH - pad), false);
        switch(s_nLTab)
        {
        case LT_HOSTED:  // 2.37: merged into Add server
        case LT_ADD:     SectionAddServer(nullptr); break;
        case LT_PLAYERS: LPagePlayers(); break;
        case LT_BANS:    LPageBans(); break;
        case LT_SRVMGMT: LPageServerMgmt(); break;   // 3.53
        case LT_CLOUD:   LPageCloud(contentW - pad * 2); break;   // 4.02
        case LT_FRIENDS: case LT_CHATS: case LT_PARTY: case LT_COMMUNITY: case LT_INBOX: SocPage(s_nLTab, contentW - pad * 2, bodyH - pad); break;   // 3.63
        default:
            if(s_nLTab >= LT_SECTIONS && s_nLTab - LT_SECTIONS < FloatMenu::SectionCount()) FloatMenu::DrawSection(s_nLTab - LT_SECTIONS);
            else s_nLTab = LT_INTERNET;
        }
        LDragScroll();
        ImGui::EndChild();
    }
    // ---- 4.03: bottom bar: status | nickname | green CONNECT ----
    {
        ImVec2 ba(wp.x, wp.y + ws.y - barH), bb(wp.x + ws.x, wp.y + ws.y);
        dl->AddRectFilled(ba, bb, IM_COL32(24, 27, 32, 220));
        dl->AddLine(ba, ImVec2(bb.x, ba.y), IM_COL32(0, 0, 0, 160), UI::S(1));
        const float bh = barH - UI::S(26), by = ba.y + UI::S(13);
        // status
        char st[160];
        int n = 0, pl = 0; for(auto& r : rows) { ++n; if(r.players > 0) pl += r.players; }
        if(listTab) snprintf(st, sizeof(st), "%d server%s   %d player%s%s", n, n == 1 ? "" : "s", pl, pl == 1 ? "" : "s", IsBusy() ? "   searching..." : "");
        else snprintf(st, sizeof(st), "%s", IsConnected() ? "Connected" : "Not connected");
        dl->AddText(ImVec2(ba.x + pad + UI::S(6), by + (bh - ImGui::GetTextLineHeight()) * 0.5f), kVDim, st);
        // nickname (was in the toolbar)
        float cw = ImMax(UI::S(300), ws.x * 0.18f);
        float nickW = ImMax(UI::S(260), ws.x * 0.15f);
        ImGui::SetCursorScreenPos(ImVec2(bb.x - pad - cw - UI::S(14) - nickW, by));
        bool idle = client.GetState() == NET_DISCONNECTED;
        if(LInputBox("nick", settings.name, "Nickname", nickW, bh) && idle) VKeyboard::Open("Nickname", settings.name, MP_NAME_LEN - 1, OnName);
        // CONNECT (Leave / Cancel while on the selected server)
        bool onSel = sel ? sel->on : (listTab ? false : IsOnDefault());
        const char* cl = onSel ? (client.GetState() == NET_CONNECTING ? "CANCEL" : "DISCONNECT") : (IsBusy() ? "SEARCHING..." : "CONNECT");
        bool can = (s_bHasNetwork || onSel) && (sel || !listTab || IsOnDefault());
        ImVec2 ca(bb.x - pad - cw, by), cb(bb.x - pad, by + bh);
        ImGui::SetCursorScreenPos(ca);
        if(!can) ImGui::BeginDisabled();
        bool hit = ImGui::InvisibleButton("##connect", ImVec2(cw, bh));
        bool held = ImGui::IsItemActive();
        if(!can) ImGui::EndDisabled();
        ImU32 g0 = onSel ? IM_COL32(170, 58, 46, 255) : kVGreen, g1 = onSel ? IM_COL32(120, 36, 28, 255) : IM_COL32(44, 110, 26, 255);
        if(!can) { g0 = IM_COL32(70, 76, 84, 255); g1 = IM_COL32(52, 56, 62, 255); }
        if(held) { g0 = VAlpha(g0, 0.8f); }
        dl->AddRectFilledMultiColor(ca, cb, g0, g0, g1, g1);
        dl->AddRect(ca, cb, can ? IM_COL32(150, 220, 120, onSel ? 0 : 160) : IM_COL32(255, 255, 255, 30));
        ImVec2 ts = ImGui::CalcTextSize(cl);
        dl->AddText(UI::Font(), ImGui::GetFontSize() * 1.1f, ImVec2((ca.x + cb.x - ts.x * 1.1f) * 0.5f, (ca.y + cb.y - ts.y * 1.1f) * 0.5f), can ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255), cl);
        if(hit && can)
        {
            if(sel) { if(sel->on) LLeave(*sel); else LJoin(*sel); }   // 3.10: Leave really leaves, Connect never reconnects
            else if(IsOnDefault()) MP::ToggleConnect();
        }
    }
    // ---- 4.03: profile menu (top right): Settings, About, Switch account, Logout ----
    if(s_bProfMenu)
    {
        std::vector<std::string> accs = AuthAccounts();
        const float iw = ImMax(UI::S(380), profB.x - profA.x), ih = UI::S(62);
        ImVec2 ma(profB.x - iw, profB.y + UI::S(4));
        int items = 4 + (int)accs.size() + ((int)accs.size() < AUTH_MAX_ACCOUNTS ? 1 : 0);
        ImVec2 mb(profB.x, ma.y + ih * items + UI::S(30));
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(ma, mb, IM_COL32(36, 40, 46, 250));
        fg->AddRect(ma, mb, IM_COL32(0, 0, 0, 200));
        float yy = ma.y + UI::S(4);
        auto item = [&](const char* id, const char* t, ImU32 col, bool sub) -> bool
        {
            ImVec2 a(ma.x, yy), b(mb.x, yy + ih);
            ImGui::SetCursorScreenPos(a); ImGui::PushID(id);
            bool h = ImGui::InvisibleButton("##pm", ImVec2(b.x - a.x, ih));
            bool act = ImGui::IsItemActive(); ImGui::PopID();
            if(act) fg->AddRectFilled(a, b, IM_COL32(255, 255, 255, 24));
            fg->AddText(ImVec2(a.x + UI::S(sub ? 46 : 22), a.y + (ih - ImGui::GetTextLineHeight()) * 0.5f), col, t);
            yy += ih; return h;
        };
        int setTab = -1;
        for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Player")) setTab = LT_SECTIONS + i;
        if(setTab < 0) for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(strcasecmp(FloatMenu::SectionTitle(i), "Servers")) { setTab = LT_SECTIONS + i; break; }
        if(item("set", "Settings", kVText, false)) { if(setTab >= 0) s_nLTab = setTab; s_bProfMenu = false; }
        if(item("about", "About", kVText, false)) { s_bAbout = true; s_bProfMenu = false; }
        fg->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(ma.x + UI::S(22), yy + UI::S(6)), kVDim, "Switch account");
        yy += UI::S(30);
        const std::string cur = ProfileUser();
        for(int i = 0; i < (int)accs.size(); ++i)
        {
            bool me = !strcasecmp(accs[i].c_str(), cur.c_str());
            char t[64]; snprintf(t, sizeof(t), "%s%s", accs[i].c_str(), me ? "   (this one)" : "");
            char id[8]; snprintf(id, sizeof(id), "a%d", i);
            if(item(id, t, me ? IM_COL32(95, 211, 95, 255) : kVText, true) && !me) { AuthLogout(accs[i].c_str(), false); s_bProfMenu = false; }
        }
        if((int)accs.size() < AUTH_MAX_ACCOUNTS)
        {
            char t[64]; snprintf(t, sizeof(t), "+ Add account  (%d/%d)", (int)accs.size(), AUTH_MAX_ACCOUNTS);
            if(item("add", t, kVTeal, true)) { AuthLogout("", true); s_bProfMenu = false; }
        }
        fg->AddLine(ImVec2(ma.x, yy + UI::S(2)), ImVec2(mb.x, yy + UI::S(2)), IM_COL32(255, 255, 255, 30));
        if(item("out", "Logout", IM_COL32(230, 110, 96, 255), false))
        {
            if(!cur.empty()) AuthAccountsRemove(cur);
            AuthLogout("", false); s_bProfMenu = false;
        }
        // tap outside = close
        if(ImGui::IsMouseClicked(0) && !ImGui::IsMouseHoveringRect(ma, mb, false) && !ImGui::IsMouseHoveringRect(profA, profB, false)) s_bProfMenu = false;
    }
    if(s_bAbout)
    {
        ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(ImMin(ds.x * 0.5f, UI::S(900)), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(26), UI::S(22)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.16f, 0.19f, 0.98f));
        if(ImGui::Begin("##MPAbout", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("MP Launcher v" MP_VERSION_STR);
            ImGui::TextDisabled("GTA San Andreas multiplayer mod by ThirtyFoxMC");
            ImGui::Separator();
            ImGui::TextWrapped("Servers: internet lobby (HiveMQ / public MQTT brokers), Wi-Fi (Lan) and saved servers. "
                               "Logins and Discord verification: the MP Discord bot (max %d game accounts per Discord).", AUTH_MAX_ACCOUNTS);
            ImGui::TextWrapped("Server backgrounds: Wikimedia Commons photos (see ICONS_CREDITS.txt). Flags: flag-icons (MIT).");
            ImGui::Spacing();
            if(ImGui::Button("Close", ImVec2(-1, UI::S(64)))) s_bAbout = false;
        }
        ImGui::End();
        ImGui::PopStyleColor(); ImGui::PopStyleVar();
    }
    // the Internet tab also shows the lobby state under the list title area when it is offline
    VPopWidgetStyle();
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    LRegionPicker(nearby);   // 3.25.1: the region list opened by the toolbar icon (its own window, on top of the launcher)
}

// ---- kick / ban dialog (SA-MP style) -----------------------------------------------------------
static void RenderKickDialog(void*)
{
    if(!s_Kick.show) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = ImMin(ds.x * 0.62f, UI::S(1100));
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(28), UI::S(24)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);   // 4.03: square
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.025f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.15f, 0.15f, 0.65f));
    ImGui::SetNextWindowFocus();
    if(ImGui::Begin("##MPKicked", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float fs = UI::S(40);
        const char* title = s_Kick.banned ? "You are banned from the server!" : "You were kicked from the server!";
        dl->AddText(UI::Font(), fs, p, IM_COL32(255, 70, 70, 255), title);
        ImGui::Dummy(ImVec2(w - UI::S(56), fs + UI::S(10)));
        ImGui::Separator();
        char d[40]; time_t tt = (time_t)s_Kick.when; struct tm lt; localtime_r(&tt, &lt); strftime(d, sizeof(d), "%Y-%m-%d  %H:%M", &lt);
        auto rowf = [](const char* k, const char* v)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1)); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
            ImGui::SameLine(UI::S(300)); ImGui::TextWrapped("%s", v);
        };
        rowf("Server:", s_Kick.server);
        rowf(s_Kick.banned ? "Ban date:" : "Kick date:", d);
        rowf("Administrator:", s_Kick.admin[0] ? s_Kick.admin : "Server");
        rowf("Reason:", s_Kick.reason[0] ? s_Kick.reason : "No reason given");
        ImGui::Separator();
        if(s_Kick.banned) ImGui::TextDisabled("If you think this is a mistake, ask the administrator (the host) to unban you.");
        else
        {
            const uint64_t nw = MP_NowMs();
            if(s_nKickWaitUntil > nw)
            {
                const int left = (int)((s_nKickWaitUntil - nw + 999) / 1000);
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1), "You can join again in %d:%02d.", left / 60, left % 60);
            }
            else ImGui::TextDisabled("You can join again now. Follow the server rules.");
        }
        ImGui::Spacing();
        float bw = UI::S(260);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw) * 0.5f);
        if(ImGui::Button("OK", ImVec2(bw, UI::S(72)))) s_Kick.show = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// 2.37: "This server doesn't allow cheat menus" - shown when CheatMenu is installed and you try to join / host
static void RenderCheatGateDialog(void*)
{
    if(!s_CheatGate.show) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = ImMin(ds.x * 0.62f, UI::S(1100));
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(28), UI::S(24)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);   // 4.03: square
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.025f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.55f, 0.10f, 0.70f));
    ImGui::SetNextWindowFocus();
    if(ImGui::Begin("##MPCheatGate", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float fs = UI::S(40);
        dl->AddText(UI::Font(), fs, p, IM_COL32(255, 150, 40, 255), "This server doesn't allow cheat menus");
        ImGui::Dummy(ImVec2(w - UI::S(56), fs + UI::S(10)));
        ImGui::Separator();
        ImGui::TextWrapped("%s is loaded in your game right now. To keep it fair for everyone, "
                           "cheat menus aren't allowed on %s.", ModScan::What(), s_CheatGate.server);
        ImGui::Spacing();
        ImGui::TextWrapped("Cheat Menu works only on custom servers whose host turned on \"Cheat Menu\" (Add server > permissions). "
                           "For the default server, remove it from your AML mods folder and restart the game - once it is not loaded any more you can join right away.");
        ImGui::Spacing();
        ImGui::TextDisabled("We check this every time you join, so it can't be switched off while playing.");
        ImGui::Spacing();
        float bw = UI::S(260);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw) * 0.5f);
        if(ImGui::Button("Got it", ImVec2(bw, UI::S(72)))) s_CheatGate.show = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}
