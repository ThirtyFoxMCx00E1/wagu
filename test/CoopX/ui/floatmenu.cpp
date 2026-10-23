#include "floatmenu.h"
#include "coop_ui.h"
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
    struct Bubble   { std::string label; ImVec2 rel; MPMenuSectionFn fn; void* data; };

    std::vector<Section> s_Sections;
    std::vector<Window>  s_Windows;
    std::vector<Bubble>  s_Bubbles;
    bool s_bOpen = false;
}

void FloatMenu::ApplyStyle()
{
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding    = UI::S(14);
    st.ChildRounding     = UI::S(10);
    st.FrameRounding     = UI::S(10);
    st.GrabRounding      = UI::S(10);
    st.PopupRounding     = UI::S(10);
    st.ScrollbarRounding = UI::S(10);
    st.WindowBorderSize  = UI::S(2);
    st.FrameBorderSize   = 0;
    st.WindowPadding     = ImVec2(UI::S(18), UI::S(16));
    st.FramePadding      = ImVec2(UI::S(16), UI::S(12));
    st.ItemSpacing       = ImVec2(UI::S(12), UI::S(12));
    st.ItemInnerSpacing  = ImVec2(UI::S(10), UI::S(8));
    st.ScrollbarSize     = UI::S(34);
    st.GrabMinSize       = UI::S(34);
    st.TouchExtraPadding = ImVec2(UI::S(4), UI::S(4));
    st.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

    ImVec4* c = st.Colors;
    ImVec4 accent    = ImGui::ColorConvertU32ToFloat4(UI::kAccent);
    ImVec4 accentDk  = ImGui::ColorConvertU32ToFloat4(UI::kAccentDark);
    c[ImGuiCol_WindowBg]         = ImVec4(0.06f, 0.07f, 0.06f, 0.94f);
    c[ImGuiCol_ChildBg]          = ImVec4(0.09f, 0.10f, 0.09f, 0.90f);
    c[ImGuiCol_Border]           = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_TitleBg]          = accentDk;
    c[ImGuiCol_TitleBgActive]    = accentDk;
    c[ImGuiCol_TitleBgCollapsed] = accentDk;
    c[ImGuiCol_FrameBg]          = ImVec4(0.16f, 0.18f, 0.16f, 1.0f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.24f, 0.20f, 1.0f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.22f, 0.30f, 0.22f, 1.0f);
    c[ImGuiCol_Button]           = ImVec4(0.18f, 0.21f, 0.18f, 1.0f);
    c[ImGuiCol_ButtonHovered]    = accentDk;
    c[ImGuiCol_ButtonActive]     = accent;
    c[ImGuiCol_Header]           = ImVec4(0.14f, 0.30f, 0.17f, 1.0f);
    c[ImGuiCol_HeaderHovered]    = accentDk;
    c[ImGuiCol_HeaderActive]     = accent;
    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.4f, 0.9f, 0.5f, 1.0f);
    c[ImGuiCol_Separator]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
}

void FloatMenu::AddSection(const char* t, MPMenuSectionFn fn, void* d) { s_Sections.push_back({ t ? t : "Section", fn, d }); }
void FloatMenu::AddFloatingWindow(MPMenuWindowFn fn, void* d)          { s_Windows.push_back({ fn, d }); }
void FloatMenu::AddBubble(const char* l, float x, float y, MPMenuSectionFn fn, void* d) { s_Bubbles.push_back({ l ? l : "?", ImVec2(x, y), fn, d }); }
void FloatMenu::Toggle() { s_bOpen = !s_bOpen; }
bool FloatMenu::IsOpen() { return s_bOpen; }

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
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), *v ? UI::kAccent : IM_COL32(70, 70, 70, 255), h * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + t * (w - h), p.y + h * 0.5f), h * 0.5f - UI::S(4), IM_COL32_WHITE);
    ImGui::PopID();
    return clicked;
}

bool FloatMenu::TextField(const char* label, const char* value, const char* kbTitle, int maxLen, void (*onSubmit)(const char*, void*), void* data, bool numeric)
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetContentRegionMax().x * 0.40f);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s##tf", value);
    bool r = ImGui::Button(buf, ImVec2(-FLT_MIN, 0));
    if(r) VKeyboard::Open(kbTitle, value, maxLen, onSubmit, data, numeric);
    ImGui::PopID();
    return r;
}


