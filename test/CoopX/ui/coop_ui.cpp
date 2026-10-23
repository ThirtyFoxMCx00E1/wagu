#include <mod/amlmod.h>
#include <mod/logger.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "coop_ui.h"
#include "../world/game_hooks.h"
#include "floatmenu.h"
#include "vkeyboard.h"
#include "ui_common.h"
#include "imgui/rw_glue.h"
#include "imgui/arial.h"
#include "imgui/RW/RenderWare.h"
#include "imgui/imgui.h"

uintptr_t pGameLib = 0;
void* pGameHandle = nullptr;
RwReal* nearScreenZ = nullptr;
RwReal* recipNearClip = nullptr;
void (*SetScissorRect)(float*) = nullptr;
void InitRenderWareFunctions();

void ImGui_ImplRenderWare_RenderDrawData(ImDrawData* draw_data);
bool ImGui_ImplRenderWare_Init();
void ImGui_ImplRenderWare_NewFrame();
void ImGui_ImplRenderWare_ShutDown();

namespace {
    CoopClient* s_client = nullptr;
    CoopUI::Actions s_actions{};
    bool s_ready = false;
    bool s_rwReady = false;
    bool s_rwInitialized = false;
    bool s_touchHooked = false;
    bool s_chatOpen = false;
    float s_scale = 1.0f;
    ImFont* s_font = nullptr;
    char s_host[96] = "127.0.0.1";
    char s_port[8] = "6767";
    char s_name[16] = "CJ";
    char s_chatText[161]{};
    std::deque<std::string> s_chatLines;
    bool s_mouseDown = false;
    int s_mouseFinger = -1;
    bool s_ignored[16]{};
    enum Field { FIELD_NAME, FIELD_HOST, FIELD_PORT, FIELD_CHAT };

    constexpr int TOUCH_RELEASE = 1;
    constexpr int TOUCH_PUSH = 2;
    constexpr int TOUCH_MOVE = 3;
    constexpr int TOUCH_POINTER_UP = 4;

    void PushChatLine(const std::string& line) {
        if (line.empty()) return;
        if (s_chatLines.size() >= 10) s_chatLines.pop_front();
        s_chatLines.push_back(line);
    }

    void ChatSubmit(const char* text, void*) {
        if (!text || !*text) return;
        if (s_client && s_client->IsConnected()) {
            s_client->SendChat(text);
        } else {
            PushChatLine("SERVER: Connect before sending chat");
        }
        s_chatText[0] = '\0';
    }

    void ApplyField(const char* text, void* userData) {
        if (!text) return;
        const Field field = static_cast<Field>(reinterpret_cast<uintptr_t>(userData));
        switch (field) {
        case FIELD_NAME:
            std::snprintf(s_name, sizeof(s_name), "%s", text);
            if (!s_name[0]) std::snprintf(s_name, sizeof(s_name), "CJ");
            break;
        case FIELD_HOST:
            std::snprintf(s_host, sizeof(s_host), "%s", text);
            if (!s_host[0]) std::snprintf(s_host, sizeof(s_host), "127.0.0.1");
            break;
        case FIELD_PORT:
            std::snprintf(s_port, sizeof(s_port), "%s", text);
            break;
        default: break;
        }
        if (s_actions.setConnectionSettings) {
            int port = std::atoi(s_port);
            if (port < 1) port = 6767;
            if (port > 65535) port = 65535;
            std::snprintf(s_port, sizeof(s_port), "%d", port);
            s_actions.setConnectionSettings(s_host, port, s_name);
        }
    }

    void TapMenu(void*) { FloatMenu::Toggle(); }
    void TapChat(void*) { s_chatOpen = !s_chatOpen; }

