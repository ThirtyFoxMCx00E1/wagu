// MP 4.13.13: see lgball.h.
//
// How: the game loads 1946 from gta3.img as usual (REQUEST_MODEL). Then CAtomicModelInfo::DeleteRwObject drops
// GTA's atomic and CFileLoader::LoadAtomicFile reads ours from a memory stream (the same call the streamer
// makes, so the model info, txd refs and render callbacks are set up the game's way). The shell material gets
// our texture (decoded from the embedded JPEG, kept referenced by us forever). Nothing is hooked.
// Every engine offset is checked first: the CColModel* is found by giving the model a box collision
// (DzModels::SetBoxCollision) and looking for that pointer in the model info; the draw distance and the RwObject*
// follow it. The atomic must be an atomic (type 1) and the geometry ours (type 8, our vertex / triangle counts),
// else nothing is touched and the League falls back to the drawn ball.
// A marker file is written around the swap: if the game dies there, the next start never tries again.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include "lgball.h"
#include "dzmodels.h"
#include "game.h"
#include "lgball_data.h"
#include "../../imgui/rw_glue.h"
#include "../../imgui/stb_image.h"

namespace
{
struct StreamingInfo { int16_t next, prev, nextOnCd; uint8_t flags, img; uint32_t cdPos, cdSize; uint8_t loadState; uint8_t pad[3]; };
static_assert(sizeof(StreamingInfo) == 20, "CStreamingInfo");
struct RwTexL { void* raster; void* dict; void* l0; void* l1; char name[32]; char mask[32]; uint32_t filterAddr; int32_t refCount; };
struct RwMem { const uint8_t* start; uint32_t length; };

const size_t P = sizeof(void*);
uint8_t** s_MI = nullptr;
StreamingInfo* s_SI = nullptr;
void  (*DeleteRwObject)(void*) = nullptr;
bool  (*LoadAtomicFile)(void*, unsigned) = nullptr;
void* (*StreamOpen)(int, int, const void*) = nullptr;
int   (*StreamClose)(void*, void*) = nullptr;
int   (*StreamFindChunk)(void*, uint32_t, uint32_t*, uint32_t*) = nullptr;
void* (*ClumpStreamRead)(void*) = nullptr;
int   (*ClumpDestroy)(void*) = nullptr;
void* (*TexCreate)(RwRaster*) = nullptr;
void* (*MatSetTexture)(void*, void*) = nullptr;
void  (*RemoveModel)(int) = nullptr;

bool  s_Bad = false, s_Said = false;
int   s_ColOff = -1;          // CBaseModelInfo: CColModel* (then the draw distance, then the RwObject*)
void* s_OrigCol = nullptr; bool s_ColSet = false;
float s_OrigLod = 0;
void* s_Ours = nullptr;       // our atomic while it is the model's one
void* s_Tex = nullptr;
std::string s_Marker;

void Fail(const char* why)
{
    s_Bad = true;
    logger->Error("RlBall: %s - the League uses the drawn ball", why);
}

void* MakeTex()
{
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(kLgBallJpg, (int)sizeof(kLgBallJpg), &w, &h, &c, 4);
    if(!px) return nullptr;
    RwImage* img = RwImageCreate(w, h, 32); if(!img) { stbi_image_free(px); return nullptr; }
    RwImageAllocatePixels(img);
    if(!img->cpPixels || img->stride < w * 4) { RwImageDestroy(img); stbi_image_free(px); return nullptr; }
    for(int y = 0; y < h; ++y) memcpy(img->cpPixels + (size_t)y * img->stride, px + (size_t)y * w * 4, (size_t)w * 4);
    stbi_image_free(px);
    RwInt32 rw = 0, rh = 0, rd = 0, rf = 0;
    RwImageFindRasterFormat(img, 4 /* rwRASTERTYPETEXTURE */, &rw, &rh, &rd, &rf);
    if(rw != w || rh != h) { RwImageDestroy(img); return nullptr; }
    RwRaster* r = RwRasterCreate(rw, rh, rd, rf);
    if(r) r = RwRasterSetFromImage(r, img);
    RwImageDestroy(img);
    if(!r) return nullptr;
    RwTexL* t = (RwTexL*)TexCreate(r);
    if(!t) { RwRasterDestroy(r); return nullptr; }
    strcpy(t->name, "lgball"); t->mask[0] = 0;
    t->filterAddr = 2u /* linear */ | (1u << 8) | (1u << 12);   // wrap u / v
    return t;
}

void* OpenMem(RwMem& m) { m.start = kLgBallDff; m.length = (uint32_t)sizeof(kLgBallDff); return StreamOpen(3 /*rwSTREAMMEMORY*/, 1 /*rwSTREAMREAD*/, &m); }
} // namespace

