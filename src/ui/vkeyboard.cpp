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
#include <mod/logger.h>

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
    bool        s_bPhoneAsk = false;   // 4.13.42: ask on the next frame (after SetChat, so chat gets the GIF button)
    std::string s_PhoneErr;            // 4.13.42: why the phone keyboard did not open (shown on the MP keyboard)
    double      s_dPhoneErr = -100;
    // 4.13.42: GIF search tab
    bool        s_bGifSearch = false;  // the SEARCH tab is selected
    bool        s_bGifTyping = false;  // its letter keys are up (the keyboard types the search, not the message)
    std::string s_GifQuery;

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
    s_bPhone = false; s_PhoneMsg.clear(); s_bGifSearch = false; s_bGifTyping = false;
    if(s_bUsePhone) { s_bPhone = true; s_bPhoneAsk = true; }   // 4.12.3; 4.13.42: asked in Render (chat known by then)
}
void VKeyboard::SetPhoneKeyboard(bool phone) { s_bUsePhone = phone; }
bool VKeyboard::PhoneKeyboard() { return s_bUsePhone; }

// 4.12.3: the phone keyboard's dialog: OK / Enter = submit (the same checks as MP's Enter), Cancel = close,
// could not open = MP's own keyboard after all
static bool PhoneTick()
{
    if(!s_bPhone) return false;
    if(s_bPhoneAsk)
    {
        s_bPhoneAsk = false;
        if(!FilePick::AskText(s_Title.c_str(), s_Text.c_str(), s_nMaxLen - 1, s_bNumeric, s_bChat && !s_bNumeric))
        {
            s_PhoneErr = FilePick::TextError(); if(s_PhoneErr.empty()) s_PhoneErr = "no activity";
            s_dPhoneErr = ImGui::GetTime(); s_bPhone = false;
            return false;   // the MP keyboard this time
        }
        return true;
    }
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
    if(st == 7)
    {   // 4.13.42: the dialog's GIF button: MP's keyboard on its GIF page (what was typed stays in the box)
        std::string t = FilePick::Text(); FilePick::TextReset();
        if((int)t.size() > s_nMaxLen - 1) t.resize(s_nMaxLen - 1);
        s_Text = t; s_bPhone = false; s_bGif = true; s_bEmoji = false; s_fGifScroll = 0; Gifs::Init();
        return false;
    }
    if(st == 4 || st == 6)
    {   // the MP keyboard instead - and say why (it used to switch silently)
        s_PhoneErr = FilePick::TextError(); if(s_PhoneErr.empty()) s_PhoneErr = st == 6 ? "could not load the keyboard helper" : "the dialog failed";
        s_dPhoneErr = ImGui::GetTime();
        logger->Error("Phone keyboard did not open (%d): %s - using the MP keyboard", st, s_PhoneErr.c_str());
        FilePick::TextReset(); s_bPhone = false;
        return false;
    }
    FilePick::TextReset();
    s_bPhone = false;
    s_bOpen = false;                        // cancelled / closed
    return true;
}

