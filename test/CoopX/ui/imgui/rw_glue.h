#pragma once
// Glue header that replaces AML_ImGui's main.h for the embedded ImGui build inside libMP.
#include <stdint.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "RW/RenderWare.h"

extern uintptr_t pGameLib;
extern void* pGameHandle;
extern RwReal* nearScreenZ;
extern RwReal* recipNearClip;
extern void (*SetScissorRect)(float*);
