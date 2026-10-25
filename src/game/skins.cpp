// 2.19.6: player model sync + special characters.
//
// Skin ids on the network:
//   1..311 (not 290-299) : normal ped models (what the game's peds.ide has)
//   290..299             : SA-MP's story characters (Rose, Paul, Cesar, OG Loc, Woozie, Toreno,
//                          Jizzy, Madd Dogg, Catalina, Claude) - same ids as SA-MP
//   312..319             : more story characters (Kendl, The Truth, Maccer, Emmet, T-Bone,
//                          Jethro, Zero, Andre)
// In single player 290-299 are the SPECIAL01..10 slots: a mission loads a named character
// into a slot (LOAD_SPECIAL_CHARACTER) and the ped model 289 + slot becomes that character.
// Remote players with a story character are loaded into MP's own slots 8 / 9 / 10 (models
// 297 / 298 / 299), which story missions practically never use; a slot is only reused when no
// avatar is using it any more (reloading a slot under a live ped is what crashes the game).
//
// Sending: if your ped really is another model (skin mod / skin-changer script / special
// character loaded by a script), that model is what the others see; CJ himself (model 0) can't
// be put on another ped, so CJ players are shown with the avatar skin chosen in the menu.
#include "game.h"
#include "scripting.h"
#include <stdint.h>
uint64_t MP_NowMs();   // 3.21 (net/server.cpp)
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <strings.h>
#include <setjmp.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include "net/protocol.h"

DEFOPCODE(023C, LOAD_SPECIAL_CHARACTER, is);
DEFOPCODE(023D, HAS_SPECIAL_CHARACTER_LOADED, i);
DEFOPCODE(0665, GET_CHAR_MODEL, iv);
DEFOPCODE(038B, LOAD_ALL_MODELS_NOW_SK, );

struct SpecialChar { int skin; const char* name; const char* label; };
static const SpecialChar kSpecials[] =
{
    { 290, "rose",    "Rose" },        { 291, "paul",    "Paul" },       { 292, "cesar",   "Cesar" },
    { 293, "ogloc",   "OG Loc" },      { 294, "wuzimu",  "Woozie" },     { 295, "torino",  "Mike Toreno" },
    { 296, "jizzy",   "Jizzy" },       { 297, "maddogg", "Madd Dogg" },  { 298, "cat",     "Catalina" },
    { 299, "claude",  "Claude" },
    { 312, "kendl",   "Kendl" },       { 313, "truth",   "The Truth" },  { 314, "maccer",  "Maccer" },
    { 315, "emmet",   "Emmet" },       { 316, "tbone",   "T-Bone" },     { 317, "jethro",  "Jethro" },
    { 318, "zero",    "Zero" },        { 319, "andre",   "Andre" },
};
#define SPECIAL_COUNT ((int)(sizeof(kSpecials) / sizeof(kSpecials[0])))

static const int kMpSlots[] = { 10, 9, 8 };      // LOAD_SPECIAL_CHARACTER slots (1-based)
#define MP_SLOT_COUNT 3
#define MAX_SLOT_PEDS 32

static char s_SlotName[11][12];                  // what the game last loaded into each slot (any script)
static bool s_SlotOurs[11];                      // we loaded it
static int  s_SlotPeds[11][MAX_SLOT_PEDS];       // our avatars using the slot
static bool s_BadSpecial[SPECIAL_COUNT];
static uint64_t s_SlotUse[11];
static uint64_t s_SlotLoadAt[11];                // 3.21: csplay requested at (ms), streaming in the background
static uint32_t s_SlotOutfit[11];                // 3.10: "csplay" slot rebuilt with this outfit (MP_ClothesHash)
static uint64_t s_nUseTick = 0;

// ---- watch every special character load (missions too) --------------------------------
static void (*RequestSpecialModel_orig)(int, const char*, int) = nullptr;
static void HookRequestSpecialModel(int model, const char* name, int flags)
{
    if(model >= 290 && model <= 299 && name)
    {
        int slot = model - 289;
        bool same = !strcasecmp(s_SlotName[slot], name);
        if(!same) { strncpy(s_SlotName[slot], name, sizeof(s_SlotName[slot]) - 1); s_SlotName[slot][sizeof(s_SlotName[slot]) - 1] = 0; s_SlotOurs[slot] = false; s_SlotOutfit[slot] = 0; }
    }
    RequestSpecialModel_orig(model, name, flags);
}