void RlBall::Init(void* h)
{
    *(void**)&s_MI = (void*)aml->GetSym(h, "_ZN10CModelInfo16ms_modelInfoPtrsE");
    *(void**)&s_SI = (void*)aml->GetSym(h, "_ZN10CStreaming16ms_aInfoForModelE");
    *(void**)&DeleteRwObject = (void*)aml->GetSym(h, "_ZN16CAtomicModelInfo14DeleteRwObjectEv");
    *(void**)&LoadAtomicFile = (void*)aml->GetSym(h, "_ZN11CFileLoader14LoadAtomicFileEP8RwStreamj");
    *(void**)&StreamOpen = (void*)aml->GetSym(h, "_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv");
    *(void**)&StreamClose = (void*)aml->GetSym(h, "_Z13RwStreamCloseP8RwStreamPv");
    *(void**)&StreamFindChunk = (void*)aml->GetSym(h, "_Z17RwStreamFindChunkP8RwStreamjPjS1_");
    *(void**)&ClumpStreamRead = (void*)aml->GetSym(h, "_Z17RpClumpStreamReadP8RwStream");
    *(void**)&ClumpDestroy = (void*)aml->GetSym(h, "_Z14RpClumpDestroyP7RpClump");
    *(void**)&TexCreate = (void*)aml->GetSym(h, "_Z15RwTextureCreateP8RwRaster");
    if(!TexCreate) *(void**)&TexCreate = (void*)aml->GetSym(h, "RwTextureCreate");
    *(void**)&MatSetTexture = (void*)aml->GetSym(h, "_Z20RpMaterialSetTextureP10RpMaterialP9RwTexture");
    *(void**)&RemoveModel = (void*)aml->GetSym(h, "_ZN10CStreaming11RemoveModelEi");
    const char* ad = aml->GetAndroidDataPath();
    if(ad && *ad)
    {
        s_Marker = std::string(ad) + "/lgball_swap.txt";
        if(access(s_Marker.c_str(), F_OK) == 0)
        {   // the game closed during a swap last time
            std::string bad = std::string(ad) + "/lgball_off.txt";
            rename(s_Marker.c_str(), bad.c_str());
        }
        if(access((std::string(ad) + "/lgball_off.txt").c_str(), F_OK) == 0) Fail("the game closed during the ball swap once (delete lgball_off.txt to try again)");
    }
    if(!s_MI || !s_SI || !DeleteRwObject || !LoadAtomicFile || !StreamOpen || !StreamClose || !StreamFindChunk || !ClumpStreamRead || !ClumpDestroy || !TexCreate || !RemoveModel)
        Fail("engine functions missing");
}

bool RlBall::Failed() { return s_Bad; }

