// 2.19: TopbarPlus v3 style menu.
//  - a top bar of dark, semi-transparent, rounded-square "pills" (no outline):  [ ⋮ ]  [ Chat ]
//    (drag any pill to move the bar; a selected pill turns white with black text, like TopbarPlus)
//  - ⋮ opens a dropdown:  Help  /  Server  /  Player list
//  - Server = every MP section (connection, servers, display, player, add-on sections of other mods)
//  - Player list / Help = one page each (set by MP with SetPage)
#include "floatmenu.h"
#include "ui_common.h"
#include "vkeyboard.h"
#include <vector>
#include <string>
#include <atomic>
#include <math.h>

namespace MP { bool IsConnected(); void AddSystemMessage(uint32_t rgba, const char* text); bool AddResource(const char* name); }

namespace
{
    struct Section  { std::string title; MPMenuSectionFn fn; void* data; };
    struct Window   { MPMenuWindowFn fn; void* data; };
    struct Bubble   { std::string label; ImVec2 rel; MPMenuSectionFn fn; void* data; bool bar; const bool* active; bool dots;
                      int (*micState)() = nullptr; };   // 3.11: voice chat pill (0 not connected, 1 mic off, 2 mic on, 3 talking)
    struct Page     { const char* title; MPMenuSectionFn fn; void* data; bool open; };

    std::vector<Section> s_Sections;
    std::vector<Window>  s_Windows;
    std::vector<Bubble>  s_Bubbles;
    Page   s_Pages[FloatMenu::PAGE_COUNT] = {
        { "Server##MPServer",       nullptr, nullptr, false },
        { "Player list##MPPlayers", nullptr, nullptr, false },
        { "Help##MPHelp",           nullptr, nullptr, false },
    };
    bool   s_bDropdown = false;
    uint64_t s_nDropdownFrame = 0;
    ImVec2 s_BarPos(0.5f, 0.06f);
    bool   s_bBarMoved = false;
    bool   s_bHidden = false;
}

static bool s_bDotsActive = false;