void Game::InitSkins(void* h)
{
    memset(s_SlotName, 0, sizeof(s_SlotName));
    void* f = (void*)aml->GetSym(h, "_ZN10CStreaming19RequestSpecialModelEiPKci");
    if(f) aml->Hook(f, (void*)&HookRequestSpecialModel, (void**)&RequestSpecialModel_orig);
    logger->Info("Skins: special character tracking %s", (f && RequestSpecialModel_orig) ? "on" : "off");
    InitClothes(h);
    InitMapObjects(h);   // 3.23.2
}

static const SpecialChar* SpecialBySkin(int skin)
{
    for(int i = 0; i < SPECIAL_COUNT; ++i) if(kSpecials[i].skin == skin) return &kSpecials[i];
    return nullptr;
}
static const SpecialChar* SpecialByName(const char* n)
{
    if(!n || !n[0]) return nullptr;
    for(int i = 0; i < SPECIAL_COUNT; ++i) if(!strcasecmp(kSpecials[i].name, n)) return &kSpecials[i];
    return nullptr;
}

bool Game::IsSpecialSkin(int skin) { return SpecialBySkin(skin) != nullptr; }

bool Game::IsSafeSkin(int skin)
{
    if(const SpecialChar* sc = SpecialBySkin(skin)) return !s_BadSpecial[sc - kSpecials];
    return IsSafePedModel(skin);
}

const char* Game::SkinLabel(int skin)
{
    if(const SpecialChar* sc = SpecialBySkin(skin)) return sc->label;
    return nullptr;
}

int Game::SpecialSkinCount() { return SPECIAL_COUNT; }
int Game::SpecialSkinAt(int i, const char** label)
{
    if(i < 0 || i >= SPECIAL_COUNT) return 0;
    if(label) *label = kSpecials[i].label;
    return kSpecials[i].skin;
}

static int SlotLivePeds(int slot)
{
    int n = 0;
    for(int k = 0; k < MAX_SLOT_PEDS; ++k)
    {
        int ped = s_SlotPeds[slot][k];
        if(!ped) continue;
        if(!Game::PedPtr(ped)) { s_SlotPeds[slot][k] = 0; continue; }
        ++n;
    }
    return n;
}

// model to create for a special skin (loads it into one of MP's slots), 0 = not possible now
static int LoadSpecial(const SpecialChar* sc, int& slotOut)
{
    int idx = (int)(sc - kSpecials);
    if(s_BadSpecial[idx]) return 0;
    // already loaded (by us, or the game has it in one of our slots)
    for(int k = 0; k < MP_SLOT_COUNT; ++k)
    {
        int s = kMpSlots[k];
        if(!strcasecmp(s_SlotName[s], sc->name) && CALLSCM(HAS_SPECIAL_CHARACTER_LOADED, s))
        { slotOut = s; s_SlotUse[s] = ++s_nUseTick; return 289 + s; }
    }
    // a free slot: never loaded, or ours with no avatar on it any more; oldest first
    int best = -1;
    for(int k = 0; k < MP_SLOT_COUNT; ++k)
    {
        int s = kMpSlots[k];
        bool foreign = s_SlotName[s][0] && !s_SlotOurs[s];   // a mission put someone there
        if(foreign && CALLSCM(HAS_SPECIAL_CHARACTER_LOADED, s)) continue;
        if(SlotLivePeds(s) > 0) continue;
        if(best < 0 || s_SlotUse[s] < s_SlotUse[best]) best = s;
    }
    if(best < 0) return 0;
    CALLSCM(LOAD_SPECIAL_CHARACTER, best, sc->name);
    CALLSCM(LOAD_ALL_MODELS_NOW_SK);
    strncpy(s_SlotName[best], sc->name, sizeof(s_SlotName[best]) - 1);
    s_SlotOurs[best] = true;
    s_SlotOutfit[best] = 0;
    s_SlotUse[best] = ++s_nUseTick;
    if(!CALLSCM(HAS_SPECIAL_CHARACTER_LOADED, best))
    {
        logger->Info("Skins: couldn't load special character '%s'", sc->name);
        s_BadSpecial[idx] = true;
        return 0;
    }
    slotOut = best;
    return 289 + best;
}