    void DrawConnectionSection(void*) {
        if (s_client && s_client->IsConnected()) {
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "CONNECTED  |  ID %u", s_client->LocalId());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.34f, 1.0f), "DISCONNECTED");
        }
        FloatMenu::TextField("Nickname", s_name, "Nickname", 15, ApplyField, reinterpret_cast<void*>(FIELD_NAME));
        FloatMenu::TextField("Server IP", s_host, "Server IP", 95, ApplyField, reinterpret_cast<void*>(FIELD_HOST));
        FloatMenu::TextField("Server port", s_port, "Server port", 6, ApplyField, reinterpret_cast<void*>(FIELD_PORT), true);
        ImGui::Spacing();
        if (FloatMenu::BigButton(s_client && s_client->IsConnected() ? "DISCONNECT" : "CONNECT", UI::kAccentDark)) {
            if (s_actions.setConnectionSettings) {
                int port = std::atoi(s_port); if (port < 1 || port > 65535) port = 6767;
                s_actions.setConnectionSettings(s_host, port, s_name);
            }
            if (s_actions.toggleConnection) s_actions.toggleConnection();
        }
        bool hosting = s_actions.isHosting ? s_actions.isHosting() : false;
        if (FloatMenu::ToggleSwitch("Host LAN Server", &hosting) && s_actions.setHosting)
            s_actions.setHosting(hosting);
        bool tags = s_actions.nameTagsVisible ? s_actions.nameTagsVisible() : true;
        if (FloatMenu::ToggleSwitch("Player name tags", &tags) && s_actions.setNameTags)
            s_actions.setNameTags(tags);
        bool bars = s_actions.healthBarsVisible ? s_actions.healthBarsVisible() : true;
        if (FloatMenu::ToggleSwitch("Health / armour bars", &bars) && s_actions.setHealthBars)
            s_actions.setHealthBars(bars);
    }

    void DrawChatWindow(void*) {
        if (!s_chatOpen) return;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(UI::S(22), display.y * 0.26f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(display.x * 0.55f, UI::S(620)), display.y * 0.36f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("MP CHAT##CoopFusionChat", &s_chatOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::BeginChild("##chatLog", ImVec2(0, -UI::S(88)), true);
            for (const auto& line : s_chatLines) {
                UI::OutlinedText(ImGui::GetWindowDrawList(), UI::S(25), ImGui::GetCursorScreenPos(), IM_COL32_WHITE, line.c_str());
                ImGui::Dummy(ImVec2(0, UI::S(28)));
            }
            ImGui::EndChild();
            ImGui::SetNextItemWidth(-UI::S(130));
            if (ImGui::Button("TYPE MESSAGE", ImVec2(-UI::S(130), UI::S(62))))
                VKeyboard::Open("Chat", s_chatText, 160, ChatSubmit, nullptr);
            ImGui::SameLine();
            if (ImGui::Button("SEND", ImVec2(UI::S(110), UI::S(62)))) ChatSubmit(s_chatText, nullptr);
        }
        ImGui::End();
    }

    bool HookTouchEventsInternal();

    bool TouchInsideUI(float x, float y) {
        if (!s_ready || !ImGui::GetCurrentContext()) return false;
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        if (VKeyboard::IsOpen() && y >= d.y * 0.47f) return true;
        if (s_chatOpen && x <= std::min(d.x * 0.62f, UI::S(680)) && y >= d.y * 0.22f && y <= d.y * 0.72f) return true;
        if (FloatMenu::IsOpen() && x >= d.x * 0.22f && x <= d.x * 0.80f && y >= d.y * 0.05f && y <= d.y * 0.96f) return true;
        return false;
    }

    bool EnsureUiInitialized() {
        if (s_ready) return true;
        if (!pGameLib || !pGameHandle) return false;
        if (!s_rwInitialized) {
            InitRenderWareFunctions();
            s_rwInitialized = true;
        }
        s_rwReady = RsGlobal && RsGlobal->maximumWidth > 0 && RsGlobal->maximumHeight > 0 && nearScreenZ && recipNearClip;
        if (!s_rwReady) return false;
        s_scale = static_cast<float>(RsGlobal->maximumHeight) / 1080.0f;
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
        ImGui_ImplRenderWare_Init();
        static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0400, 0x045F, 0x2010, 0x2040, 0 };
        ImFontConfig fontConfig{};
        fontConfig.FontDataOwnedByAtlas = false;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        s_font = io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), UI::S(34.0f), &fontConfig, ranges);
        if (!s_font) s_font = io.Fonts->AddFontDefault();
        io.FontDefault = s_font;
        FloatMenu::ApplyStyle();
        FloatMenu::AddSection("Connection", DrawConnectionSection, nullptr);
        FloatMenu::AddFloatingWindow(DrawChatWindow, nullptr);
        FloatMenu::AddBubble("MP MENU", 0.18f, 0.84f, TapMenu, nullptr);
        FloatMenu::AddBubble("CHAT", 0.18f, 0.93f, TapChat, nullptr);
        RegisterInterface("MPMenu", FloatMenu::GetInterface());
        s_ready = true;
        logger->Info("CoopFusion32 UI initialized (%dx%d)", RsGlobal->maximumWidth, RsGlobal->maximumHeight);
        return true;
    }
}

float UI::S(float px) { return px * s_scale; }
ImFont* UI::Font() { return s_font ? s_font : ImGui::GetFont(); }
void UI::OutlinedText(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* text, float alphaMul) {
    if (!dl || !text) return;
    ImFont* font = UI::Font();
    const float o = std::max(1.0f, size * 0.055f);
    const ImU32 shadow = IM_COL32(0, 0, 0, static_cast<int>(255.0f * alphaMul));
    dl->AddText(font, size, ImVec2(p.x - o, p.y), shadow, text);
    dl->AddText(font, size, ImVec2(p.x + o, p.y), shadow, text);
    dl->AddText(font, size, ImVec2(p.x, p.y - o), shadow, text);
    dl->AddText(font, size, ImVec2(p.x, p.y + o), shadow, text);
    dl->AddText(font, size, p, col, text);
}

namespace MP {
    bool IsConnected() { return s_client && s_client->IsConnected(); }
    void AddSystemMessage(uint32_t rgba, const char* text) {
        (void)rgba;
        if (text) CoopUI::AddSystemChat(rgba, text);
    }
    bool AddResource(const char*) { return false; }
}