void FloatMenu::ApplyStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding    = UI::S(16);
    st.ChildRounding     = UI::S(12);
    st.FrameRounding     = UI::S(12);
    st.GrabRounding      = UI::S(12);
    st.PopupRounding     = UI::S(12);
    st.ScrollbarRounding = UI::S(12);
    st.WindowBorderSize  = 0;          // 2.19: no outline
    st.ChildBorderSize   = 0;
    st.PopupBorderSize   = 0;
    st.FrameBorderSize   = 0;
    st.WindowPadding     = ImVec2(UI::S(18), UI::S(16));
    st.FramePadding      = ImVec2(UI::S(16), UI::S(12));
    st.ItemSpacing       = ImVec2(UI::S(12), UI::S(12));
    st.ItemInnerSpacing  = ImVec2(UI::S(10), UI::S(8));
    st.ScrollbarSize     = UI::S(30);
    st.GrabMinSize       = UI::S(34);
    st.TouchExtraPadding = ImVec2(UI::S(4), UI::S(4));
    st.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.72f, 0.72f, 0.72f, 1);
    c[ImGuiCol_WindowBg]             = ImVec4(0.0f, 0.0f, 0.0f, 0.62f);   // TopbarPlus: dark see-through
    c[ImGuiCol_ChildBg]              = ImVec4(0.0f, 0.0f, 0.0f, 0.25f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.05f, 0.05f, 0.05f, 0.90f);
    c[ImGuiCol_Border]               = ImVec4(1, 1, 1, 0.0f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.0f, 0.0f, 0.0f, 0.70f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.0f, 0.0f, 0.0f, 0.78f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
    c[ImGuiCol_FrameBg]              = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(1, 1, 1, 0.20f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(1, 1, 1, 0.26f);
    c[ImGuiCol_Button]               = ImVec4(1, 1, 1, 0.94f);          // white buttons, black text
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.90f, 0.90f, 0.90f, 0.96f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
    c[ImGuiCol_Header]               = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(1, 1, 1, 0.18f);
    c[ImGuiCol_HeaderActive]         = ImVec4(1, 1, 1, 0.26f);
    c[ImGuiCol_CheckMark]            = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_SliderGrab]           = ImVec4(1, 1, 1, 0.95f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.85f, 0.85f, 0.85f, 1);
    c[ImGuiCol_Separator]            = ImVec4(1, 1, 1, 0.16f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(1, 1, 1, 0.30f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.40f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(1, 1, 1, 0.50f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(1, 1, 1, 0.25f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(1, 1, 1, 0.40f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.04f);
}

void FloatMenu::AddSection(const char* t, MPMenuSectionFn fn, void* d) { s_Sections.push_back({ t ? t : "Section", fn, d }); }
void FloatMenu::AddFloatingWindow(MPMenuWindowFn fn, void* d)          { s_Windows.push_back({ fn, d }); }
void FloatMenu::AddBubble(const char* l, float x, float y, MPMenuSectionFn fn, void* d) { s_Bubbles.push_back({ l ? l : "?", ImVec2(x, y), fn, d, false, nullptr, false }); }
void FloatMenu::AddBarButton(const char* l, MPMenuSectionFn fn, void* d, const bool* active) { s_Bubbles.push_back({ l ? l : "?", ImVec2(0.5f, 0.06f), fn, d, true, active, false }); }
void FloatMenu::AddMicButton(MPMenuSectionFn fn, int (*state)())
{
    Bubble b = { "mic", ImVec2(0.5f, 0.06f), fn, nullptr, true, nullptr, false };
    b.micState = state;
    s_Bubbles.push_back(b);
}
void FloatMenu::AddDotsButton() { s_Bubbles.push_back({ "...", ImVec2(0.5f, 0.06f), nullptr, nullptr, true, &s_bDotsActive, true }); }
void FloatMenu::SetBarPos(float x, float y) { s_BarPos = ImVec2(ImClamp(x, 0.0f, 1.0f), ImClamp(y, 0.0f, 1.0f)); }
bool FloatMenu::GetBarPos(float* x, float* y) { *x = s_BarPos.x; *y = s_BarPos.y; bool m = s_bBarMoved; s_bBarMoved = false; return m; }
void FloatMenu::SetPage(int page, MPMenuSectionFn fn, void* d) { if(page >= 0 && page < PAGE_COUNT) { s_Pages[page].fn = fn; s_Pages[page].data = d; } }
void FloatMenu::OpenPage(int page)
{
    if(page < 0 || page >= PAGE_COUNT) return;
    s_Pages[page].open = true;
    s_bDropdown = false;
    ImGui::SetWindowFocus(s_Pages[page].title);
}
bool FloatMenu::IsPageOpen(int page) { return page >= 0 && page < PAGE_COUNT && s_Pages[page].open; }
void FloatMenu::ClosePage(int page) { if(page >= 0 && page < PAGE_COUNT) s_Pages[page].open = false; }
void FloatMenu::CloseAll() { for(auto& p : s_Pages) p.open = false; s_bDropdown = false; }
void FloatMenu::SetHidden(bool h) { s_bHidden = h; if(h) s_bDropdown = false; }
void FloatMenu::Toggle() { s_Pages[PAGE_SERVER].open = !s_Pages[PAGE_SERVER].open; s_bDropdown = false; }
bool FloatMenu::IsOpen() { for(auto& p : s_Pages) if(p.open) return true; return s_bDropdown; }

bool FloatMenu::BigButton(const char* label, unsigned int col)
{
    if(col) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(col));
    bool r = ImGui::Button(label, ImVec2(-FLT_MIN, UI::S(70)));
    if(col) ImGui::PopStyleColor();
    return r;
}

bool FloatMenu::ToggleSwitch(const char* label, bool* v)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = UI::S(44), w = h * 1.9f;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - w);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##sw", ImVec2(w, h));
    if(clicked) *v = !*v;
    float t = *v ? 1.0f : 0.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), *v ? UI::kPillActive : IM_COL32(255, 255, 255, 45), h * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + t * (w - h), p.y + h * 0.5f), h * 0.5f - UI::S(4), *v ? IM_COL32(20, 20, 20, 255) : IM_COL32_WHITE);
    ImGui::PopID();
    return clicked;
}