// called by CreateRemotePed: real model for a network skin (special characters loaded here)
int Game::ModelForSkin(int skin, int& specialSlot)
{
    specialSlot = 0;
    if(const SpecialChar* sc = SpecialBySkin(skin)) return LoadSpecial(sc, specialSlot);
    return IsSafePedModel(skin) ? skin : 0;
}

void Game::NoteSpecialPed(int slot, int ped)
{
    if(slot < 1 || slot > 10 || !ped) return;
    for(int k = 0; k < MAX_SLOT_PEDS; ++k) if(!s_SlotPeds[slot][k] || !PedPtr(s_SlotPeds[slot][k])) { s_SlotPeds[slot][k] = ped; return; }
}

// what the others should see for you
int Game::LocalSkinForNet(int fallback)
{
    int ped = LocalPed();
    if(ped <= 0) return fallback;
    uint32_t model = 0;
    CALLSCM(GET_CHAR_MODEL, ped, &model);
    int m = (int)model;
    if(m <= 0) return fallback;                       // CJ: the avatar skin from the menu
    if(m >= 290 && m <= 299)                          // special character loaded by a script / mod
    {
        const SpecialChar* sc = SpecialByName(s_SlotName[m - 289]);
        return sc ? sc->skin : fallback;
    }
    return IsSafePedModel(m) ? m : fallback;
}


// ---- 3.10: clothing sync ------------------------------------------------------------------
// CJ (model 0) only exists once (he needs CPlayerData), so another player's CJ is rebuilt the
// way the game builds CJ for cutscenes: the cutscene CJ model "csplay" is loaded into one of
// MP's special slots and CClothes::ConstructPedModel(slot model, outfit, null, true) puts the
// outfit on it (CClothes::RebuildCutscenePlayer does exactly this). Every outfit needs its own
// slot (all peds of a model share one 3D model), so up to 3 differently dressed CJs are shown
// at once (the same 3 slots the story characters use); anyone else is shown with their avatar
// skin, like before. The rebuild runs inside a fault guard: if this game's files can't build an
// outfit, that outfit is never tried again (and after 3 faults clothing sync turns itself off).
struct ClothesDescRaw { uint32_t model[MP_CLOTHES_MODELS]; uint32_t tex[MP_CLOTHES_TEXTURES]; float fat, muscle; };   // CPedClothesDesc
static void (*ConstructPedModel_fn)(uint32_t, ClothesDescRaw&, const ClothesDescRaw*, bool) = nullptr;
static uint8_t* s_pPlayers = nullptr;            // CWorld::Players (CPlayerInfo[])
static int  s_nClothesFaults = 0;
static uint32_t s_BadOutfit[32]; static int s_nBadOutfit = 0;

