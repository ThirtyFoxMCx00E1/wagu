#include "vkeyboard.h"
#include "ui_common.h"
#include <string>
#include <string.h>
#include <float.h>
#include "emoji.h"

namespace
{
    bool        s_bOpen = false;
    bool        s_bShift = false;
    bool        s_bSymbols = false;
    bool        s_bNumeric = false;
    bool        s_bEmoji = false;   // 2.19 emoji page
    bool        s_bChat = false;
    int         s_nMaxLen = 128;
    std::string s_Text, s_Title;
    VKeyboard::SubmitFn s_fnSubmit = nullptr;
    void*       s_pData = nullptr;
    VKeyboard::SubmitFn s_fnValFor = nullptr;   // 2.37
    VKeyboard::CheckFn  s_fnCheck = nullptr;

    const char* kRowsLower[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
    const char* kRowsUpper[] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    const char* kRowsSym[]   = { "1234567890", "@#$%&*-+()", "!?\"':;/=_", ".,<>[]~" };
    const char* kRowsNum[]   = { "123", "456", "789", ".0" };
}

void VKeyboard::Open(const char* title, const char* initial, int maxLen, SubmitFn fn, void* data, bool numeric)
{
    s_bOpen = true;
    s_Title = title ? title : "";
    s_Text = initial ? initial : "";
    s_nMaxLen = maxLen > 0 ? maxLen : 128;
    s_fnSubmit = fn; s_pData = data;
    s_bNumeric = numeric; s_bShift = false; s_bSymbols = false; s_bEmoji = false; s_bChat = false;
}

void VKeyboard::SetValidator(SubmitFn forSubmit, CheckFn check) { s_fnValFor = forSubmit; s_fnCheck = check; }
void VKeyboard::Close() { s_bOpen = false; }
const char* VKeyboard::GetText() { return s_Text.c_str(); }
void VKeyboard::SetText(const char* t) { s_Text = t ? t : ""; if((int)s_Text.size() > s_nMaxLen - 1) s_Text.resize(s_nMaxLen - 1); }
bool VKeyboard::IsChat() { return s_bOpen && s_bChat; }
void VKeyboard::SetChat(bool c) { s_bChat = c; }
float VKeyboard::Height() { return s_bOpen ? ImGui::GetIO().DisplaySize.y * 0.50f : 0.0f; }

// 2.19: whole UTF-8 characters (emojis are 3-4 bytes)
static void DeleteLastChar()
{
    if(s_Text.empty()) return;
    size_t i = s_Text.size() - 1;
    while(i > 0 && ((unsigned char)s_Text[i] & 0xC0) == 0x80) --i;
    s_Text.erase(i);
}
static void AppendStr(const char* u)
{
    if((int)(s_Text.size() + strlen(u)) < s_nMaxLen) s_Text += u;
}
bool VKeyboard::IsOpen() { return s_bOpen; }

// 3.63: Valorant-style keys (layout unchanged) - chamfered translucent navy keys,
// thin outlines, teal for active toggles, red for Enter / close.
enum { VK_NORMAL = 0, VK_TEAL, VK_RED, VK_DANGER, VK_DIM };
static void VkChamfer(ImDrawList* dl, ImVec2 a, ImVec2 b, float c, ImU32 fill, ImU32 line, float th)
{
    ImVec2 pts[6] = { ImVec2(a.x + c, a.y), ImVec2(b.x, a.y), ImVec2(b.x, b.y - c),
                      ImVec2(b.x - c, b.y), ImVec2(a.x, b.y), ImVec2(a.x, a.y + c) };
    if(fill) dl->AddConvexPolyFilled(pts, 6, fill);
    if(line) dl->AddPolyline(pts, 6, line, ImDrawFlags_Closed, th);
}
static bool Key(const char* label, float w, float h, int style = VK_NORMAL)
{
    ImVec2 a = ImGui::GetCursorScreenPos();
    bool r = ImGui::InvisibleButton(label, ImVec2(w, h));
    bool held = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
    ImVec2 b(a.x + w, a.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill, line, txt = IM_COL32(236, 232, 225, 255);
    switch(style)
    {
        case VK_TEAL:   fill = IM_COL32(64, 230, 190, held ? 255 : 215); line = IM_COL32(150, 255, 225, 255); txt = IM_COL32(10, 20, 28, 255); break;
        case VK_RED:    fill = IM_COL32(255, 70, 85, held ? 255 : 220);  line = IM_COL32(255, 140, 150, 255); txt = IM_COL32_WHITE; break;
        case VK_DANGER: fill = IM_COL32(255, 70, 85, held ? 120 : 40);   line = IM_COL32(255, 70, 85, 230); break;
        case VK_DIM:    fill = IM_COL32(15, 25, 35, held ? 230 : 170);   line = IM_COL32(236, 232, 225, hov ? 120 : 70); txt = IM_COL32(200, 205, 210, 255); break;
        default:        fill = held ? IM_COL32(64, 230, 190, 70) : IM_COL32(20, 32, 44, 175);
                        line = held ? IM_COL32(64, 230, 190, 230) : IM_COL32(236, 232, 225, hov ? 90 : 45); break;
    }
    float c = h * 0.18f;
    VkChamfer(dl, a, b, c, fill, line, UI::S(1.2f));
    if(style == VK_NORMAL && !held)   // tiny teal tick, like the Valorant panels
        dl->AddLine(ImVec2(a.x + c + UI::S(2), a.y + UI::S(1)), ImVec2(a.x + c + w * 0.18f, a.y + UI::S(1)), IM_COL32(64, 230, 190, 110), UI::S(1.5f));
    const char* end = strstr(label, "##");
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), txt, label, end);
    return r;
}