bool FloatMenu::TextField(const char* label, const char* value, const char* kbTitle, int maxLen, void (*onSubmit)(const char*, void*), void* data, bool numeric, bool masked)
{
    // 2.19.1: drawn by hand so the text is always black on the white field (and a grey hint
    // when empty); a long label gets its own line instead of running under the field
    ImGui::PushID(label);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float labelW = ImGui::CalcTextSize(label, nullptr, true).x;   // 2.25: "Reason##id" shows "Reason"
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
    if(labelW + UI::S(16) < avail * 0.40f) ImGui::SameLine(ImGui::GetContentRegionMax().x * 0.40f);
    char shown[160];
    if(masked && value[0])   // 2.16: e.g. a custom server's IP / password: never shown on screen
    {
        int n = (int)strlen(value); if(n > 16) n = 16;
        memset(shown, '*', n); shown[n] = 0;
    }
    else snprintf(shown, sizeof(shown), "%s", value);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool r = ImGui::InvisibleButton("##tf", ImVec2(w > 1 ? w : 1, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rr = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::IsItemActive() ? IM_COL32(225, 225, 225, 250) : IM_COL32(255, 255, 255, 245), rr);
    const bool empty = !shown[0];
    const char* txt = empty ? (kbTitle && *kbTitle ? kbTitle : "Tap to type") : shown;
    ImVec2 ts = ImGui::CalcTextSize(txt);
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    dl->PushClipRect(ImVec2(p.x + pad.x * 0.5f, p.y), ImVec2(p.x + w - pad.x * 0.5f, p.y + h), true);
    dl->AddText(ImVec2(p.x + pad.x, p.y + (h - ts.y) * 0.5f), empty ? IM_COL32(120, 120, 120, 255) : IM_COL32(10, 10, 10, 255), txt);
    dl->PopClipRect();
    if(r) VKeyboard::Open(kbTitle, value, maxLen, onSubmit, data, numeric);
    ImGui::PopID();
    return r;
}

// ---- top bar: hit-tested straight from raw touch events (no ImGui input needed) -----------
static std::atomic<int> s_nActiveBubble(-1);   // pill held by a finger
static std::atomic<int> s_nPendingTap(-1);     // tapped pill, handled on the render thread
static int   s_nBubbleFinger = -1;
static float s_fPressX = 0, s_fPressY = 0;
static bool  s_bDragged = false;
static float s_fGrabDX = 0, s_fGrabDY = 0;

static float PillH() { return UI::S(64); }
static ImVec2 BubbleSize(const Bubble& b)
{
    if(b.dots || b.micState) return ImVec2(PillH(), PillH());   // square icon pill
    float fs = UI::S(30);
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, b.label.c_str());
    return ImVec2(ts.x + UI::S(44), PillH());
}

static float BarGap() { return UI::S(12); }
static float BarWidth()
{
    float w = 0; int n = 0;
    for(auto& b : s_Bubbles) if(b.bar) { w += BubbleSize(b).x; ++n; }
    return n ? w + BarGap() * (n - 1) : 0;
}
static ImVec2 BarCentre(const ImVec2& ds)
{
    float hw = BarWidth() * 0.5f, hh = PillH() * 0.5f;
    float cx = ImClamp(s_BarPos.x * ds.x, hw + UI::S(8), ds.x - hw - UI::S(8));
    float cy = ImClamp(s_BarPos.y * ds.y, hh + UI::S(6), ds.y - hh);
    return ImVec2(cx, cy);
}
bool FloatMenu::BarBottom(float* cx, float* y)   // 3.11: where a panel under the top bar goes
{
    if(s_bHidden || BarWidth() <= 0) return false;
    ImVec2 c = BarCentre(ImGui::GetIO().DisplaySize);
    *cx = c.x; *y = c.y + PillH() * 0.5f + UI::S(10);
    return true;
}
static ImVec2 BubbleCentre(size_t i, const ImVec2& ds)
{
    const Bubble& b = s_Bubbles[i];
    if(!b.bar) return ImVec2(b.rel.x * ds.x, b.rel.y * ds.y);
    ImVec2 c = BarCentre(ds);
    float x = c.x - BarWidth() * 0.5f;
    for(size_t k = 0; k < s_Bubbles.size(); ++k)
    {
        if(!s_Bubbles[k].bar) continue;
        float w = BubbleSize(s_Bubbles[k]).x;
        if(k == i) return ImVec2(x + w * 0.5f, c.y);
        x += w + BarGap();
    }
    return c;
}

static std::atomic<bool> s_bModal{false};   // 2.26: written on the game thread, read by the touch thread
bool FloatMenu::IsModal() { return s_bModal.load(); }