void VKeyboard::SetValidator(SubmitFn forSubmit, CheckFn check) { s_fnValFor = forSubmit; s_fnCheck = check; }
void VKeyboard::Close() { s_bOpen = false; if(s_bPhone) { s_bPhone = false; s_bPhoneAsk = false; FilePick::CancelText(); } }
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
        if(!bad && ImGui::GetTime() - s_dPhoneErr < 6.0 && !s_PhoneErr.empty())
        {   // 4.13.42: why the phone keyboard did not open
            std::string pe = "Phone keyboard unavailable: " + s_PhoneErr;
            if(pe.size() > 70) { pe.resize(67); pe += "..."; }
            ImVec2 bs = UI::Font()->CalcTextSizeA(UI::S(20), FLT_MAX, 0, pe.c_str());
            dl->AddText(UI::Font(), UI::S(20), ImVec2(p.x + w - bs.x - UI::S(14), p.y + (lineH - UI::S(20)) * 0.5f), IM_COL32(255, 170, 80, 255), pe.c_str());
        }
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
                if(Key("GIF##vkgif", toolW, lineH, s_bGif ? VK_TEAL : VK_DIM)) { s_bGif = !s_bGif; s_bEmoji = false; s_fGifScroll = 0; s_bGifSearch = false; s_bGifTyping = false; Gifs::Init(); }
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
        // 4.13.42: SEARCH tab - while typing, 1 row of results + 3 rows of letters (they type the search)
        const bool typing = s_bGifSearch && s_bGifTyping;
        const float lettH = typing ? (areaH - catH - gap * 2) * 0.56f : 0.0f;   // the 3 letter rows
        const float topH = areaH - lettH - (typing ? gap : 0.0f);
        const ImVec2 c0 = o, c1(o.x + avail, o.y + catH);              // category strip / search box
        const ImVec2 g0(o.x, o.y + catH + gap), g1(o.x + avail, o.y + topH);   // grid
        ImGui::InvisibleButton("##vkgifarea", ImVec2(avail, topH));
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
            if(s_bGifDrag) { if(s_nGifDownArea == 1) s_fCatScroll -= io.MouseDelta.x; else s_fGifScroll -= (typing ? io.MouseDelta.x : io.MouseDelta.y); }
            if(!io.MouseDown[0]) { tap = !s_bGifDrag; s_bGifDown = false; }
        }
        if(In(g0, g1) && io.MouseWheel != 0) s_fGifScroll -= io.MouseWheel * UI::S(80);
        dl->AddRectFilled(o, ImVec2(o.x + avail, o.y + topH), IM_COL32(20, 23, 27, 255));
        const float cfs = UI::S(22);
        auto Chip = [&](float x, const char* lb, bool sel, ImU32 selCol) -> float
        {   // one strip button, returns its width
            float tw = UI::Font()->CalcTextSizeA(cfs, FLT_MAX, 0, lb).x + UI::S(28);
            ImVec2 a(x, c0.y), b(x + tw, c1.y);
            dl->AddRectFilled(a, b, sel ? selCol : IM_COL32(52, 58, 66, 235));
            dl->AddRect(a, b, IM_COL32(255, 255, 255, 30), 0, 0, UI::S(1));
            dl->AddText(UI::Font(), cfs, ImVec2(a.x + UI::S(14), a.y + (catH - cfs) * 0.5f), sel ? IM_COL32(16, 22, 30, 255) : IM_COL32(226, 229, 233, 255), lb);
            return tw;
        };
        auto Tapped = [&](float x0, float x1) { return tap && s_nGifDownArea == 1 && In(c0, c1) && m.x >= x0 && m.x < x1; };
        bool listDirty = false;
        if(!s_bGifSearch)
        {   // categories, SEARCH first
            dl->PushClipRect(c0, c1, true);
            float cx = c0.x - s_fCatScroll, totalW = 0;
            {
                float tw = Chip(cx, "SEARCH", false, 0);
                if(Tapped(cx, cx + tw)) { s_bGifSearch = true; s_bGifTyping = true; s_fGifScroll = 0; listDirty = true; }
                cx += tw + UI::S(4); totalW += tw + UI::S(4);
            }
            for(int i = 0; i < (int)cats.size(); ++i)
            {
                std::string lb = cats[i]; for(char& ch : lb) ch = (char)toupper((unsigned char)ch);
                float tw = Chip(cx, lb.c_str(), i == s_nGifCat, IM_COL32(126, 178, 226, 235));
                if(Tapped(cx, cx + tw)) { s_nGifCat = i; s_fGifScroll = 0; }
                cx += tw + UI::S(4); totalW += tw + UI::S(4);
            }
            dl->PopClipRect();
            float maxCat = ImMax(0.0f, totalW - avail); if(s_fCatScroll > maxCat) s_fCatScroll = maxCat; if(s_fCatScroll < 0) s_fCatScroll = 0;
        }
        else
        {   // < BACK | [ search box ] | CLEAR
            float bx = c0.x;
            float bw = Chip(bx, "< BACK", false, 0);
            if(Tapped(bx, bx + bw)) { s_bGifSearch = false; s_bGifTyping = false; s_fGifScroll = 0; listDirty = true; }
            const char* clr = "CLEAR";
            float cw = UI::Font()->CalcTextSizeA(cfs, FLT_MAX, 0, clr).x + UI::S(28);
            float clx = c1.x - cw;
            Chip(clx, clr, false, 0);
            if(Tapped(clx, c1.x) && !s_GifQuery.empty()) { s_GifQuery.clear(); s_fGifScroll = 0; listDirty = true; s_bGifTyping = true; }
            ImVec2 sa(bx + bw + UI::S(4), c0.y), sb(clx - UI::S(4), c1.y);
            dl->AddRectFilled(sa, sb, IM_COL32(32, 36, 42, 255));
            dl->AddRect(sa, sb, s_bGifTyping ? IM_COL32(126, 178, 226, 220) : IM_COL32(255, 255, 255, 40), 0, 0, UI::S(1));
            std::string q = s_GifQuery;
            if(s_bGifTyping && ((int)(ImGui::GetTime() * 2.0)) % 2 == 0) q += "_";
            const bool ph = s_GifQuery.empty();
            dl->PushClipRect(sa, sb, true);
            dl->AddText(UI::Font(), cfs, ImVec2(sa.x + UI::S(12), sa.y + (catH - cfs) * 0.5f), ph ? IM_COL32(150, 157, 166, 255) : IM_COL32_WHITE,
                        ph ? (s_bGifTyping ? "Search GIFs: type a word (cat, dance, lol...)_" : "Search GIFs (tap to type)") : q.c_str());
            dl->PopClipRect();
            if(Tapped(sa.x, sb.x)) s_bGifTyping = true;
        }
        // grid: 2 rows visible (1 row, scrolled sideways, while typing a search), 4:3 tiles
        static std::vector<int> ids; static int s_nListCat = -1; static size_t s_nListN = 0; static std::string s_ListQ; static bool s_bListSearch = false, s_bListReady = false;
        if(listDirty || s_bListSearch != s_bGifSearch || (s_bGifSearch ? s_ListQ != s_GifQuery : s_nListCat != s_nGifCat) || s_nListN != cats.size() || s_bListReady != Gifs::Ready() || (ids.empty() && !s_bGifSearch))
        {
            if(s_bGifSearch) Gifs::List(nullptr, s_GifQuery.c_str(), ids);
            else Gifs::List(cats.empty() ? nullptr : cats[s_nGifCat].c_str(), nullptr, ids);
            s_nListCat = s_nGifCat; s_nListN = cats.size(); s_ListQ = s_GifQuery; s_bListSearch = s_bGifSearch; s_bListReady = Gifs::Ready();
        }
        const float gridH = g1.y - g0.y;
        const float tileH = typing ? gridH : (gridH - gap) * 0.5f, tileW0 = tileH * 4.0f / 3.0f;
        int cols, nRows;
        float tileW, maxScroll;
        if(typing)
        {   // one row, sideways
            tileW = tileW0; cols = ImMax(1, (int)ids.size()); nRows = 1;
            maxScroll = ImMax(0.0f, ids.size() * (tileW + gap) - gap - avail);
        }
        else
        {
            cols = ImMax(1, (int)((avail + gap) / (tileW0 + gap)));
            tileW = (avail - gap * (cols - 1)) / cols;
            nRows = ((int)ids.size() + cols - 1) / cols;
            maxScroll = ImMax(0.0f, nRows * (tileH + gap) - gap - gridH);
        }
        if(s_fGifScroll > maxScroll) s_fGifScroll = maxScroll; if(s_fGifScroll < 0) s_fGifScroll = 0;
        dl->PushClipRect(g0, g1, true);
        if(!Gifs::Ready())
            dl->AddText(UI::Font(), UI::S(26), ImVec2(g0.x + UI::S(16), g0.y + UI::S(16)), IM_COL32(150, 157, 166, 255), "Loading GIFs...");
        else if(ids.empty())
            dl->AddText(UI::Font(), UI::S(24), ImVec2(g0.x + UI::S(16), g0.y + UI::S(16)), IM_COL32(150, 157, 166, 255),
                        s_bGifSearch ? (s_GifQuery.empty() ? "Type to search all GIFs" : "No GIFs found - try another word") : "No GIFs here");
        int picked = 0;
        auto Tile = [&](int k, ImVec2 a)
        {
            ImVec2 b(a.x + tileW, a.y + tileH);
            if(b.x < g0.x || a.x > g1.x || b.y < g0.y || a.y > g1.y) return;
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
        };
        if(typing)
        {
            int k0 = (int)(s_fGifScroll / (tileW + gap)), k1 = (int)((s_fGifScroll + avail) / (tileW + gap));
            for(int k = ImMax(0, k0); k <= k1 && k < (int)ids.size(); ++k) Tile(k, ImVec2(g0.x + k * (tileW + gap) - s_fGifScroll, g0.y));
        }
        else
        {
            int r0 = (int)(s_fGifScroll / (tileH + gap)), r1 = (int)((s_fGifScroll + gridH) / (tileH + gap));
            for(int r = r0; r <= r1 && r < nRows; ++r)
                for(int c = 0; c < cols; ++c)
                {
                    int k = r * cols + c; if(k >= (int)ids.size()) break;
                    Tile(k, ImVec2(g0.x + c * (tileW + gap), g0.y + r * (tileH + gap) - s_fGifScroll));
                }
        }
        dl->PopClipRect();
        if(maxScroll > 0)
        {
            if(typing)
            {
                float vw = avail, bw2 = ImMax(UI::S(20), vw * vw / (vw + maxScroll)), bx2 = g0.x + (vw - bw2) * (s_fGifScroll / maxScroll);
                dl->AddRectFilled(ImVec2(bx2, g1.y - UI::S(4)), ImVec2(bx2 + bw2, g1.y), IM_COL32(126, 178, 226, 160));
            }
            else
            {
                float vh = gridH, bh = ImMax(UI::S(20), vh * vh / (vh + maxScroll)), by = g0.y + (vh - bh) * (s_fGifScroll / maxScroll);
                dl->AddRectFilled(ImVec2(g1.x - UI::S(4), by), ImVec2(g1.x, by + bh), IM_COL32(126, 178, 226, 160));
            }
        }
        if(typing)
        {   // the 3 letter rows type the search
            static const char* kSearchRows[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
            const float rowSp = ImGui::GetStyle().ItemSpacing.y;
            const float kh = (lettH - rowSp * 2) / 3.0f;
            float y = o.y + topH + gap;
            for(int r = 0; r < 3; ++r, y += kh + rowSp)
            {
                const char* k = kSearchRows[r];
                int n = (int)strlen(k);
                float rowW = n * keyW + (n - 1) * spacing;
                ImGui::SetCursorScreenPos(ImVec2(o.x + (avail - rowW) * 0.5f, y));
                for(int i = 0; i < n; ++i)
                {
                    char lbl[16]; snprintf(lbl, sizeof(lbl), "%c##gs%d%d", k[i], r, i);
                    if(Key(lbl, keyW, kh) && s_GifQuery.size() < 32) { s_GifQuery.push_back(k[i]); s_fGifScroll = 0; }
                    if(i < n - 1) ImGui::SameLine();
                }
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + pad, o.y + areaH + ImGui::GetStyle().ItemSpacing.y));
        if(picked)
        {   // send it right away as "[gif:N]" (every client draws it as a tall chat row)
            char msg[24]; snprintf(msg, sizeof(msg), "[gif:%d]", picked);
            SubmitFn fn = s_fnSubmit; void* d = s_pData;
            s_bOpen = false; s_bGif = false; s_bGifSearch = false; s_bGifTyping = false;
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
    const bool gifTyping = s_bGif && s_bChat && !s_bNumeric && s_bGifSearch && s_bGifTyping && s_bOpen;   // 4.13.42
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
        if(Key("Space##sp", unit * 2.4f - spacing, keyH)) { if(gifTyping) { if(!s_GifQuery.empty() && s_GifQuery.back() != ' ' && s_GifQuery.size() < 32) s_GifQuery.push_back(' '); } else Append(' '); }
        ImGui::SameLine();
    }
    else
    {
        if(Key("Space##sp", unit * 6.0f + spacing * 2, keyH)) Append(' ');
        ImGui::SameLine();
    }
    if(Key("<- Del##bk", unit * 1.3f, keyH, VK_DIM)) { if(gifTyping) { if(!s_GifQuery.empty()) { s_GifQuery.pop_back(); s_fGifScroll = 0; } } else if(!s_Text.empty()) DeleteLastChar(); }
    ImGui::SameLine();
    if(gifTyping)
    {   // 4.13.42: Done = the letters go away, the results get the whole page
        if(Key("Done##en", unit * 1.5f, keyH, VK_RED)) { s_bGifTyping = false; s_fGifScroll = 0; }
    }
    else
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