static void Append(char c)
{
    if((int)s_Text.size() < s_nMaxLen - 1) s_Text.push_back(c);
}

void VKeyboard::Render()
{
    if(!s_bOpen) return;

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float pad = UI::S(10.0f);
    const float kbH = ds.y * 0.50f;

    // dim the game behind the keyboard
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 90));

    ImGui::SetNextWindowPos(ImVec2(0, ds.y - kbH));
    ImGui::SetNextWindowSize(ImVec2(ds.x, kbH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.08f, 0.12f, 0.80f));   // 3.63: Valorant navy
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.93f, 0.91f, 0.88f, 0.18f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##MPKeyboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    {   // 3.63: top accent line + gradient
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        wdl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ds.x, wp.y + kbH), IM_COL32(64, 230, 190, 14), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 40), IM_COL32(0, 0, 0, 40));
        wdl->AddLine(wp, ImVec2(wp.x + ds.x, wp.y), IM_COL32(236, 232, 225, 50), UI::S(1));
        wdl->AddLine(wp, ImVec2(wp.x + ds.x * 0.18f, wp.y), IM_COL32(255, 70, 85, 230), UI::S(3));
        wdl->AddLine(ImVec2(wp.x + ds.x * 0.82f, wp.y), ImVec2(wp.x + ds.x, wp.y), IM_COL32(64, 230, 190, 200), UI::S(3));
    }

    // input line
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float lineH = UI::S(56.0f);
        float w = ds.x - pad * 2;
        VkChamfer(dl, p, ImVec2(p.x + w, p.y + lineH), lineH * 0.22f, IM_COL32(10, 20, 30, 200), IM_COL32(64, 230, 190, 200), UI::S(1.6f));
        dl->AddRectFilled(ImVec2(p.x, p.y + lineH * 0.22f), ImVec2(p.x + UI::S(4), p.y + lineH), IM_COL32(255, 70, 85, 230));
        std::string shown = s_Title + ": " + UI::EmojiToPUA(s_Text.c_str());
        bool caret = ((int)(ImGui::GetTime() * 2.0)) % 2 == 0;
        if(caret) shown += "_";
        const char* bad = (s_fnCheck && s_fnSubmit == s_fnValFor) ? s_fnCheck(s_Text.c_str()) : nullptr;   // 2.37
        ImGui::PushFont(UI::Font());
        dl->AddText(UI::Font(), UI::S(30), ImVec2(p.x + UI::S(14), p.y + (lineH - UI::S(30)) * 0.5f), bad ? IM_COL32(255, 60, 60, 255) : IM_COL32_WHITE, shown.c_str());
        if(bad)
        {
            ImVec2 bs = UI::Font()->CalcTextSizeA(UI::S(24), FLT_MAX, 0, bad);
            dl->AddText(UI::Font(), UI::S(24), ImVec2(p.x + w - bs.x - UI::S(14), p.y + (lineH - UI::S(24)) * 0.5f), IM_COL32(255, 90, 90, 255), bad);
        }
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(w, lineH + pad));
    }

    const char** rows = s_bNumeric ? kRowsNum : (s_bSymbols ? kRowsSym : (s_bShift ? kRowsUpper : kRowsLower));
    const int maxCols = s_bNumeric ? 3 : 10;
    const float avail = ds.x - pad * 2;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float keyW = (avail - spacing * (maxCols - 1)) / maxCols;
    const float keyH = (kbH - UI::S(56.0f) - pad * 4) / 5.2f;

    ImGui::SetWindowFontScale(1.15f);
    if(s_bEmoji && !s_bNumeric)   // 2.19: 4 rows x 10 emojis
    {
        for(int r = 0; r < 4; ++r)
        {
            ImGui::SetCursorPosX(pad);
            for(int i = 0; i < 10; ++i)
            {
                int k = r * 10 + i;
                char lbl[24]; snprintf(lbl, sizeof(lbl), "%s##em%d", UI::EmojiGlyph(k), k);
                if(k < UI::EmojiCount() && Key(lbl, keyW, keyH)) AppendStr(UI::EmojiUtf8(k));
                if(i < 9) ImGui::SameLine();
            }
        }
    }
    else
    for(int r = 0; r < 4; ++r)
    {
        const char* k = rows[r];
        int n = (int)strlen(k);
        float rowW = n * keyW + (n - 1) * spacing;
        ImGui::SetCursorPosX(pad + (avail - rowW) * 0.5f);
        for(int i = 0; i < n; ++i)
        {
            char lbl[8] = { k[i], 0 };
            snprintf(lbl, sizeof(lbl), "%c##k%d%d", k[i], r, i);
            if(Key(lbl, keyW, keyH)) Append(k[i]);
            if(i < n - 1) ImGui::SameLine();
        }
    }

    // bottom row: Shift | Sym | Space | Back | Send | Close
    ImGui::SetCursorPosX(pad);
    float unit = (avail - spacing * 5) / 10.0f;
    if(!s_bNumeric)
    {
        if(Key(s_bShift ? "SHIFT##sh" : "Shift##sh", unit * 1.3f, keyH, s_bShift ? VK_TEAL : VK_DIM)) s_bShift = !s_bShift;
        ImGui::SameLine();
        if(Key(s_bSymbols ? "ABC##sy" : "?123##sy", unit * 1.3f, keyH, s_bSymbols ? VK_TEAL : VK_DIM)) { s_bSymbols = !s_bSymbols; s_bEmoji = false; }
        ImGui::SameLine();
        {   // 2.19: emoji page
            char el[24]; snprintf(el, sizeof(el), "%s##emj", s_bEmoji ? "ABC" : UI::EmojiGlyph(0));
            if(Key(el, unit * 1.0f, keyH, s_bEmoji ? VK_TEAL : VK_DIM)) s_bEmoji = !s_bEmoji;
        }
        ImGui::SameLine();
        if(Key("Space##sp", unit * 2.4f - spacing, keyH)) Append(' ');
        ImGui::SameLine();
    }
    else
    {
        if(Key("Space##sp", unit * 6.0f + spacing * 2, keyH)) Append(' ');
        ImGui::SameLine();
    }
    if(Key("<- Del##bk", unit * 1.3f, keyH, VK_DIM) && !s_Text.empty()) DeleteLastChar();
    ImGui::SameLine();
    if(Key("Enter##en", unit * 1.5f, keyH, VK_RED) && !(s_fnCheck && s_fnSubmit == s_fnValFor && s_fnCheck(s_Text.c_str())))
    {
        std::string txt = s_Text;
        SubmitFn fn = s_fnSubmit; void* d = s_pData;
        s_bOpen = false;
        if(fn) fn(txt.c_str(), d);
    }
    ImGui::SameLine();
    if(Key("X##cl", unit * 1.2f - spacing, keyH, VK_DANGER)) s_bOpen = false;
    ImGui::SetWindowFontScale(1.0f);

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}
