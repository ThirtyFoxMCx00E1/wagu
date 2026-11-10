#include "vkeyboard.h"
#include "ui_common.h"
#include <string>
#include <string.h>
#include <float.h>
#include <ctype.h>
#include "emoji.h"
#include "gifs.h"
#include "clipboard.h"
#include "hud.h"
#include "filepick.h"
#include <vector>
#include <math.h>

namespace
{
    bool        s_bOpen = false;
    bool        s_bShift = false;
    bool        s_bSymbols = false;
    bool        s_bNumeric = false;
    bool        s_bEmoji = false;   // 2.19 emoji page
    bool        s_bChat = false;
    bool        s_bGif = false;     // 4.04 GIF page (chat only)
    bool        s_bPasteWait = false;
    int         s_nGifCat = 0;
    float       s_fGifScroll = 0, s_fCatScroll = 0;
    bool        s_bGifDown = false, s_bGifDrag = false; float s_fGifDownX = 0, s_fGifDownY = 0; int s_nGifDownArea = 0;
    double      s_dCopied = -10;
    int         s_nMaxLen = 128;
    std::string s_Text, s_Title;
    VKeyboard::SubmitFn s_fnSubmit = nullptr;
    void*       s_pData = nullptr;
    VKeyboard::SubmitFn s_fnValFor = nullptr;   // 2.37
    VKeyboard::CheckFn  s_fnCheck = nullptr;
    bool        s_bUsePhone = false;   // 4.12.3: Settings > Keyboard
    bool        s_bPhone = false;      // this keyboard is the phone's own (dialog open)
    std::string s_PhoneMsg;            // validator message shown in the dialog title

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
    s_bNumeric = numeric; s_bShift = false; s_bSymbols = false; s_bEmoji = false; s_bChat = false; s_bGif = false; s_bPasteWait = false;
    s_bPhone = false; s_PhoneMsg.clear();
    if(s_bUsePhone && FilePick::AskText(s_Title.c_str(), s_Text.c_str(), s_nMaxLen - 1, numeric)) s_bPhone = true;   // 4.12.3
}
void VKeyboard::SetPhoneKeyboard(bool phone) { s_bUsePhone = phone; }
bool VKeyboard::PhoneKeyboard() { return s_bUsePhone; }

// 4.12.3: the phone keyboard's dialog: OK / Enter = submit (the same checks as MP's Enter), Cancel = close,
// could not open = MP's own keyboard after all
static bool PhoneTick()
{
    if(!s_bPhone) return false;
    int st = FilePick::TextState();
    if(st == 1) return true;
    if(st == 3)
    {
        std::string t = FilePick::Text(); FilePick::TextReset();
        if((int)t.size() > s_nMaxLen - 1) t.resize(s_nMaxLen - 1);
        s_Text = t;
        const char* bad = (s_fnCheck && s_fnSubmit == s_fnValFor) ? s_fnCheck(s_Text.c_str()) : nullptr;
        if(bad) { s_PhoneMsg = bad; FilePick::AskText(bad, s_Text.c_str(), s_nMaxLen - 1, s_bNumeric); return true; }   // ask again
        s_bPhone = false; s_bOpen = false;
        VKeyboard::SubmitFn fn = s_fnSubmit; void* d = s_pData;
        if(fn) fn(t.c_str(), d);
        return true;
    }
    FilePick::TextReset();
    s_bPhone = false;
    if(st == 4 || st == 6) return false;   // the MP keyboard instead
    s_bOpen = false;                        // cancelled / closed
    return true;
}

void VKeyboard::SetValidator(SubmitFn forSubmit, CheckFn check) { s_fnValFor = forSubmit; s_fnCheck = check; }
void VKeyboard::Close() { s_bOpen = false; if(s_bPhone) { s_bPhone = false; FilePick::TextReset(); } }
const char* VKeyboard::GetText() { return s_Text.c_str(); }
void VKeyboard::SetText(const char* t) { s_Text = t ? t : ""; if((int)s_Text.size() > s_nMaxLen - 1) s_Text.resize(s_nMaxLen - 1); }
bool VKeyboard::IsChat() { return s_bOpen && s_bChat; }
void VKeyboard::SetChat(bool c) { s_bChat = c; }
float VKeyboard::Height() { return s_bOpen && !s_bPhone ? ImGui::GetIO().DisplaySize.y * 0.50f : 0.0f; }

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