bool FloatMenu::OnTouch(int type, int finger, float x, float y)
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if(!ctx || s_bHidden) return false;
    if(s_bModal.load())   // launcher open: ⋮ / Chat are hidden, the touch goes to the launcher
    {
        if(s_nActiveBubble >= 0 && finger == s_nBubbleFinger && type != 2) { s_nActiveBubble = -1; s_nBubbleFinger = -1; return true; }
        return false;
    }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(ds.x <= 0 || ds.y <= 0) return false;
    const float pad = UI::S(12);

    if(type == 2) // push
    {
        if(s_nActiveBubble >= 0) return false;
        for(int i = (int)s_Bubbles.size() - 1; i >= 0; --i)
        {
            ImVec2 sz = BubbleSize(s_Bubbles[i]);
            ImVec2 c = BubbleCentre((size_t)i, ds);
            float px = s_Bubbles[i].bar ? ImMin(pad, BarGap() * 0.5f) : pad;
            if(fabsf(x - c.x) <= sz.x * 0.5f + px && fabsf(y - c.y) <= sz.y * 0.5f + pad)
            {
                s_nActiveBubble = i; s_nBubbleFinger = finger;
                s_fPressX = x; s_fPressY = y; s_bDragged = false;
                ImVec2 g = s_Bubbles[i].bar ? BarCentre(ds) : c;
                s_fGrabDX = g.x - x; s_fGrabDY = g.y - y;
                return true;
            }
        }
        return false;
    }
    if(finger != s_nBubbleFinger || s_nActiveBubble < 0) return false;

    int idx = s_nActiveBubble;
    if(type == 3) // move = drag the bar
    {
        float dx = x - s_fPressX, dy = y - s_fPressY;
        if(!s_bDragged && dx * dx + dy * dy > UI::S(24) * UI::S(24)) s_bDragged = true;
        if(s_bDragged && idx < (int)s_Bubbles.size())
        {
            Bubble& b = s_Bubbles[idx];
            if(b.bar)
            {
                s_BarPos.x = ImClamp((x + s_fGrabDX) / ds.x, 0.0f, 1.0f);
                s_BarPos.y = ImClamp((y + s_fGrabDY) / ds.y, 0.0f, 1.0f);
            }
            else
            {
                b.rel.x = ImClamp((x + s_fGrabDX) / ds.x, 0.05f, 0.95f);
                b.rel.y = ImClamp((y + s_fGrabDY) / ds.y, 0.04f, 0.96f);
            }
        }
        return true;
    }
    if(!s_bDragged) s_nPendingTap = idx;
    else if(idx < (int)s_Bubbles.size() && s_Bubbles[idx].bar) s_bBarMoved = true;
    s_nActiveBubble = -1; s_nBubbleFinger = -1;
    return true;
}

// TopbarPlus pill: rounded square-ish rectangle (not a circle), dark see-through; white when selected
static void DrawBubble(ImDrawList* dl, const Bubble& b, ImVec2 c, bool held)
{
    ImVec2 sz = BubbleSize(b);
    ImVec2 a(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), z(c.x + sz.x * 0.5f, c.y + sz.y * 0.5f);
    bool on = b.active && *b.active;
    int ms = b.micState ? b.micState() : -1;
    if(ms >= 2) on = true;   // 3.11: mic on = white pill like the other "on" buttons
    ImU32 bg = on ? UI::kPillActive : UI::kPill;
    if(held) bg = on ? IM_COL32(220, 220, 220, 235) : IM_COL32(40, 40, 40, 170);
    dl->AddRectFilled(a, z, bg, sz.y * 0.26f);
    ImU32 fg = on ? IM_COL32(10, 10, 10, 255) : IM_COL32_WHITE;
    if(b.micState)   // 3.11: small white mic (SA-MP mobile voice button look), slashed while off
    {
        float r = sz.y * 0.30f;
        ImU32 col = ms == 0 ? IM_COL32(255, 255, 255, 110) : fg;
        UI::DrawMic(dl, c, r, col, ms <= 1);
        if(ms == 3)   // talking: green ring around the pill
            dl->AddRect(ImVec2(a.x - UI::S(2), a.y - UI::S(2)), ImVec2(z.x + UI::S(2), z.y + UI::S(2)), IM_COL32(60, 220, 90, 255), sz.y * 0.28f, 0, UI::S(4));
        return;
    }
    if(b.dots)   // ⋮
    {
        float r = UI::S(4.2f), gap = UI::S(11);
        for(int k = -1; k <= 1; ++k) dl->AddCircleFilled(ImVec2(c.x, c.y + k * gap), r, fg, 12);
        return;
    }
    float fs = UI::S(30);
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, b.label.c_str());
    dl->AddText(UI::Font(), fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), fg, b.label.c_str());
}

