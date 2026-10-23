#ifndef _SAUTILS_INTERFACE
#define _SAUTILS_INTERFACE

#include <mod/amlmod.h>
#include <stdint.h>



#define _VA_ARGS(...) , ##__VA_ARGS__
#define MAX_SCRIPT_VARS 16
#define MAX_SCRIPT_SIZE	255

#define DEFOPCODE(__opcode, __name, __args) const SCRIPT_COMMAND scm_##__name = { 0x##__opcode, #__args }
#define CALLSCM(__name, ...)             sautils->ScriptCommand(&scm_##__name _VA_ARGS(__VA_ARGS__))


struct SCRIPT_COMMAND
{
    uint16_t  opCode;
    char      params[MAX_SCRIPT_VARS];
};

DEFOPCODE(FFFF, CRASH_GAME, );


class CWidgetButton;
class RpAtomic;
class RwFrame;
class CPhysical;
class CEntity;
class CPed;
class CObject;
class CVehicle;


typedef void        (*OnSettingChangedFn)(int nOldValue, int nNewValue, void* pData);
typedef const char* (*OnSettingDrawedFn) (int nNewValue, void* pData);
typedef void        (*OnButtonPressedFn) (uintptr_t screen);
typedef void        (*OnPlayerProcessFn) (uintptr_t info);
typedef void*       (*LookingForTextureFn)(const char* name);
typedef void        (*SimpleFn)();
typedef void        (*SimpleDataFn)(void* data);


#define MAX_IMG_ARCHIVES                    32
#define MAX_WIDGETS_GAME                    190
#define MAX_WIDGETS                         0xFF



#define SETITEM_SA_TOUCH_LAYOUT             9
#define SETITEM_SA_TOUCH_STEERING           3
#define SETITEM_SA_ACCELEROMETER            8
#define SETITEM_SA_INVERT_LOOK              15

    #define SETITEM_SA_ANALOG_BIKE_STEERING 18
    #define SETITEM_SA_ANALOG_SENSITIVITY   23

    #define SETITEM_SA_JOY_ACCEL_X          25
    #define SETITEM_SA_JOY_ACCEL_Y          26

    #define SETITEM_SA_MOUSE_ACCEL_X        31
    #define SETITEM_SA_MOUSE_ACCEL_Y        32
#define SETITEM_SA_AUTO_CLIMB               35


#define SETITEM_SA_TRAFFIC_MODE             4
#define SETITEM_SA_TARGETING_MODE           6
#define SETITEM_SA_VEH_CAMERA_HEIGHT        10
#define SETITEM_SA_VIBRO_FEEDBACK           29
#define SETITEM_SA_FRAMELIMITER             30


#define SETITEM_SA_BRIGHTNESS               16
#define SETITEM_SA_EFFECTS_QUALITY          0
#define SETITEM_SA_RESOLUTION_SCALING       1
#define SETITEM_SA_DRAW_DISTANCE            2
#define SETITEM_SA_SHADOWS_QUALITY          5
#define SETITEM_SA_CAR_REFLECTIONS          7
#define SETITEM_SA_SUBTITLES                24


#define SETITEM_SA_SFX_VOLUME               11
#define SETITEM_SA_RADIO_VOLUME             12
#define SETITEM_SA_POSITIONAL_SOUND         33
#define SETITEM_SA_RADIO_AUTOTUNE           13
#define SETITEM_SA_CURRENT_RADIO            14


enum eLoadedGame
{
    Unknown = 0,
    GTASA_2_00,
    GTAVC_1_09,
    GTA3_1_09,
    GTALCS_1_09,
    GTASA_2_10,
};

enum eTypeOfSettings : unsigned char
{
    SetType_Controller = 0,
    SetType_Game = 1,
    SetType_Display = 2,
    SetType_Audio = 3,
    SetType_Language = 4,
    SetType_Mods = 5,

    SETTINGS_COUNT
};

enum eTypeOfItem : unsigned char
{
    ItemType_WithItems = 0,
    ItemType_Slider = 1,
    ItemType_Button = 2,

    ITEMTYPES_COUNT
};

