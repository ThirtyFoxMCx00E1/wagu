#pragma once

#include <cstdint>

#include "../transport/coop_client.h"

struct ImDrawList;
#include <isautils.h>

void InstallGameHooks(uintptr_t gameLibrary, CoopClient& client, ISAUtils* saUtils);
void SetRemoteNameTagsVisible(bool visible);
void SetRemoteNameTagScale(float scale);
void SetRemoteHealthBarsVisible(bool visible);
void DrawRemoteWeaponIcons();
void RenderRemotePlayerNameTags(ImDrawList* drawList);
