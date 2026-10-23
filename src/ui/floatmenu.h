// Floating ImGui menu framework: draggable bubble buttons + floating windows with sections.
// Developers can add their own sections/windows (in-source via FloatMenu::*, or from another
// AML mod through the "MPMenu" interface, see include/mp/imp_menu.h).
#pragma once
#include "../../include/mp/imp_menu.h"

namespace FloatMenu
{
    void ApplyStyle();
    void AddSection(const char* title, MPMenuSectionFn fn, void* data = nullptr);
    void AddFloatingWindow(MPMenuWindowFn fn, void* data = nullptr);
    void AddBubble(const char* label, float relX, float relY, MPMenuSectionFn onTap, void* data = nullptr);
    // 2.18.3: MP's own floating buttons (MP MENU, CHAT) sit side by side in one tidy bar with a
    // fixed gap and a divider; dragging any of them moves the whole bar.
    void AddBarButton(const char* label, MPMenuSectionFn onTap, void* data = nullptr, const bool* active = nullptr);
    // 2.19 TopbarPlus: the ⋮ pill (dropdown: Help / Server / Player list) and its pages
    enum { PAGE_SERVER = 0, PAGE_PLAYERS, PAGE_HELP, PAGE_COUNT };
    void AddDotsButton();
    bool BarBottom(float* centreX, float* y);   // 3.11: just under the top bar (false = bar hidden)
    void AddMicButton(MPMenuSectionFn onTap, int (*state)());   // 3.11: voice chat pill after [Chat]
    void SetPage(int page, MPMenuSectionFn fn, void* data = nullptr);   // fn gets bool* open, draws its own window
    void OpenPage(int page);
    void ClosePage(int page);
    bool IsPageOpen(int page);
    void CloseAll();
    void SetHidden(bool hidden);       // join screen: no top bar
    bool GetBarRect(float* x0, float* y0, float* x1, float* y1);
    void SetBarPos(float relX, float relY);
    bool GetBarPos(float* relX, float* relY);   // true once if it was dragged since the last call (save it)

    // Raw touch input (type: 1 release, 2 push, 3 move, 4 pointer-up). Returns true if a bubble used it.
    bool OnTouch(int type, int finger, float x, float y);

    void Toggle();
    bool IsOpen();
    bool IsModal();   // 2.26: full-screen launcher open -> it gets every touch, floating buttons hidden
    void Render();        // inside an ImGui frame

    // Helpers for section authors
    bool BigButton(const char* label, unsigned int colorABGR = 0);
    bool ToggleSwitch(const char* label, bool* v);
    bool TextField(const char* label, const char* value, const char* kbTitle, int maxLen, void (*onSubmit)(const char*, void*), void* data = nullptr, bool numeric = false, bool masked = false);

    IMPMenu* GetInterface();

    // 2.25: the Server page is drawn by MP's launcher (fn gets bool* open). Sections (MP's own and
    // the ones other mods add) are listed in its sidebar.
    void SetServerPageRenderer(MPMenuSectionFn fn);
    int  SectionCount();
    const char* SectionTitle(int i);
    void DrawSection(int i);
}