// dropdown under the ⋮ pill: icon + label rows (like TopbarPlus menus)
static void DropdownIcon(ImDrawList* dl, int kind, ImVec2 c, float s, ImU32 col)
{
    switch(kind)
    {
        case 0:   // help: ring with "?"
        {
            dl->AddCircle(c, s * 0.5f, col, 20, UI::S(2.5f));
            ImVec2 ts = UI::Font()->CalcTextSizeA(s * 0.75f, FLT_MAX, 0, "?");
            dl->AddText(UI::Font(), s * 0.75f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, "?");
            break;
        }
        case 1:   // server: stacked racks
            for(int k = 0; k < 3; ++k)
            {
                float y = c.y - s * 0.45f + k * s * 0.32f;
                dl->AddRect(ImVec2(c.x - s * 0.45f, y), ImVec2(c.x + s * 0.45f, y + s * 0.24f), col, UI::S(3), 0, UI::S(2.2f));
                dl->AddCircleFilled(ImVec2(c.x + s * 0.28f, y + s * 0.12f), UI::S(2.4f), col);
            }
            break;
        default:  // players: two heads
            dl->AddCircleFilled(ImVec2(c.x - s * 0.18f, c.y - s * 0.18f), s * 0.17f, col);
            dl->AddCircleFilled(ImVec2(c.x + s * 0.22f, c.y - s * 0.12f), s * 0.14f, col);
            dl->PathArcTo(ImVec2(c.x - s * 0.18f, c.y + s * 0.42f), s * 0.32f, IM_PI, IM_PI * 2, 14); dl->PathFillConvex(col);
            dl->PathArcTo(ImVec2(c.x + s * 0.24f, c.y + s * 0.42f), s * 0.25f, IM_PI, IM_PI * 2, 14); dl->PathFillConvex(col);
            break;
    }
}

