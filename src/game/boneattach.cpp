// 4.00: objects that follow a ped BONE (the Day Z backpack on the spine), like SA-MP / MTA attached
// objects. The script attach (ATTACH_OBJECT_TO_CHAR) only follows the ped's root: the bag floated
// while running (it did not move with the torso) and lagged / jumped in cars (the object is placed in
// the physics step, the ped is drawn later). Now, right after the game has posed the ped for this
// frame (CPed::PreRenderAfterTest), the object's matrix is set from the bone matrix, so both are
// drawn from the same pose.
//
// No axis conventions to guess: once, while the ped stands still on foot, the object's own placement
// (made by the script attach) is stored relative to the bone (L = inverse(bone) * object). After that
// object = bone * L every frame.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "boneattach.h"

namespace
{
struct M4 { float r[3]; uint32_t f; float u[3]; uint32_t p1; float a[3]; uint32_t p2; float p[3]; uint32_t p3; };   // RwMatrix / CMatrix head
static_assert(sizeof(M4) == 64, "RwMatrix");

void* (*GetPed)(int) = nullptr;
void* (*GetObj)(int) = nullptr;
void* (*GetHier)(void*) = nullptr;
int   (*HAnimIdx)(void*, int) = nullptr;
void  (*EntUpdateRW)(void*) = nullptr;
void  (*MatUpdateRW)(void*) = nullptr;   // 4.05: CMatrix::UpdateRW() const - mobile has no CEntity::UpdateRW (the hook was never on)
// 4.05.1: CMatrix::UpdateRW only writes to an *attached* RwMatrix - an entity's CMatrix is never attached on
// mobile (CEntity::AttachToRwObject only copies the frame into it), so 4.05 still drew the bag from the
// old frame. Now the matrix goes straight into the RwFrame's modelling matrix, like CEntity::UpdateRW does.
void  (*MatToRw)(const void*, void*) = nullptr;   // CMatrix::UpdateRwMatrix(RwMatrix*) const
void  (*RwMatUpdate)(void*) = nullptr;            // RwMatrixUpdate - clears the "orthonormal" cache flags
void  (*EntUpdateFrame)(void*) = nullptr;
void  (*EntUpdateRpHAnim)(void*) = nullptr;
void  (*PRAT_orig)(void*) = nullptr;

#if defined(__LP64__)
const int kMatrixOff = 0x18, kRwObjOff = 0x20, kObjParentOff = 0x08, kFrameModelOff = 0x20;
#else
const int kMatrixOff = 0x14, kRwObjOff = 0x18, kObjParentOff = 0x04, kFrameModelOff = 0x10;   // RwObject.parent, RwFrame.modelling
#endif

struct Att { int obj = 0, ped = 0, bone = 3, key = 0; bool want = false, calWanted = false, cal = false; M4 L{}; };
Att s_A[4];
// 4.05: one calibration per bag model / level, kept (and saved) so a bag made again while driving,
// crouching or prone follows the bone at once instead of waiting for the next stand-still
struct Cal { int key = 0; M4 L{}; };
Cal s_Cal[16]; int s_nCal = 0;
char s_szCalFile[512] = "";
const Cal* FindCal(int key) { if(key) for(int i = 0; i < s_nCal; ++i) if(s_Cal[i].key == key) return &s_Cal[i]; return nullptr; }
void SaveCals()
{
    if(!s_szCalFile[0]) return;
    if(FILE* f = fopen(s_szCalFile, "wb")) { fwrite(&s_nCal, sizeof(int), 1, f); fwrite(s_Cal, sizeof(Cal), (size_t)s_nCal, f); fclose(f); }
}
void LoadCals()
{
    s_nCal = 0;
    if(FILE* f = fopen(s_szCalFile, "rb"))
    {
        int n = 0;
        if(fread(&n, sizeof(int), 1, f) == 1 && n > 0 && n <= 16 && fread(s_Cal, sizeof(Cal), (size_t)n, f) == (size_t)n) s_nCal = n;
        fclose(f);
        for(int i = 0; i < s_nCal; ++i)   // sanity: a bag is within 1.5 m of the spine
            if(!(fabsf(s_Cal[i].L.p[0]) < 1.5f && fabsf(s_Cal[i].L.p[1]) < 1.5f && fabsf(s_Cal[i].L.p[2]) < 1.5f)) { s_nCal = 0; break; }
    }
}
void StoreCal(int key, const M4& L)
{
    if(!key) return;
    Cal* c = const_cast<Cal*>(FindCal(key));
    if(!c) { if(s_nCal >= 16) s_nCal = 15; c = &s_Cal[s_nCal++]; c->key = key; }
    c->L = L; SaveCals();
}

inline M4* MatOf(void* ent) { return ent ? *(M4**)((char*)ent + kMatrixOff) : nullptr; }
inline void* ClumpOf(void* ent) { return ent ? *(void**)((char*)ent + kRwObjOff) : nullptr; }

M4* BoneMatrix(void* ped, int boneId)
{
    void* clump = ClumpOf(ped); if(!clump || !GetHier || !HAnimIdx) return nullptr;
    void* h = GetHier(clump); if(!h) return nullptr;
    int n = *(int*)((char*)h + 4);
    M4* arr = *(M4**)((char*)h + 8); if(!arr) return nullptr;
    int i = HAnimIdx(h, boneId); if(i < 0 || i >= n) return nullptr;
    return &arr[i];
}
// columns: right, up, at;  p_world = R * p_local + pos
void Mul(const M4& A, const M4& B, M4& o)   // o = A * B
{
    const float* ac[3] = { A.r, A.u, A.a }; const float* bc[3] = { B.r, B.u, B.a }; float* oc[3] = { o.r, o.u, o.a };
    for(int c = 0; c < 3; ++c) for(int k = 0; k < 3; ++k) oc[c][k] = ac[0][k] * bc[c][0] + ac[1][k] * bc[c][1] + ac[2][k] * bc[c][2];
    for(int k = 0; k < 3; ++k) o.p[k] = ac[0][k] * B.p[0] + ac[1][k] * B.p[1] + ac[2][k] * B.p[2] + A.p[k];
}
void InvRigid(const M4& A, M4& o)   // rotation transposed (the bone matrices of a ped are rigid)
{
    // normalise the columns first (in case of a tiny scale)
    float s[3]; const float* ac[3] = { A.r, A.u, A.a };
    for(int c = 0; c < 3; ++c) { float l = sqrtf(ac[c][0] * ac[c][0] + ac[c][1] * ac[c][1] + ac[c][2] * ac[c][2]); s[c] = l > 1e-6f ? 1.0f / l : 1.0f; }
    float* oc[3] = { o.r, o.u, o.a };
    for(int c = 0; c < 3; ++c) for(int k = 0; k < 3; ++k) oc[c][k] = ac[k][c] * s[k] * s[k];
    for(int k = 0; k < 3; ++k) o.p[k] = -(oc[0][k] * A.p[0] + oc[1][k] * A.p[1] + oc[2][k] * A.p[2]);
}

void HookPRAT(void* self)
{
    PRAT_orig(self);
    for(Att& a : s_A)
    {
        if(!a.want || !a.obj || !a.ped || !GetPed || !GetObj) continue;
        void* ped = GetPed(a.ped); if(ped != self) continue;
        void* obj = GetObj(a.obj); if(!obj) { a.want = false; continue; }
        M4* om = MatOf(obj); if(!om) continue;
        if(EntUpdateRpHAnim) EntUpdateRpHAnim(self);   // this frame's pose
        M4* bm = BoneMatrix(self, a.bone); if(!bm) continue;
        if(a.calWanted)
        {   // the script attach placed it right for the standing pose: keep that, relative to the bone
            M4 inv{}; InvRigid(*bm, inv); Mul(inv, *om, a.L);
            a.calWanted = false; a.cal = true;
            StoreCal(a.key, a.L);
        }
        if(!a.cal) continue;
        M4 w{}; Mul(*bm, a.L, w);
        memcpy(om->r, w.r, 12); memcpy(om->u, w.u, 12); memcpy(om->a, w.a, 12); memcpy(om->p, w.p, 12);
        if(EntUpdateRW) EntUpdateRW(obj);
        else if(void* rw = ClumpOf(obj))
        {   // the frame the object is drawn with
            void* frame = *(void**)((char*)rw + kObjParentOff);
            if(frame)
            {
                M4* fm = (M4*)((char*)frame + kFrameModelOff);
                if(MatToRw) MatToRw(om, fm);
                else { memcpy(fm->r, w.r, 12); memcpy(fm->u, w.u, 12); memcpy(fm->a, w.a, 12); memcpy(fm->p, w.p, 12); fm->f = 0; if(RwMatUpdate) RwMatUpdate(fm); }
            }
        }
        if(EntUpdateFrame) EntUpdateFrame(obj);   // RwFrameUpdateObjects: the new place is used for this frame's draw
    }
}
void* Sym(void* h, const char* a, const char* b = nullptr)
{
    void* p = (void*)aml->GetSym(h, a);
    if(!p && b) p = (void*)aml->GetSym(h, b);
    return p;
}
} // namespace