// 4.04: CS:GO server browser style keys (layout unchanged) - plain square boxes (no chamfer, no round
// corners), dark slate keys with a thin light line, blue for active toggles, green for Enter, red for close.
enum { VK_NORMAL = 0, VK_TEAL, VK_RED, VK_DANGER, VK_DIM };
static void VkChamfer(ImDrawList* dl, ImVec2 a, ImVec2 b, float /*c*/, ImU32 fill, ImU32 line, float th)
{
    if(fill) dl->AddRectFilled(a, b, fill, 0.0f);
    if(line) dl->AddRect(a, b, line, 0.0f, 0, th);
}
static bool Key(const char* label, float w, float h, int style = VK_NORMAL)
{
    ImVec2 a = ImGui::GetCursorScreenPos();
    bool r = ImGui::InvisibleButton(label, ImVec2(w, h));
    bool held = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
    ImVec2 b(a.x + w, a.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fill, line = IM_COL32(255, 255, 255, hov ? 60 : 30), txt = IM_COL32(226, 229, 233, 255);
    switch(style)
    {
        case VK_TEAL:   fill = held ? IM_COL32(96, 148, 196, 255) : IM_COL32(126, 178, 226, 235); txt = IM_COL32(16, 22, 30, 255); break;   // toggled (Shift / ?123)
        case VK_RED:    fill = held ? IM_COL32(44, 110, 26, 255) : IM_COL32(76, 160, 48, 255);  txt = IM_COL32_WHITE; break;              // Enter = green CONNECT
        case VK_DANGER: fill = held ? IM_COL32(120, 36, 28, 255) : IM_COL32(170, 58, 46, 235); txt = IM_COL32_WHITE; break;              // close / clear
        case VK_DIM:    fill = held ? IM_COL32(70, 77, 86, 255) : IM_COL32(44, 49, 56, 240);   txt = IM_COL32(170, 177, 186, 255); break;
        default:        fill = held ? IM_COL32(126, 178, 226, 70) : IM_COL32(52, 58, 66, hov ? 250 : 235); break;
    }
    if(held && style == VK_NORMAL) line = IM_COL32(126, 178, 226, 220);
    VkChamfer(dl, a, b, 0, fill, line, UI::S(1));
    if(style == VK_NORMAL && !held)   // a 1 px lighter top edge, like the CS:GO buttons
        dl->AddLine(ImVec2(a.x + UI::S(1), a.y + UI::S(1)), ImVec2(b.x - UI::S(1), a.y + UI::S(1)), IM_COL32(255, 255, 255, 18), UI::S(1));
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
    if(PhoneTick()) return;   // 4.12.3: the phone's keyboard is typing
    if(!s_bOpen) return;

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float pad = UI::S(10.0f);
    const float kbH = ds.y * 0.50f;

    // dim the game behind the keyboard
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 90));

    ImGui::SetNextWindowPos(ImVec2(0, ds.y - kbH));
    ImGui::SetNextWindowSize(ImVec2(ds.x, kbH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.102f, 0.114f, 0.133f, 0.97f));   // 4.04: CS:GO slate
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##MPKeyboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    {   // 3.63: top accent line + gradient
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        wdl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ds.x, wp.y + kbH), IM_COL32(255, 255, 255, 6), IM_COL32(255, 255, 255, 6), IM_COL32(0, 0, 0, 30), IM_COL32(0, 0, 0, 30));
        wdl->AddLine(wp, ImVec2(wp.x + ds.x, wp.y), IM_COL32(255, 255, 255, 40), UI::S(1));
        wdl->AddLine(wp, ImVec2(wp.x + ds.x * 0.12f, wp.y), IM_COL32(126, 178, 226, 230), UI::S(2));
    }

    // input line
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float lineH = UI::S(56.0f);
        const bool tools = !s_bNumeric;                       // 4.04: Copy / Paste (+ GIF in chat) on the input line
        const int nTools = tools ? (s_bChat ? 3 : 2) : 0;
        const float toolW = UI::S(118), sp = ImGui::GetStyle().ItemSpacing.x;
        float w = ds.x - pad * 2 - nTools * (toolW + sp);
        VkChamfer(dl, p, ImVec2(p.x + w, p.y + lineH), 0, IM_COL32(32, 36, 42, 255), IM_COL32(126, 178, 226, 200), UI::S(1));   // 4.04: square search box
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
        if(nTools)
        {
            ImVec2 keep = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(p.x + w + sp, p.y));
            bool copied = ImGui::GetTime() - s_dCopied < 1.2;
            if(Key(copied ? "COPIED##vkcp" : "COPY##vkcp", toolW, lineH, copied ? VK_TEAL : VK_DIM) && !s_Text.empty()) { Clipboard::Copy(s_Text.c_str()); s_dCopied = ImGui::GetTime(); }
            ImGui::SameLine();
            if(Key(s_bPasteWait ? "...##vkps" : "PASTE##vkps", toolW, lineH, VK_DIM) && !s_bPasteWait) { s_bPasteWait = true; Clipboard::RequestPaste(); }
            if(s_bChat)
            {
                ImGui::SameLine();
                if(Key("GIF##vkgif", toolW, lineH, s_bGif ? VK_TEAL : VK_DIM)) { s_bGif = !s_bGif; s_bEmoji = false; s_fGifScroll = 0; Gifs::Init(); }
            }
            ImGui::SetCursorScreenPos(keep);
        }
        if(s_bPasteWait)
        {
            std::string got;
            if(Clipboard::PollPaste(got))
            {
                s_bPasteWait = false;
                for(char c : got) { if((int)s_Text.size() >= s_nMaxLen - 1) break; if((unsigned char)c >= 32 || (c & 0x80)) s_Text.push_back(c); }
                while(!s_Text.empty() && ((unsigned char)s_Text.back() & 0xC0) == 0x80 && (int)s_Text.size() >= s_nMaxLen - 1) DeleteLastChar();   // no half UTF-8 char at the cut
            }
        }
        ImGui::Dummy(ImVec2(w, lineH + pad));
    }

    const char** rows = s_bNumeric ? kRowsNum : (s_bSymbols ? kRowsSym : (s_bShift ? kRowsUpper : kRowsLower));
    const int maxCols = s_bNumeric ? 3 : 10;
    const float avail = ds.x - pad * 2;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float keyW = (avail - spacing * (maxCols - 1)) / maxCols;
    const float keyH = (kbH - UI::S(56.0f) - pad * 4) / 5.2f;

    ImGui::SetWindowFontScale(1.15f);
    if(s_bGif && s_bChat && !s_bNumeric)   // 4.04: GIF page in place of the 4 key rows
    {
        const float areaH = keyH * 4 + ImGui::GetStyle().ItemSpacing.y * 3;
        ImVec2 o = ImGui::GetCursorScreenPos(); o.x = ImGui::GetWindowPos().x + pad;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        const float catH = keyH * 0.62f, gap = UI::S(6);
        const ImVec2 c0 = o, c1(o.x + avail, o.y + catH);              // category strip
        const ImVec2 g0(o.x, o.y + catH + gap), g1(o.x + avail, o.y + areaH);   // grid
        ImGui::InvisibleButton("##vkgifarea", ImVec2(avail, areaH));
        const std::vector<std::string>& cats = Gifs::Categories();
        if(s_nGifCat >= (int)cats.size()) s_nGifCat = 0;
        // touch: drag scrolls (strip sideways, grid up / down), a tap picks
        ImVec2 m = io.MousePos;
        auto In = [&](ImVec2 a, ImVec2 b) { return m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y; };
        if(io.MouseClicked[0] && (In(c0, c1) || In(g0, g1))) { s_bGifDown = true; s_bGifDrag = false; s_fGifDownX = m.x; s_fGifDownY = m.y; s_nGifDownArea = In(c0, c1) ? 1 : 2; }
        bool tap = false;
        if(s_bGifDown)
        {
            if(fabsf(m.x - s_fGifDownX) + fabsf(m.y - s_fGifDownY) > UI::S(14)) s_bGifDrag = true;
            if(s_bGifDrag) { if(s_nGifDownArea == 1) s_fCatScroll -= io.MouseDelta.x; else s_fGifScroll -= io.MouseDelta.y; }
            if(!io.MouseDown[0]) { tap = !s_bGifDrag; s_bGifDown = false; }
        }
        if(In(g0, g1) && io.MouseWheel != 0) s_fGifScroll -= io.MouseWheel * UI::S(80);
        dl->AddRectFilled(o, ImVec2(o.x + avail, o.y + areaH), IM_COL32(20, 23, 27, 255));
        // categories
        dl->PushClipRect(c0, c1, true);
        float cx = c0.x - s_fCatScroll, totalW = 0;
        const float cfs = UI::S(22);
        for(int i = 0; i < (int)cats.size(); ++i)
        {
            std::string lb = cats[i]; for(char& ch : lb) ch = (char)toupper((unsigned char)ch);
            float tw = UI::Font()->CalcTextSizeA(cfs, FLT_MAX, 0, lb.c_str()).x + UI::S(28);
            ImVec2 a(cx, c0.y), b(cx + tw, c1.y);
            bool sel = i == s_nGifCat;
            dl->AddRectFilled(a, b, sel ? IM_COL32(126, 178, 226, 235) : IM_COL32(52, 58, 66, 235));
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 30), 0, 0, UI::S(1));
            dl->AddText(UI::Font(), cfs, ImVec2(a.x + UI::S(14), a.y + (catH - cfs) * 0.5f), sel ? IM_COL32(16, 22, 30, 255) : IM_COL32(226, 229, 233, 255), lb.c_str());
            if(tap && s_nGifDownArea == 1 && m.x >= a.x && m.x < b.x && In(c0, c1)) { s_nGifCat = i; s_fGifScroll = 0; }
            cx += tw + UI::S(4); totalW += tw + UI::S(4);
        }
        dl->PopClipRect();
        float maxCat = ImMax(0.0f, totalW - avail); if(s_fCatScroll > maxCat) s_fCatScroll = maxCat; if(s_fCatScroll < 0) s_fCatScroll = 0;
        // grid: 2 rows visible, 4:3 tiles
        static std::vector<int> ids; static int s_nListCat = -1; static size_t s_nListN = 0;
        if(s_nListCat != s_nGifCat || ids.empty() || s_nListN != cats.size())
        {
            Gifs::List(cats.empty() ? nullptr : cats[s_nGifCat].c_str(), nullptr, ids);
            s_nListCat = s_nGifCat; s_nListN = cats.size();
        }
        const float tileH = (g1.y - g0.y - gap) * 0.5f, tileW0 = tileH * 4.0f / 3.0f;
        int cols = ImMax(1, (int)((avail + gap) / (tileW0 + gap)));
        const float tileW = (avail - gap * (cols - 1)) / cols;
        int nRows = ((int)ids.size() + cols - 1) / cols;
        float maxScroll = ImMax(0.0f, nRows * (tileH + gap) - gap - (g1.y - g0.y));
        if(s_fGifScroll > maxScroll) s_fGifScroll = maxScroll; if(s_fGifScroll < 0) s_fGifScroll = 0;
        dl->PushClipRect(g0, g1, true);
        if(!Gifs::Ready())
            dl->AddText(UI::Font(), UI::S(26), ImVec2(g0.x + UI::S(16), g0.y + UI::S(16)), IM_COL32(150, 157, 166, 255), "Loading GIFs...");
        int r0 = (int)(s_fGifScroll / (tileH + gap)), r1 = (int)((s_fGifScroll + (g1.y - g0.y)) / (tileH + gap));
        int picked = 0;
        for(int r = r0; r <= r1 && r < nRows; ++r)
            for(int c = 0; c < cols; ++c)
            {
                int k = r * cols + c; if(k >= (int)ids.size()) break;
                ImVec2 a(g0.x + c * (tileW + gap), g0.y + r * (tileH + gap) - s_fGifScroll), b(a.x + tileW, a.y + tileH);
                dl->AddRectFilled(a, b, IM_COL32(44, 49, 56, 255));
                ImVec2 u0, u1; float asp = 4.0f / 3.0f;
                if(void* t = HUD::GifFrame(ids[k], &u0, &u1, &asp))
                {   // fit inside the tile, keep the aspect
                    float iw = tileW, ih = tileW / asp;
                    if(ih > tileH) { ih = tileH; iw = tileH * asp; }
                    ImVec2 p0(a.x + (tileW - iw) * 0.5f, a.y + (tileH - ih) * 0.5f);
                    dl->AddImage((ImTextureID)t, p0, ImVec2(p0.x + iw, p0.y + ih), u0, u1);
                }
                else dl->AddText(UI::Font(), UI::S(20), ImVec2(a.x + UI::S(8), a.y + UI::S(8)), IM_COL32(150, 157, 166, 255), "...");
                dl->AddRect(a, b, IM_COL32(255, 255, 255, 28), 0, 0, UI::S(1));
                if(tap && s_nGifDownArea == 2 && In(g0, g1) && m.x >= a.x && m.x < b.x && m.y >= a.y && m.y < b.y) picked = ids[k];
            }
        dl->PopClipRect();
        if(maxScroll > 0)
        {
            float vh = g1.y - g0.y, bh = ImMax(UI::S(20), vh * vh / (vh + maxScroll)), by = g0.y + (vh - bh) * (s_fGifScroll / maxScroll);
            dl->AddRectFilled(ImVec2(g1.x - UI::S(4), by), ImVec2(g1.x, by + bh), IM_COL32(126, 178, 226, 160));
        }
        if(picked)
        {   // send it right away as "[gif:N]" (every client draws it as a tall chat row)
            char msg[24]; snprintf(msg, sizeof(msg), "[gif:%d]", picked);
            SubmitFn fn = s_fnSubmit; void* d = s_pData;
            s_bOpen = false; s_bGif = false;
            if(fn) fn(msg, d);
        }
    }
    else
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
        if(Key(s_bSymbols ? "ABC##sy" : "?123##sy", unit * 1.3f, keyH, s_bSymbols ? VK_TEAL : VK_DIM)) { s_bSymbols = !s_bSymbols; s_bEmoji = false; s_bGif = false; }
        ImGui::SameLine();
        {   // 2.19: emoji page
            char el[24]; snprintf(el, sizeof(el), "%s##emj", s_bEmoji ? "ABC" : UI::EmojiGlyph(0));
            if(Key(el, unit * 1.0f, keyH, s_bEmoji ? VK_TEAL : VK_DIM)) { s_bEmoji = !s_bEmoji; s_bGif = false; }
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