enum eWidgetState : unsigned char
{
    WState_Touched = 0,
    WState_DoubleTapped,
    WState_Released,
    WState_HeldDown,
    WState_SwipedLeft,
    WState_SwipedRight,
    WState_SwipedUp,
    WState_SwipedDown,

    WIDGETSTATE_MAX
};

enum eRenderOfType : unsigned char
{
    ROfType_Effects = 0,
    ROfType_Menu,
    ROfType_Hud,
    ROfType_Ped,
    ROfType_Vehicle,
    ROfType_Object,
    ROfType_RadarBlips,
    ROfType_MapBlips,

    RENDEROFTYPE_MAX
};

enum eSettingsTabButtonLoc : unsigned char
{
    STB_MainMenu = 0,
    STB_Pause,
    STB_StartGame,
    STB_Settings,

    TABBUTTONLOC_MAX,
};

enum ePoolType : unsigned char
{
    POOLTYPE_PEDS,
    POOLTYPE_VEHICLES,
    POOLTYPE_OBJECTS,

    POOLTYPES_MAX,
};

enum eTexDBType : unsigned char
{
    TEXDBTYPE_DXT,
    TEXDBTYPE_ETC,
    TEXDBTYPE_PVR,

    TEXDBTYPES_MAX,
};


class ISAUtils
{
public:






    virtual uintptr_t IsFLALoaded() = 0;


    #if __cplusplus >= 201300
      [[deprecated("Use AddClickableItem or AddSliderItem")]]
    #endif
    virtual int AddSettingsItem(eTypeOfSettings typeOf, const char* name, int initVal = 0, int minVal = 0, int maxVal = 0, OnSettingChangedFn fnOnValueChange = NULL, bool isSlider = false, void* switchesArray = NULL) = 0;






    virtual int ValueOfSettingsItem(int settingId) = 0;















    virtual int AddClickableItem(eTypeOfSettings typeOf, const char* name, int initVal = 0, int minVal = 0, int maxVal = 0, const char** switchesArray = NULL, OnSettingChangedFn fnOnValueChange = NULL) = 0;












    virtual int AddSliderItem(eTypeOfSettings typeOf, const char* name, int initVal = 0, int minVal = 0, int maxVal = 0, OnSettingChangedFn fnOnValueChange = NULL, OnSettingDrawedFn fnOnValueDraw = NULL) = 0;










    virtual void AddButton(eTypeOfSettings typeOf, const char* name, OnButtonPressedFn fnOnButtonPressed = NULL) = 0;







    virtual uintptr_t* AddTextureDB(const char* name, bool registerMe = false) = 0;






    virtual int* GetSettingValuePointer(int settingId) = 0;





    virtual void AddIMG(const char* imgName) = 0;











    virtual eTypeOfSettings AddSettingsTab(const char* name, const char* textureName = "menu_mainsettings") = 0;





    virtual unsigned int GetCurrentMs() = 0;





    virtual float GetCurrentFPS() = 0;






    virtual uintptr_t GetTextureDB(const char* texDbName) = 0;





    virtual void RegisterTextureDB(uintptr_t textureDbPtr) = 0;





    virtual void UnregisterTextureDB(uintptr_t textureDbPtr) = 0;






    virtual uintptr_t GetTexture(const char* texName) = 0;





    virtual void AddOnWidgetsCreateListener(SimpleFn fn) = 0;





    virtual void AddPlayerUpdateListener(OnPlayerProcessFn fn, bool post = true) = 0;





    virtual int FindFreeWidgetId() = 0;










    virtual CWidgetButton* CreateWidget(int widgetId, int x, int y, float scale, const char* textureName = NULL) = 0;






    virtual int GetWidgetIndex(CWidgetButton* widget) = 0;






    virtual void SetWidgetIcon(CWidgetButton* widget, uintptr_t texturePtr) = 0;






    virtual void SetWidgetIcon(CWidgetButton* widget, const char* textureName) = 0;






    virtual void SetWidgetIcon2(CWidgetButton* widget, uintptr_t texturePtr) = 0;






    virtual void SetWidgetIcon2(CWidgetButton* widget, const char* textureName) = 0;






