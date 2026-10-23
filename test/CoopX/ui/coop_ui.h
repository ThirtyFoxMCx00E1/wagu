#pragma once
#include <cstdint>
#include "../transport/coop_client.h"

namespace CoopUI {
struct Actions {
    void (*toggleConnection)() = nullptr;
    void (*setHosting)(bool) = nullptr;
    void (*setConnectionSettings)(const char* host, int port, const char* nickname) = nullptr;
    void (*setNameTags)(bool) = nullptr;
    void (*setHealthBars)(bool) = nullptr;
    bool (*isHosting)() = nullptr;
    bool (*nameTagsVisible)() = nullptr;
    bool (*healthBarsVisible)() = nullptr;
};

bool Install(uintptr_t gameLibrary, void* gameHandle, CoopClient* client,
             const char* host, int port, const char* nickname, const Actions& actions);
void Render();
void Shutdown();
bool OnTouch(int type, int finger, int x, int y);
bool IsReady();
void AddSystemChat(uint32_t rgba, const char* text);
}
