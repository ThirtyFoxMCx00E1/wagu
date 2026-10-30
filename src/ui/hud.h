#pragma once
struct ImVec2;
namespace HUD
{
    bool Init();
    bool IsInitialised();
    void Render();                     // from Render2dStuff hook
    bool WantsTouch(float x, float y); // should this touch be eaten by MP UI?
    // 2.24: server icon texture (0 = default main_df_icon.png, 1..250 = icon_N.png); nullptr if no file
    void* ServerIcon(int id, ImVec2* uv);   // ImTextureID
    void ReloadServerIcons();
    void* Texture(const char* fileInDataDir, ImVec2* uv);   // 3.04: e.g. "bcw_briefcase.png" (nullptr if missing)
    void  ReloadTexture(const char* fileInDataDir);          // 3.24: the file changed (Discord avatar): load it again
    // 3.34: a small extra ImGui frame drawn inside the pause-menu map (Game::MenuMapLayerFn)
    bool  RenderMenuMapLayer(int layer, const void* labels /* Game::MapLabel */, int n);
}