bool RlBall::Ready(int model)
{
    if(s_Bad || model <= 0 || model >= 20000) return false;
    if(!Game::LoadModel(model)) return false;                 // requested, still streaming
    uint8_t* mi = s_MI[model];
    if(!mi || s_SI[model].loadState != 1) return false;
    const float R = 0.9275f * 4.0f * 1.02f;
    if(s_ColOff < 0)
    {   // find the CColModel* in the model info: the one place that turns into our box collision
        uint8_t before[128]; memcpy(before, mi, sizeof(before));
        const float box[6] = { -R, -R, -R, R, R, R };
        if(!DzModels::SetBoxCollision(model, box, 1, 0)) { Fail("no box collision"); return false; }
        void* col = DzModels::BoxCol(model);
        for(int off = 32; off + 3 * (int)P <= (int)sizeof(before); off += (int)P)
            if(*(void**)(mi + off) == col && *(void**)(before + off) != col) { s_ColOff = off; s_OrigCol = *(void**)(before + off); break; }
        if(s_ColOff < 0) { Fail("collision pointer not found in the model info"); return false; }
        const float lod = *(float*)(mi + s_ColOff + P);
        if(!(lod > 0.5f && lod < 5000.0f)) { *(void**)(mi + s_ColOff) = s_OrigCol; Fail("draw distance check failed"); return false; }
        s_OrigLod = lod; s_ColSet = true;
        logger->Info("RlBall: model info offsets ok (col %d, draw distance %.0f m)", s_ColOff, lod);
    }
    else if(!s_ColSet)
    {   // again after Restore
        const float box[6] = { -R, -R, -R, R, R, R };
        DzModels::SetBoxCollision(model, box, 1, 0); s_ColSet = true;
    }
    void* rw = *(void**)(mi + s_ColOff + 2 * P);
    if(!rw) return false;
    if(rw == s_Ours) { *(float*)(mi + s_ColOff + P) = 1500.0f; return true; }
    if(*(uint8_t*)rw != 1 /* rpATOMIC */) { Fail("model is not an atomic"); return false; }
    if(!s_Tex && !(s_Tex = MakeTex())) { Fail("texture could not be made"); return false; }
    {   // dry run: our dff must read
        RwMem m; void* s = OpenMem(m); if(!s) { Fail("memory stream"); return false; }
        void* clump = StreamFindChunk(s, 0x10, nullptr, nullptr) ? ClumpStreamRead(s) : nullptr;
        StreamClose(s, nullptr);
        if(!clump) { Fail("the ball dff did not read"); return false; }
        ClumpDestroy(clump);
    }
    if(FILE* f = s_Marker.empty() ? nullptr : fopen(s_Marker.c_str(), "wb")) { fputs("1946", f); fclose(f); }
    DeleteRwObject(mi);
    bool ok = false;
    { RwMem m; void* s = OpenMem(m); if(s) { ok = LoadAtomicFile(s, (unsigned)model); StreamClose(s, nullptr); } }
    void* at = *(void**)(mi + s_ColOff + 2 * P);
    uint8_t* geo = at ? *(uint8_t**)((uint8_t*)at + (P == 8 ? 48 : 24)) : nullptr;
    const bool geoOk = ok && at && *(uint8_t*)at == 1 && geo && geo[0] == 8 &&
                       *(int32_t*)(geo + (P == 8 ? 28 : 20)) == kLgBallVerts && *(int32_t*)(geo + (P == 8 ? 24 : 16)) == kLgBallTris;
    if(!geoOk)
    {
        if(!s_Marker.empty()) unlink(s_Marker.c_str());
        if(at) DeleteRwObject(mi);
        RemoveModel(model);                                  // gone: the next request reads GTA's again
        Fail(ok ? "the loaded ball does not look like ours" : "LoadAtomicFile failed");
        return false;
    }
    void** mats = *(void***)(geo + (P == 8 ? 40 : 32));
    const int nm = *(int32_t*)(geo + (P == 8 ? 48 : 36));
    if(mats && nm >= 1 && mats[0])
    {
        if(MatSetTexture) MatSetTexture(mats[0], s_Tex);
        else { *(void**)mats[0] = s_Tex; ((RwTexL*)s_Tex)->refCount++; }
    }
    *(float*)(mi + s_ColOff + P) = 1500.0f;                  // seen from anywhere in the arena
    s_Ours = at;
    if(!s_Marker.empty()) unlink(s_Marker.c_str());
    if(!s_Said) { s_Said = true; logger->Info("RlBall: object %d is the Rocket League ball now (%d vertices, %d materials)", model, kLgBallVerts, nm); }
    return true;
}

void RlBall::Restore(int model)
{
    if(model <= 0 || model >= 20000 || !s_MI || !s_MI[model] || s_ColOff < 0) return;
    uint8_t* mi = s_MI[model];
    if(s_ColSet) { *(void**)(mi + s_ColOff) = s_OrigCol; s_ColSet = false; }
    if(!s_Ours) return;
    if(*(void**)(mi + s_ColOff + 2 * P) == s_Ours)
    {
        *(float*)(mi + s_ColOff + P) = s_OrigLod;
        if(*(uint16_t*)(mi + P + 26) == 0 && RemoveModel) RemoveModel(model);   // refs 0: drop ours, gta3.img's comes back on the next request
    }
    s_Ours = nullptr;
}