static sigjmp_buf s_ClJmp;
static volatile sig_atomic_t s_bInClothes = 0;
static pthread_t s_ClThread;
static struct sigaction s_ClOldSegv, s_ClOldBus;
static void ClothesFault(int sig, siginfo_t* si, void* uc)
{
    if(s_bInClothes && pthread_equal(pthread_self(), s_ClThread)) { s_bInClothes = 0; siglongjmp(s_ClJmp, 1); }
    struct sigaction* old = sig == SIGBUS ? &s_ClOldBus : &s_ClOldSegv;
    if(old->sa_flags & SA_SIGINFO) { if(old->sa_sigaction) { old->sa_sigaction(sig, si, uc); return; } }
    else if(old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) { old->sa_handler(sig); return; }
    signal(sig, SIG_DFL); raise(sig);
}
static bool GuardedConstruct(int modelId, ClothesDescRaw& d)
{
    if(!ConstructPedModel_fn) return false;
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ClothesFault; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &s_ClOldSegv); sigaction(SIGBUS, &sa, &s_ClOldBus);
    bool ok = true;
    s_ClThread = pthread_self();
    if(sigsetjmp(s_ClJmp, 1) == 0)
    {
        s_bInClothes = 1;
        ConstructPedModel_fn((uint32_t)modelId, d, nullptr, true);
        s_bInClothes = 0;
    }
    else ok = false;
    sigaction(SIGSEGV, &s_ClOldSegv, nullptr); sigaction(SIGBUS, &s_ClOldBus, nullptr);
    return ok;
}
static bool ReadMem(const void* src, void* dst, size_t n)
{
    if((uintptr_t)src < 0x10000) return false;
    struct iovec l = { dst, n }, r = { (void*)src, n };
    return syscall(__NR_process_vm_readv, (long)getpid(), &l, 1L, &r, 1L, 0L) == (long)n;   // API 21 safe
}

void Game::InitClothes(void* h)
{
    ConstructPedModel_fn = (decltype(ConstructPedModel_fn))aml->GetSym(h, "_ZN8CClothes17ConstructPedModelEjR15CPedClothesDescPKS0_b");
    s_pPlayers = (uint8_t*)aml->GetSym(h, "_ZN6CWorld7PlayersE");
    logger->Info("Skins: clothing sync %s", (ConstructPedModel_fn && s_pPlayers) ? "on" : "off (game functions not found)");
}

bool Game::ClothesSyncAvailable() { return ConstructPedModel_fn && s_pPlayers && s_nClothesFaults < 3; }

// your outfit (only while you are CJ); false = not CJ / can't read it
bool Game::GetLocalClothes(PktClothes& c)
{
    memset(&c, 0, sizeof(c));
    c.type = PKT_CLOTHES;
    if(!s_pPlayers) return false;
    int ped = LocalPed();
    if(ped <= 0) return false;
    uint32_t model = 1;
    CALLSCM(GET_CHAR_MODEL, ped, &model);
    if(model != 0) return false;
    // CPlayerInfo { CPlayerPed* m_pPed; CPlayerData m_PlayerData { CWanted* m_pWanted; CPedClothesDesc* m_pPedClothesDesc; ... } }
    void* desc = nullptr;
    if(!ReadMem(s_pPlayers + 2 * sizeof(void*), &desc, sizeof(desc)) || !desc) return false;
    ClothesDescRaw d;
    if(!ReadMem(desc, &d, sizeof(d))) return false;
    memcpy(c.model, d.model, sizeof(c.model));
    memcpy(c.tex, d.tex, sizeof(c.tex));
    c.fat = d.fat; c.muscle = d.muscle;
    c.on = 1;
    return MP_ClothesSane(c);
}

bool Game::ClothesCanBuild(const PktClothes& c)
{
    if(!ClothesSyncAvailable() || !c.on) return false;
    uint32_t h = MP_ClothesHash(c);
    for(int i = 0; i < s_nBadOutfit; ++i) if(s_BadOutfit[i] == h) return false;
    for(int k = 0; k < MP_SLOT_COUNT; ++k)
    {
        int s = kMpSlots[k];
        if(s_SlotOurs[s] && s_SlotOutfit[s] == h) return true;
        bool foreign = s_SlotName[s][0] && !s_SlotOurs[s];
        if(foreign && CALLSCM(HAS_SPECIAL_CHARACTER_LOADED, s)) continue;
        if(SlotLivePeds(s) == 0) return true;
    }
    return false;
}

// 3.21: a csplay slot is streaming in for an outfit (the avatar retries soon)
bool Game::ClothesLoadPending()
{
    for(int k = 0; k < MP_SLOT_COUNT; ++k) if(s_SlotLoadAt[kMpSlots[k]]) return true;
    return false;
}