namespace CoopUI {
bool Install(uintptr_t gameLibrary, void* gameHandle, CoopClient* client,
             const char* host, int port, const char* nickname, const Actions& actions) {
    pGameLib = gameLibrary;
    pGameHandle = gameHandle;
    s_client = client;
    s_actions = actions;
    std::snprintf(s_host, sizeof(s_host), "%s", host && *host ? host : "127.0.0.1");
    std::snprintf(s_port, sizeof(s_port), "%d", port > 0 && port <= 65535 ? port : 6767);
    std::snprintf(s_name, sizeof(s_name), "%s", nickname && *nickname ? nickname : "CJ");
    if (!pGameLib || !pGameHandle) {
        logger->Error("CoopFusion UI: GTA library/handle unavailable; ImGui layer disabled");
        return false;
    }
    nearScreenZ = reinterpret_cast<RwReal*>(aml->GetSym(pGameHandle, "_ZN9CSprite2d11NearScreenZE"));
    recipNearClip = reinterpret_cast<RwReal*>(aml->GetSym(pGameHandle, "_ZN9CSprite2d13RecipNearClipE"));
    SetScissorRect = reinterpret_cast<void (*)(float*)>(aml->GetSym(pGameHandle, "_ZN7CWidget10SetScissorER5CRect"));
    InitRenderWareFunctions();
    s_rwInitialized = true;


    if (!s_touchHooked) s_touchHooked = HookTouchEventsInternal();
    EnsureUiInitialized();
    return true;
}

void Render() {
    if (!s_ready) EnsureUiInitialized();
    if (!s_ready || !s_rwReady || !ImGui::GetCurrentContext()) return;
    if (!ImGui_ImplRenderWare_Init()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)RsGlobal->maximumWidth, (float)RsGlobal->maximumHeight);
    ImGui_ImplRenderWare_NewFrame();
    ImGui::NewFrame();
    RenderRemotePlayerNameTags(ImGui::GetBackgroundDrawList());
    if (s_client) {
        for (const RemoteChatMessage& msg : s_client->DrainChatMessages()) {
            char line[200];
            std::snprintf(line, sizeof(line), "%s: %s", msg.from, msg.text);
            PushChatLine(line);
        }
    }
    FloatMenu::Render();
    VKeyboard::Render();
    ImGui::Render();
    ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());
}

bool OnTouch(int type, int finger, int x, int y) {
    if (!s_ready || !ImGui::GetCurrentContext() || finger < 0 || finger >= 16) return false;
    ImGuiIO& io = ImGui::GetIO();
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    const bool release = type == TOUCH_RELEASE || type == TOUCH_POINTER_UP;
    if (FloatMenu::OnTouch(type, finger, fx, fy)) {
        s_ignored[finger] = !release;
        if (release && finger == s_mouseFinger) {
            io.AddMouseButtonEvent(0, false);
            s_mouseDown = false;
            s_mouseFinger = -1;
        }
        return true;
    }
    if (type == TOUCH_PUSH) {
        if (TouchInsideUI(fx, fy) && !s_mouseDown) {
            io.AddMousePosEvent(fx, fy);
            io.AddMouseButtonEvent(0, true);
            s_mouseDown = true;
            s_mouseFinger = finger;
            s_ignored[finger] = true;
            return true;
        }
    } else if (type == TOUCH_MOVE) {
        if (finger == s_mouseFinger) {
            io.AddMousePosEvent(fx, fy);
            return true;
        }
        if (s_ignored[finger]) return true;
    } else if (release) {
        if (finger == s_mouseFinger) {
            io.AddMousePosEvent(fx, fy);
            io.AddMouseButtonEvent(0, false);
            s_mouseDown = false;
            s_mouseFinger = -1;
            s_ignored[finger] = false;
            return true;
        }
        if (s_ignored[finger]) { s_ignored[finger] = false; return true; }
    }
    return false;
}

void Shutdown() {
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplRenderWare_ShutDown();
        ImGui::DestroyContext();
    }
    s_ready = false;
    s_rwReady = false;
    s_rwInitialized = false;
    s_touchHooked = false;
    s_font = nullptr;
}

bool IsReady() { return s_ready; }
void AddSystemChat(uint32_t rgba, const char* text) {
    (void)rgba;
    if (text) PushChatLine(std::string("SERVER: ") + text);
}
}

DECL_HOOKv(CoFusionTouchEvent, int type, int finger, int x, int y) {
    if (CoopUI::OnTouch(type, finger, x, y)) return;
    CoFusionTouchEvent(type, finger, x, y);
}

namespace {
bool HookTouchEventsInternal() {
    if (!pGameLib || !pGameHandle) return false;
#ifdef AML32
    if (*(uint32_t*)(pGameLib + 0x202020) == 0xE8BDB001)
        HOOKPLT(CoFusionTouchEvent, pGameLib + 0x675DE4);
    else
        HOOKSYM(CoFusionTouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#else
    HOOKSYM(CoFusionTouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#endif
    return true;
}
}