void BoneAttach::Init(void* h)
{
    *(void**)&GetPed = Sym(h, "_ZN6CPools6GetPedEi");
    *(void**)&GetObj = Sym(h, "_ZN6CPools9GetObjectEi");
    *(void**)&GetHier = Sym(h, "_Z29GetAnimHierarchyFromSkinClumpP7RpClump", "GetAnimHierarchyFromSkinClump");
    *(void**)&HAnimIdx = Sym(h, "_Z17RpHAnimIDGetIndexP16RpHAnimHierarchyi", "RpHAnimIDGetIndex");
    *(void**)&EntUpdateRW = Sym(h, "_ZN7CEntity8UpdateRWEv");
    *(void**)&MatUpdateRW = Sym(h, "_ZNK7CMatrix8UpdateRWEv");
    *(void**)&MatToRw = Sym(h, "_ZNK7CMatrix14UpdateRwMatrixEP11RwMatrixTag");
    *(void**)&RwMatUpdate = Sym(h, "_Z14RwMatrixUpdateP11RwMatrixTag", "RwMatrixUpdate");
    *(void**)&EntUpdateFrame = Sym(h, "_ZN7CEntity13UpdateRwFrameEv");
    *(void**)&EntUpdateRpHAnim = Sym(h, "_ZN7CEntity13UpdateRpHAnimEv");
    void* f = Sym(h, "_ZN4CPed18PreRenderAfterTestEv");
    if(f && GetPed && GetObj && GetHier && HAnimIdx && (EntUpdateRW || EntUpdateFrame)) aml->Hook(f, (void*)&HookPRAT, (void**)&PRAT_orig);
    logger->Info("BoneAttach: %s (pools %d%d, hanim %d%d, rw %d%d%d%d)", PRAT_orig ? "ok" : "off", GetPed != nullptr, GetObj != nullptr,
                 GetHier != nullptr, HAnimIdx != nullptr, EntUpdateRW != nullptr, MatUpdateRW != nullptr, EntUpdateFrame != nullptr, EntUpdateRpHAnim != nullptr);
}
bool BoneAttach::Available() { return PRAT_orig != nullptr; }
void BoneAttach::SetCalFile(const char* path)
{
    snprintf(s_szCalFile, sizeof(s_szCalFile), "%s", path ? path : "");
    LoadCals();
}
void BoneAttach::Follow(int obj, int ped, int boneId, int key)
{
    if(!PRAT_orig || obj <= 0 || ped <= 0) return;
    const Cal* c = FindCal(key);
    for(Att& a : s_A) if(a.want && a.obj == obj) { a.ped = ped; a.bone = boneId; a.key = key; if(c && !a.cal) { a.L = c->L; a.cal = true; } return; }
    for(Att& a : s_A) if(!a.want) { a = Att(); a.obj = obj; a.ped = ped; a.bone = boneId; a.key = key; a.want = true; if(c) { a.L = c->L; a.cal = true; } return; }
}
void BoneAttach::Calibrate(int obj) { for(Att& a : s_A) if(a.want && a.obj == obj && !a.cal) a.calWanted = true; }
bool BoneAttach::Calibrated(int obj) { for(Att& a : s_A) if(a.want && a.obj == obj) return a.cal; return false; }
void BoneAttach::Stop(int obj) { for(Att& a : s_A) if(a.obj == obj) a = Att(); }