    virtual void ToggleWidget(CWidgetButton* widget, bool enable = true) = 0;






    virtual bool IsWidgetEnabled(int widgetId) = 0;






    #ifdef AML32
        inline bool IsWidgetEnabled(CWidgetButton* widget) { return *(bool*)((uintptr_t)widget + 77); };
    #else
        inline bool IsWidgetEnabled(CWidgetButton* widget) { return *(bool*)((uintptr_t)widget + 92); };
    #endif





    virtual void ClearWidgetTapHistory(int widgetId) = 0;





    virtual void ClearWidgetTapHistory(CWidgetButton* widget) = 0;







    virtual int GetWidgetState(CWidgetButton* widget, eWidgetState stateToGet) = 0;










    virtual int GetWidgetState(int widgetId, eWidgetState stateToGet, bool doDoubleTapEff = true, int frames = 1) = 0;









    virtual void GetWidgetPos(int widgetId, float* x = NULL, float* y = NULL, float* sx = NULL, float* sy = NULL) = 0;









    virtual void SetWidgetPos(int widgetId, float x, float y, float sx, float sy);









    virtual void* LoadRwTextureFromPNG(const char* filename) = 0;






    virtual void* LoadRwTextureFromBMP(const char* filename) = 0;





    virtual void AddOnRWInitListener(SimpleFn fn) = 0;





    virtual void AddTextureLookupListener(LookingForTextureFn fn) = 0;













    virtual int AddClickableItem(eTypeOfSettings typeOf, const char* name, int initVal, int minVal, int maxVal, const char** switchesArray, OnSettingChangedFn fnOnValueChange, void* data) = 0;













    virtual int AddSliderItem(eTypeOfSettings typeOf, const char* name, int initVal, int minVal, int maxVal, OnSettingChangedFn fnOnValueChange, OnSettingDrawedFn fnOnValueDraw, void* data) = 0;











    virtual int ScriptCommand(const SCRIPT_COMMAND *pScriptCommand, ...) = 0;






    virtual void AddOnRenderListener(eRenderOfType typeOf, SimpleDataFn fn) = 0;











    virtual void AddSettingsTabButton(const char* name, SimpleDataFn fn, eSettingsTabButtonLoc loc = STB_Settings, const char* textureName = "menu_mainsettings", void* data = NULL) = 0;









    virtual bool LoadDFF(const char* name, bool doPrelit = false, RpAtomic** atomic = NULL, RwFrame** frame = NULL) = 0;





    virtual eLoadedGame GetLoadedGame() = 0;





    virtual uintptr_t GetLoadedGameLibAddress() = 0;











    virtual uintptr_t* AddTextureDBOfType(const char* name, eTexDBType type, bool registerMe = false);






    virtual int GetPoolSize(ePoolType poolType) = 0;






    virtual int GetPoolMemSize(ePoolType poolType) = 0;






    virtual int GetPoolIndex(void* entityPtr) = 0;







    virtual void* GetPoolMember(ePoolType poolType, int index) = 0;








    virtual bool IsGameInitialised();










    virtual void SetPosition(CPhysical* ent, float x, float y, float z, bool resetRotation = false);








    virtual void SetAngle(CPhysical* ent, unsigned char axis, float angle);









    virtual void SetAngle(CPhysical* ent, float x = -999, float y = -999, float z = -999);






    virtual void LoadModelId(int modelId);







    virtual void LoadArea(float x, float y);











    virtual CPed* CreatePed(int pedType, int modelId, float x, float y, float z, int *ref = NULL);










    virtual CPed* CreatePed(int pedType, int modelId, CVehicle* vehicle, int seat = -1, int *ref = NULL);










    virtual CVehicle* CreateVehicle(int modelId, float x, float y, float z, int *ref = NULL);










    virtual CObject* CreateObject(int modelId, float x, float y, float z, int *ref = NULL);






    virtual void MarkEntityAsNotNeeded(CEntity* ent);






    virtual void MarkModelAsNotNeeded(int modelId);








    virtual void PutPedInVehicle(CPed* ped, CVehicle* vehicle, int seat = -1);
};

#endif