static std::atomic<int> s_nActiveBubble(-1);
static std::atomic<int> s_nPendingTap(-1);
static int   s_nBubbleFinger = -1;
static float s_fPressX = 0, s_fPressY = 0;
static bool  s_bDragged = false;
static float s_fGrabDX = 0, s_fGrabDY = 0;


static ImVec2 BubbleSize(const Bubble& b)
{
    float fs = UI::S(30);
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, b.label.c_str());
    return ImVec2(ts.x + UI::S(56), UI::S(68));
}

bool FloatMenu::OnTouch(int type, int finger, float x, float y)
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if(!ctx) return false;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(ds.x <= 0 || ds.y <= 0) return false;
    const float pad = UI::S(18);

    if(type == 2)
    {
        if(s_nActiveBubble >= 0) return false;
        for(int i = (int)s_Bubbles.size() - 1; i >= 0; --i)
        {
            ImVec2 sz = BubbleSize(s_Bubbles[i]);
            float cx = s_Bubbles[i].rel.x * ds.x, cy = s_Bubbles[i].rel.y * ds.y;
            if(fabsf(x - cx) <= sz.x * 0.5f + pad && fabsf(y - cy) <= sz.y * 0.5f + pad)
            {
                s_nActiveBubble = i; s_nBubbleFinger = finger;
                s_fPressX = x; s_fPressY = y; s_bDragged = false;
                s_fGrabDX = cx - x; s_fGrabDY = cy - y;
                return true;
            }
        }
        return false;
    }
    if(finger != s_nBubbleFinger || s_nActiveBubble < 0) return false;

    int idx = s_nActiveBubble;
    if(type == 3)
    {
        float dx = x - s_fPressX, dy = y - s_fPressY;
        if(!s_bDragged && dx * dx + dy * dy > UI::S(24) * UI::S(24)) s_bDragged = true;
        if(s_bDragged && idx < (int)s_Bubbles.size())
        {
            Bubble& b = s_Bubbles[idx];
            b.rel.x = ImClamp((x + s_fGrabDX) / ds.x, 0.05f, 0.95f);
            b.rel.y = ImClamp((y + s_fGrabDY) / ds.y, 0.04f, 0.96f);
        }
        return true;
    }

    if(!s_bDragged) s_nPendingTap = idx;
    s_nActiveBubble = -1; s_nBubbleFinger = -1;
    return true;
}



static void DrawBubble(ImDrawList* dl, const Bubble& b, bool held)
{
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImVec2 c(b.rel.x * ds.x, b.rel.y * ds.y);
    float fs = UI::S(30);
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, b.label.c_str());
    ImVec2 tp(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
    const ImU32 shadow = IM_COL32(0, 0, 0, 220);
    const float o = UI::S(2);
    dl->AddText(UI::Font(), fs, ImVec2(tp.x - o, tp.y), shadow, b.label.c_str());
    dl->AddText(UI::Font(), fs, ImVec2(tp.x + o, tp.y), shadow, b.label.c_str());
    dl->AddText(UI::Font(), fs, ImVec2(tp.x, tp.y - o), shadow, b.label.c_str());
    dl->AddText(UI::Font(), fs, ImVec2(tp.x, tp.y + o), shadow, b.label.c_str());
    dl->AddText(UI::Font(), fs, tp, held ? IM_COL32(255, 255, 255, 150) : IM_COL32_WHITE, b.label.c_str());
}

void FloatMenu::Render()
{
    for(auto& w : s_Windows) if(w.fn) w.fn(w.data);

    if(s_bOpen)
    {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(ds.x * 0.28f, ds.y * 0.08f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(ds.x * 0.44f, ds.y * 0.80f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(UI::S(500), UI::S(300)), ds);
        if(ImGui::Begin("MP  -  Multiplayer##MPMain", &s_bOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
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


    int tap = s_nPendingTap.exchange(-1);
    if(tap >= 0 && tap < (int)s_Bubbles.size() && s_Bubbles[tap].fn) s_Bubbles[tap].fn(s_Bubbles[tap].data);

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    int held = s_nActiveBubble;
    for(size_t i = 0; i < s_Bubbles.size(); ++i) DrawBubble(fg, s_Bubbles[i], (int)i == held);
}


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
