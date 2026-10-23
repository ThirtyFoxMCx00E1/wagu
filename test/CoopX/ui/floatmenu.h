


#pragma once
#include "../include/mp/imp_menu.h"

namespace FloatMenu
{
    void ApplyStyle();
    void AddSection(const char* title, MPMenuSectionFn fn, void* data = nullptr);
    void AddFloatingWindow(MPMenuWindowFn fn, void* data = nullptr);
    void AddBubble(const char* label, float relX, float relY, MPMenuSectionFn onTap, void* data = nullptr);


    bool OnTouch(int type, int finger, float x, float y);

    void Toggle();
    bool IsOpen();
    void Render();


    bool BigButton(const char* label, unsigned int colorABGR = 0);
    bool ToggleSwitch(const char* label, bool* v);
    bool TextField(const char* label, const char* value, const char* kbTitle, int maxLen, void (*onSubmit)(const char*, void*), void* data = nullptr, bool numeric = false);

    IMPMenu* GetInterface();
}
