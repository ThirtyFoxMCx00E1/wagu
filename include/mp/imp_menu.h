// Public API of libMP's floating ImGui menu for other AML mods.
//
// Usage from your own AML mod (build it against the same Dear ImGui version, 1.89.7):
//
//   #include <mp/imp_menu.h>
//   IMPMenu* mpMenu = nullptr;
//   static void DrawMySection(void*) { ImGui::Text("Hello from my mod"); }
//   extern "C" void OnAllModsLoaded()
//   {
//       mpMenu = (IMPMenu*)GetInterface("MPMenu");
//       if(mpMenu) mpMenu->AddSection("My Mod", DrawMySection, nullptr);
//   }
//
// Before calling ImGui functions from your section callback libMP has already made its
// context current; if you link your own copy of ImGui call
// ImGui::SetCurrentContext((ImGuiContext*)mpMenu->GetImGuiContext()) once.
#pragma once
#include <stdint.h>

typedef void (*MPMenuSectionFn)(void* userData);
typedef void (*MPMenuWindowFn)(void* userData);

class IMPMenu
{
public:
    virtual int   GetVersion() = 0;                                   // 2
    virtual void* GetImGuiContext() = 0;                              // ImGuiContext*
    // A collapsible section inside the main "MP" floating menu
    virtual void  AddSection(const char* title, MPMenuSectionFn fn, void* userData = nullptr) = 0;
    // Your own floating window. Called every frame inside an ImGui frame (draw your Begin/End).
    virtual void  AddFloatingWindow(MPMenuWindowFn fn, void* userData = nullptr) = 0;
    // A round draggable bubble button on screen; onTap is called when it's tapped.
    virtual void  AddBubble(const char* label, float x, float y, MPMenuSectionFn onTap, void* userData = nullptr) = 0;
    virtual void  OpenKeyboard(const char* title, const char* initial, int maxLen, void (*onSubmit)(const char*, void*), void* userData = nullptr) = 0;
    virtual void  AddChatMessage(uint32_t rgba, const char* text) = 0;
    virtual bool  IsConnected() = 0;
    virtual float Scale(float px1080) = 0;                            // UI scale helper
    // Version 2+: add a server resource, listed in the chat when joining (": name resources").
    // 90 are built in; up to 32 more. Accepts "server_example" or ": server_example".
    virtual bool  AddResource(const char* name) = 0;
};
