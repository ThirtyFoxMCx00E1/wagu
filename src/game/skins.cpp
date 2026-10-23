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
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <strings.h>

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
static uint64_t s_nUseTick = 0;

// ---- watch every special character load (missions too) --------------------------------
static void (*RequestSpecialModel_orig)(int, const char*, int) = nullptr;
static void HookRequestSpecialModel(int model, const char* name, int flags)
{
    if(model >= 290 && model <= 299 && name)
    {
        int slot = model - 289;
        bool same = !strcasecmp(s_SlotName[slot], name);
        if(!same) { strncpy(s_SlotName[slot], name, sizeof(s_SlotName[slot]) - 1); s_SlotName[slot][sizeof(s_SlotName[slot]) - 1] = 0; s_SlotOurs[slot] = false; }
    }
    RequestSpecialModel_orig(model, name, flags);
}

void Game::InitSkins(void* h)
{
    memset(s_SlotName, 0, sizeof(s_SlotName));
    void* f = (void*)aml->GetSym(h, "_ZN10CStreaming19RequestSpecialModelEiPKci");
    if(f) aml->Hook(f, (void*)&HookRequestSpecialModel, (void**)&RequestSpecialModel_orig);
    logger->Info("Skins: special character tracking %s", (f && RequestSpecialModel_orig) ? "on" : "off");
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