// model to create for a remote CJ wearing this outfit, 0 = not possible now (use the avatar skin)
int Game::ModelForClothes(const PktClothes& c0, int& slotOut)
{
    slotOut = 0;
    if(!ClothesSyncAvailable() || !c0.on) return 0;
    PktClothes c = c0;
    if(!MP_ClothesSane(c)) return 0;
    uint32_t h = MP_ClothesHash(c);
    for(int i = 0; i < s_nBadOutfit; ++i) if(s_BadOutfit[i] == h) return 0;
    // this outfit is already built in one of our slots
    for(int k = 0; k < MP_SLOT_COUNT; ++k)
    {
        int s = kMpSlots[k];
        if(s_SlotOurs[s] && s_SlotOutfit[s] == h && !strcasecmp(s_SlotName[s], "csplay") && CALLSCM(HAS_SPECIAL_CHARACTER_LOADED, s))
        { slotOut = s; s_SlotUse[s] = ++s_nUseTick; return 289 + s; }
    }
    int best = -1;
    for(int k = 0; k < MP_SLOT_COUNT; ++k)
    {
        int s = kMpSlots[k];
        bool foreign = s_SlotName[s][0] && !s_SlotOurs[s];
        if(foreign && CALLSCM(HAS_SPECIAL_CHARACTER_LOADED, s)) continue;
        if(SlotLivePeds(s) > 0) continue;   // never rebuild a model under a live ped
        if(best < 0 || s_SlotUse[s] < s_SlotUse[best]) best = s;
    }
    // 3.21: crash log CBaseModelInfo::SetColModel (this = NULL) <- CStreaming::LoadAllRequestedModels
    // <- Game::ModelForClothes. LOAD_ALL_MODELS_NOW forced the streamer to finish EVERYTHING it had
    // queued, in the middle of the game's own streaming (RetryLoadFile), and a half-registered model
    // got its collision. Now csplay streams in the background like any model: the slot is asked
    // for here, and the outfit is built on a later call (the avatar retries every 1.5 s meanwhile).
    uint64_t nowMs = MP_NowMs();
    for(int k = 0; k < MP_SLOT_COUNT; ++k)
    {
        int s = kMpSlots[k];
        if(!s_SlotLoadAt[s]) continue;
        if(!s_SlotOurs[s] || strcasecmp(s_SlotName[s], "csplay") || SlotLivePeds(s) > 0) { s_SlotLoadAt[s] = 0; continue; }
        best = s; break;   // already streaming csplay for us: use that slot
    }
    if(best < 0) return 0;
    if(!s_SlotLoadAt[best])
    {
        CALLSCM(LOAD_SPECIAL_CHARACTER, best, "csplay");
        strncpy(s_SlotName[best], "csplay", sizeof(s_SlotName[best]) - 1);
        s_SlotOurs[best] = true;
        s_SlotOutfit[best] = 0;
        s_SlotUse[best] = ++s_nUseTick;
        s_SlotLoadAt[best] = nowMs;
    }
    if(!CALLSCM(HAS_SPECIAL_CHARACTER_LOADED, best))
    {
        if(nowMs - s_SlotLoadAt[best] > 20000)
        {   // 20 s and still not in: give up on clothes for now (avatar skin)
            s_SlotLoadAt[best] = 0; s_SlotName[best][0] = 0; s_SlotOurs[best] = false;
            logger->Info("Skins: couldn't load the cutscene CJ model (csplay)"); ++s_nClothesFaults;
        }
        return 0;
    }
    s_SlotLoadAt[best] = 0;
    ClothesDescRaw d;
    memcpy(d.model, c.model, sizeof(d.model)); memcpy(d.tex, c.tex, sizeof(d.tex));
    d.fat = c.fat; d.muscle = c.muscle;
    if(!GuardedConstruct(289 + best, d))
    {
        ++s_nClothesFaults;
        if(s_nBadOutfit < 32) s_BadOutfit[s_nBadOutfit++] = h;
        s_SlotName[best][0] = 0; s_SlotOurs[best] = false;   // the slot's model is unusable: let the game reload it
        logger->Info("Skins: an outfit couldn't be built on this game - shown with the avatar skin (%d)", s_nClothesFaults);
        return 0;
    }
    s_SlotOutfit[best] = h;
    slotOut = best;
    return 289 + best;
}