static void RenderDropdown(const ImVec2& ds)
{
    int dots = -1;
    for(size_t i = 0; i < s_Bubbles.size(); ++i) if(s_Bubbles[i].dots) { dots = (int)i; break; }
    if(dots < 0) { s_bDropdown = false; return; }
    ImVec2 c = BubbleCentre((size_t)dots, ds);
    const float rowH = UI::S(74), w = UI::S(380);
    float x = ImClamp(c.x - PillH() * 0.5f, UI::S(8), ds.x - w - UI::S(8));
    float y = c.y + PillH() * 0.5f + UI::S(10);
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, rowH * 3 + UI::S(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(10), UI::S(10)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.66f));
    if(ImGui::Begin("##MPDropdown", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        static const char* names[3] = { "Help", "Server", "Player list" };
        static const int   pages[3] = { FloatMenu::PAGE_HELP, FloatMenu::PAGE_SERVER, FloatMenu::PAGE_PLAYERS };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for(int k = 0; k < 3; ++k)
        {
            ImGui::PushID(k);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float rw = ImGui::GetContentRegionAvail().x;
            bool hit = ImGui::InvisibleButton("##dd", ImVec2(rw, rowH));
            bool open = s_Pages[pages[k]].open;
            if(ImGui::IsItemActive() || open) dl->AddRectFilled(p, ImVec2(p.x + rw, p.y + rowH), open ? IM_COL32(255, 255, 255, 235) : IM_COL32(255, 255, 255, 40), UI::S(12));
            ImU32 fg = open ? IM_COL32(10, 10, 10, 255) : IM_COL32_WHITE;
            DropdownIcon(dl, k, ImVec2(p.x + UI::S(38), p.y + rowH * 0.5f), UI::S(34), fg);
            float fs = UI::S(30);
            dl->AddText(UI::Font(), fs, ImVec2(p.x + UI::S(76), p.y + (rowH - fs) * 0.5f), fg, names[k]);
            if(hit) { if(open) FloatMenu::ClosePage(pages[k]); else FloatMenu::OpenPage(pages[k]); s_bDropdown = false; }
            ImGui::PopID();
        }
        // tap anywhere else = close
        if(ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::GetFrameCount() > (int)s_nDropdownFrame + 1)
            s_bDropdown = false;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

static MPMenuSectionFn s_fnServerPage = nullptr;   // 2.25 launcher
void FloatMenu::SetServerPageRenderer(MPMenuSectionFn fn) { s_fnServerPage = fn; }
int  FloatMenu::SectionCount() { return (int)s_Sections.size(); }
const char* FloatMenu::SectionTitle(int i) { return (i >= 0 && i < (int)s_Sections.size()) ? s_Sections[i].title.c_str() : ""; }
void FloatMenu::DrawSection(int i) { if(i >= 0 && i < (int)s_Sections.size() && s_Sections[i].fn) s_Sections[i].fn(s_Sections[i].data); }

static void RenderServerPage(const ImVec2& ds)
{
    Page& pg = s_Pages[FloatMenu::PAGE_SERVER];
    if(s_fnServerPage) { s_fnServerPage(&pg.open); return; }
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.27f, ds.y * 0.13f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.46f, ds.y * 0.80f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(UI::S(500), UI::S(300)), ds);
    if(ImGui::Begin(pg.title, &pg.open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        for(size_t i = 0; i < s_Sections.size(); ++i)
        {
            Section& s = s_Sections[i];
            ImGui::PushID((int)i);
            if(i == 0) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if(ImGui::CollapsingHeader(s.title.c_str()))
            {
                ImGui::Indent(UI::S(6));
                if(s.fn) s.fn(s.data);
                ImGui::Unindent(UI::S(6));
                ImGui::Spacing();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void FloatMenu::Render()
{
    const bool modal = s_fnServerPage && s_Pages[PAGE_SERVER].open;   // 2.26
    s_bModal.store(modal);
    if(modal) { s_bDropdown = false; s_nPendingTap.store(-1); }
    for(auto& w : s_Windows) if(w.fn) w.fn(w.data);

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(s_Pages[PAGE_SERVER].open) RenderServerPage(ds);
    if(modal && !s_Pages[PAGE_SERVER].open) s_bModal.store(false);   // closed with X this frame
    for(int k = PAGE_PLAYERS; k < PAGE_COUNT; ++k)
        if(s_Pages[k].open && s_Pages[k].fn) s_Pages[k].fn(&s_Pages[k].open);   // the page draws its own window
    if(s_bDropdown && !s_bHidden) RenderDropdown(ds);

    // run a queued pill tap here (render thread) so game/ImGui calls stay on one thread
    int tap = s_nPendingTap.exchange(-1);
    if(tap >= 0 && tap < (int)s_Bubbles.size())
    {
        if(s_Bubbles[tap].dots) { s_bDropdown = !s_bDropdown; s_nDropdownFrame = ImGui::GetFrameCount(); }
        else if(s_Bubbles[tap].fn) s_Bubbles[tap].fn(s_Bubbles[tap].data);
    }
    s_bDotsActive = s_bDropdown;
    if(s_bHidden || s_bModal.load()) return;   // 2.26: no ⋮ / Chat over the launcher (it has its own X)

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    int held = s_nActiveBubble;
    for(size_t i = 0; i < s_Bubbles.size(); ++i)
        DrawBubble(fg, s_Bubbles[i], BubbleCentre(i, ds), (int)i == held);
}

// pill rectangle (for things placed under the bar, e.g. the chat box)
bool FloatMenu::GetBarRect(float* x0, float* y0, float* x1, float* y1)
{
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(BarWidth() <= 0) return false;
    ImVec2 c = BarCentre(ds);
    *x0 = c.x - BarWidth() * 0.5f; *x1 = c.x + BarWidth() * 0.5f;
    *y0 = c.y - PillH() * 0.5f;    *y1 = c.y + PillH() * 0.5f;
    return true;
}

// ---- exported interface -------------------------------------------------------------
class MPMenuImpl : public IMPMenu
{
public:
    int   GetVersion() override { return 2; }
    void* GetImGuiContext() override { return ImGui::GetCurrentContext(); }
    void  AddSection(const char* t, MPMenuSectionFn fn, void* d) override { FloatMenu::AddSection(t, fn, d); }
    void  AddFloatingWindow(MPMenuWindowFn fn, void* d) override { FloatMenu::AddFloatingWindow(fn, d); }
    void  AddBubble(const char* l, float x, float y, MPMenuSectionFn fn, void* d) override { FloatMenu::AddBubble(l, x, y, fn, d); }
    void  OpenKeyboard(const char* t, const char* i, int m, void (*fn)(const char*, void*), void* d) override { VKeyboard::Open(t, i, m, fn, d); }
    void  AddChatMessage(uint32_t c, const char* t) override { MP::AddSystemMessage(c, t); }
    bool  AddResource(const char* n) override { return MP::AddResource(n); }
    bool  IsConnected() override { return MP::IsConnected(); }
    float Scale(float px) override { return UI::S(px); }
};
static MPMenuImpl s_Iface;
IMPMenu* FloatMenu::GetInterface() { return &s_Iface; }
